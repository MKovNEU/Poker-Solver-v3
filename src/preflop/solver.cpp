#include "include/preflop/solver.h"

#include <immintrin.h>
#include <omp.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>

// Values are counterfactual: a seat's value for a class is its expected result with one combo of that class, weighted
// by how likely every other seat is to have played to this spot. Each opponent contributes the share of its reach
// that is compatible with the combo (of the 1225 combos left once the combo is dealt), so a seat that hasn't acted
// counts as 1 and the root values are plain EVs in big blinds.

namespace {

const char MAGIC[8] = {'P', 'F', 'S', 'O', 'L', 'V', 'E', '1'};

// per class: its combos, its two ranks (12 = ace) and how many of its combos hold one given card of each rank
struct ClassInfo {
    std::array<float, NUM_CLASSES> combos, per_card_hi, per_card_lo;
    std::array<uint8_t, NUM_CLASSES> hi, lo;
    std::array<int16_t, NUM_COMBOS> of_combo;
};

const ClassInfo& classes() {
    static const ClassInfo info = [] {
        ClassInfo c{};
        for (int cls = 0; cls < NUM_CLASSES; cls++) {
            const int row = cls / 13, col = cls % 13;
            if (row == col) { // pair: 3 of its combos hold a given card of its rank
                c.hi[cls] = c.lo[cls] = (uint8_t)(12 - row);
                c.combos[cls] = 6;
                c.per_card_hi[cls] = 3;
                c.per_card_lo[cls] = 0;
            } else if (row < col) { // suited
                c.hi[cls] = (uint8_t)(12 - row);
                c.lo[cls] = (uint8_t)(12 - col);
                c.combos[cls] = 4;
                c.per_card_hi[cls] = c.per_card_lo[cls] = 1;
            } else { // offsuit
                c.hi[cls] = (uint8_t)(12 - col);
                c.lo[cls] = (uint8_t)(12 - row);
                c.combos[cls] = 12;
                c.per_card_hi[cls] = c.per_card_lo[cls] = 3;
            }
        }
        for (int combo = 0; combo < NUM_COMBOS; combo++) c.of_combo[combo] = (int16_t)combo_class(combo);
        return c;
    }();
    return info;
}

const float* ones() {
    static const std::array<float, NUM_CLASSES> v = [] {
        std::array<float, NUM_CLASSES> a;
        a.fill(1.0f);
        return a;
    }();
    return v.data();
}

// share of a seat's reach that holds no card of a combo of each class (inclusion-exclusion over the two cards: every
// combo holding either card, the combo itself counted twice)
void compatible(const float* r, float* m) {
    const ClassInfo& c = classes();
    float total = 0.0f, by_rank[13] = {};
    for (int j = 0; j < NUM_CLASSES; j++) {
        total += r[j] * c.combos[j];
        by_rank[c.hi[j]] += r[j] * c.per_card_hi[j];
        by_rank[c.lo[j]] += r[j] * c.per_card_lo[j];
    }
    for (int i = 0; i < NUM_CLASSES; i++) m[i] = (total - by_rank[c.hi[i]] - by_rank[c.lo[i]] + r[i]) * (1.0f / 1225.0f);
}

void normalize(float* sigma, int A, float* sum) {
    const float uniform = 1.0f / A;
    for (int i = 0; i < NUM_CLASSES; i++) sum[i] = sum[i] > 0.0f ? 1.0f / sum[i] : 0.0f;
    for (int a = 0; a < A; a++) {
        float* s = sigma + (size_t)a * NUM_CLASSES;
        for (int i = 0; i < NUM_CLASSES; i++) s[i] = sum[i] > 0.0f ? s[i] * sum[i] : uniform;
    }
}

void set_fast_float_mode() { _mm_setcsr(_mm_getcsr() | 0x8040); }

template <class T> void write_pod(std::ofstream& f, const T& v) { f.write(reinterpret_cast<const char*>(&v), sizeof(T)); }

template <class T> void read_pod(std::ifstream& f, T& v) {
    f.read(reinterpret_cast<char*>(&v), sizeof(T));
    if (!f) throw std::runtime_error("save file is truncated");
}

} // namespace

PreflopSolver::PreflopSolver(const PreflopConfig& config, const PreflopEquity& equity) : config_(config) {
    threads_ = config.threads > 0 ? config.threads : omp_get_max_threads();
    build_preflop_tree(tree_, config_);
    build_matrices(equity);
    regret_.assign(tree_.storage, 0.0f);
    cum_.assign(tree_.storage, 0.0f);
}

void PreflopSolver::build_matrices(const PreflopEquity& equity) {
    const ClassInfo& c = classes();
    constexpr int K = NUM_CLASSES;

    // depth levels used by the showdowns (0 = all-in, plain equity), and every class's realization at each
    std::map<int, uint16_t> level_of;
    std::vector<float> depths;
    terminal_level_.assign(tree_.terminals.size(), 0);
    for (size_t t = 0; t < tree_.terminals.size(); t++) {
        const int key = (int)std::lround(tree_.terminals[t].depth * 100.0);
        auto it = level_of.find(key);
        if (it == level_of.end()) {
            it = level_of.emplace(key, (uint16_t)depths.size()).first;
            depths.push_back(tree_.terminals[t].depth);
        }
        terminal_level_[t] = it->second;
    }
    const size_t L = depths.size();
    const std::array<float, NUM_CLASSES> play = playability_factors(config_.playability);
    std::vector<std::array<double, NUM_CLASSES>> real_oop(L), real_ip(L);
    for (size_t l = 0; l < L; l++) {
        for (int cls = 0; cls < K; cls++) {
            real_oop[l][cls] = preflop_realization(config_, play, cls, true, depths[l]);
            real_ip[l][cls] = preflop_realization(config_, play, cls, false, depths[l]);
        }
    }

    std::vector<double> pairs((size_t)K * K, 0.0);
    std::vector<std::vector<double>> oop(L, std::vector<double>((size_t)K * K, 0.0)), ip = oop;
#pragma omp parallel for schedule(dynamic, 8) num_threads(threads_)
    for (int i = 0; i < K; i++) { // rows by class so threads never share an output row
        for (int a = 0; a < NUM_COMBOS; a++) {
            if (c.of_combo[a] != i) continue;
            for (int b = 0; b < NUM_COMBOS; b++) {
                if (combo_mask(a) & combo_mask(b)) continue;
                const int j = c.of_combo[b];
                const size_t cell = (size_t)i * K + j;
                pairs[cell] += 1.0;
                const double e = equity(a, b);
                for (size_t l = 0; l < L; l++) {
                    const double ro = real_oop[l][i], ri = real_ip[l][j]; // a is OOP against an IP b
                    oop[l][cell] += e * ro / (e * ro + (1.0 - e) * ri);
                    const double ra = real_ip[l][i], rb = real_oop[l][j]; // a is IP against an OOP b
                    ip[l][cell] += e * ra / (e * ra + (1.0 - e) * rb);
                }
            }
        }
    }

    compat_.assign((size_t)K * K, 0.0f);
    share_oop_.assign(L, std::vector<float>((size_t)K * K, 0.0f));
    share_ip_ = share_oop_;
    for (int i = 0; i < K; i++) {
        const double scale = 1.0 / (1225.0 * c.combos[i]);
        for (int j = 0; j < K; j++) {
            const size_t cell = (size_t)i * K + j;
            compat_[cell] = (float)(pairs[cell] * scale);
            for (size_t l = 0; l < L; l++) {
                share_oop_[l][cell] = (float)(oop[l][cell] * scale);
                share_ip_[l][cell] = (float)(ip[l][cell] * scale);
            }
        }
    }
}

// ---------------------------------------------------------------- traversal

void PreflopSolver::fold_value(float lost, int p, const Reach& reach, float* out) const {
    float m[NUM_CLASSES];
    for (int i = 0; i < NUM_CLASSES; i++) out[i] = -lost;
    for (int o = 0; o < config_.players; o++) {
        if (o == p || reach[o] == ones()) continue;
        compatible(reach[o], m);
        for (int i = 0; i < NUM_CLASSES; i++) out[i] *= m[i];
    }
}

// the traverser is always one of the seats left: its own folds are valued where it folds
void PreflopSolver::terminal(const PNode& n, int p, const Reach& reach, float* out) const {
    const PTerminal& t = tree_.terminals[n.data];
    const int q = n.type == PNodeType::SHOWDOWN ? (t.oop == p ? t.ip : t.oop) : -1;
    float mult[NUM_CLASSES], m[NUM_CLASSES];
    std::fill(mult, mult + NUM_CLASSES, 1.0f);
    for (int o = 0; o < config_.players; o++) {
        if (o == p || o == q || reach[o] == ones()) continue;
        compatible(reach[o], m);
        for (int i = 0; i < NUM_CLASSES; i++) mult[i] *= m[i];
    }
    if (n.type == PNodeType::FOLD) {
        const float won = t.pot - t.invested[p];
        for (int i = 0; i < NUM_CLASSES; i++) out[i] = won * mult[i];
        return;
    }
    // pot * share - invested, summed over the opponent's compatible combos
    const float* S = (p == t.oop ? share_oop_ : share_ip_)[terminal_level_[n.data]].data();
    const float* C = compat_.data();
    const float* r = reach[q];
    const float pot = t.pot, invested = t.invested[p];
    for (int i = 0; i < NUM_CLASSES; i++) {
        const float* s = S + (size_t)i * NUM_CLASSES;
        const float* c = C + (size_t)i * NUM_CLASSES;
        float share = 0.0f, mass = 0.0f;
        for (int j = 0; j < NUM_CLASSES; j++) {
            share += s[j] * r[j];
            mass += c[j] * r[j];
        }
        out[i] = mult[i] * (pot * share - invested * mass);
    }
}

template <PreflopSolver::Mode M>
void PreflopSolver::traverse(uint32_t id, int p, const Reach& reach, float* out, const Discount& d, int depth) {
    const PNode& n = tree_.nodes[id];
    if (n.type != PNodeType::ACTION) {
        terminal(n, p, reach, out);
        return;
    }
    const int A = n.num_children, q = n.player;
    float sigma[MAX_PREFLOP_ACTIONS][NUM_CLASSES], sum[NUM_CLASSES];
    const float* store = (M == Mode::CFR ? regret_ : cum_).data() + n.data;
    std::fill(sum, sum + NUM_CLASSES, 0.0f);
    for (int a = 0; a < A; a++) {
        for (int i = 0; i < NUM_CLASSES; i++) {
            const float v = store[(size_t)a * NUM_CLASSES + i];
            sigma[a][i] = M == Mode::CFR ? std::max(v, 0.0f) : v;
            sum[i] += sigma[a][i];
        }
    }
    normalize(&sigma[0][0], A, sum);

    float child_reach[MAX_PREFLOP_ACTIONS][NUM_CLASSES], values[MAX_PREFLOP_ACTIONS][NUM_CLASSES];
    const bool tasks = threads_ > 1 && depth < 4;
    for (int a = 0; a < A; a++) {
        const uint32_t child = n.first_child + a;
        bool live = q == p;
        for (int i = 0; i < NUM_CLASSES; i++) {
            child_reach[a][i] = reach[q][i] * sigma[a][i];
            live |= child_reach[a][i] > 0.0f;
        }
        if (!live) { // an opponent never takes this action, nothing below matters
            std::fill(values[a], values[a] + NUM_CLASSES, 0.0f);
            continue;
        }
        if (q == p && tree_.meta[child].action == PAction::FOLD) {
            fold_value(n.invested, p, reach, values[a]);
            continue;
        }
        Reach next = reach;
        next[q] = child_reach[a];
        if (tasks) {
#pragma omp task firstprivate(next, child, a) shared(values, d)
            traverse<M>(child, p, next, values[a], d, depth + 1);
        } else {
            traverse<M>(child, p, next, values[a], d, depth + 1);
        }
    }
    if (tasks) {
#pragma omp taskwait
    }

    if (q != p) {
        std::copy(values[0], values[0] + NUM_CLASSES, out);
        for (int a = 1; a < A; a++)
            for (int i = 0; i < NUM_CLASSES; i++) out[i] += values[a][i];
        return;
    }
    if constexpr (M == Mode::BEST_RESPONSE) {
        std::copy(values[0], values[0] + NUM_CLASSES, out);
        for (int a = 1; a < A; a++)
            for (int i = 0; i < NUM_CLASSES; i++) out[i] = std::max(out[i], values[a][i]);
        return;
    }
    for (int i = 0; i < NUM_CLASSES; i++) out[i] = sigma[0][i] * values[0][i];
    for (int a = 1; a < A; a++)
        for (int i = 0; i < NUM_CLASSES; i++) out[i] += sigma[a][i] * values[a][i];

    if constexpr (M == Mode::CFR) {
        float* regret = regret_.data() + n.data;
        float* cum = cum_.data() + n.data;
        for (int a = 0; a < A; a++) {
            float* r = regret + (size_t)a * NUM_CLASSES;
            float* c = cum + (size_t)a * NUM_CLASSES;
            for (int i = 0; i < NUM_CLASSES; i++) {
                float x = r[i];
                x *= x > 0.0f ? d.alpha : d.beta;
                r[i] = x + values[a][i] - out[i];
                c[i] = c[i] * d.gamma + reach[p][i] * sigma[a][i];
            }
        }
    }
}

double PreflopSolver::root_value(Mode mode, int p) {
    Reach reach;
    reach.fill(ones());
    float v[NUM_CLASSES];
    const Discount d = mode == Mode::CFR ? Discount::at(iterations_) : Discount{};
    auto go = [&] {
        set_fast_float_mode();
        switch (mode) {
            case Mode::CFR: traverse<Mode::CFR>(0, p, reach, v, d, 0); break;
            case Mode::BEST_RESPONSE: traverse<Mode::BEST_RESPONSE>(0, p, reach, v, d, 0); break;
            case Mode::AVERAGE: traverse<Mode::AVERAGE>(0, p, reach, v, d, 0); break;
        }
    };
    if (threads_ > 1) {
#pragma omp parallel num_threads(threads_)
        {
            set_fast_float_mode();
#pragma omp single
            go();
        }
    } else {
        go();
    }
    const ClassInfo& c = classes();
    double ev = 0.0;
    for (int i = 0; i < NUM_CLASSES; i++) ev += (double)c.combos[i] * v[i];
    return ev / NUM_COMBOS;
}

// ---------------------------------------------------------------- solving

void PreflopSolver::iterate(int t) {
    iterations_ = t;
    for (int p = 0; p < config_.players; p++) root_value(Mode::CFR, p);
    iterations_ = t + 1;
}

double PreflopSolver::exploitability() {
    double gain = 0.0;
    for (int p = 0; p < config_.players; p++) gain += root_value(Mode::BEST_RESPONSE, p) - root_value(Mode::AVERAGE, p);
    return 1000.0 * gain / config_.players;
}

std::vector<double> PreflopSolver::expected_values() {
    std::vector<double> ev(config_.players);
    for (int p = 0; p < config_.players; p++) ev[p] = root_value(Mode::AVERAGE, p);
    return ev;
}

double PreflopSolver::solve(bool verbose) {
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    double e = std::numeric_limits<double>::infinity();
    for (int t = iterations_; t < config_.iterations; t++) {
        iterate(t);
        const bool last = t + 1 == config_.iterations;
        if ((t + 1) % config_.check_every == 0 || last) {
            e = exploitability();
            if (verbose) {
                const double secs = std::chrono::duration<double>(clock::now() - start).count();
                std::printf("iteration %5d   exploitability %9.3f mbb/hand   %7.1fs\n", t + 1, e, secs);
                std::fflush(stdout);
            }
            if (e <= config_.target) break;
        }
    }
    return e;
}

void PreflopSolver::average_strategy(uint32_t node, float* out) const {
    const PNode& n = tree_.nodes[node];
    if (n.type != PNodeType::ACTION) throw std::runtime_error("not an action node");
    const int A = n.num_children;
    float sum[NUM_CLASSES] = {};
    for (int a = 0; a < A; a++) {
        for (int i = 0; i < NUM_CLASSES; i++) {
            out[(size_t)a * NUM_CLASSES + i] = cum_[n.data + (size_t)a * NUM_CLASSES + i];
            sum[i] += out[(size_t)a * NUM_CLASSES + i];
        }
    }
    normalize(out, A, sum);
}

// ---------------------------------------------------------------- save / load

void PreflopSolver::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write '" + path + "'");
    f.write(MAGIC, sizeof MAGIC);
    write_pod(f, (uint64_t)config_.text.size());
    f.write(config_.text.data(), (std::streamsize)config_.text.size());
    write_pod(f, (uint64_t)tree_.storage);
    write_pod(f, (int32_t)iterations_);
    std::vector<float> sigma(MAX_PREFLOP_ACTIONS * NUM_CLASSES);
    std::vector<uint16_t> q;
    for (uint32_t id = 0; id < tree_.nodes.size(); id++) {
        const PNode& n = tree_.nodes[id];
        if (n.type != PNodeType::ACTION) continue;
        const size_t len = (size_t)n.num_children * NUM_CLASSES;
        average_strategy(id, sigma.data());
        q.resize(len);
        for (size_t i = 0; i < len; i++) q[i] = (uint16_t)std::lround(std::clamp(sigma[i], 0.0f, 1.0f) * 65535.0f);
        f.write(reinterpret_cast<const char*>(q.data()), (std::streamsize)(len * sizeof(uint16_t)));
    }
    if (!f) throw std::runtime_error("failed writing '" + path + "'");
}

std::unique_ptr<PreflopSolver> PreflopSolver::load(const std::string& path, const PreflopEquity& equity) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open '" + path + "'");
    char magic[sizeof MAGIC];
    f.read(magic, sizeof magic);
    if (!f || std::memcmp(magic, MAGIC, sizeof MAGIC) != 0) throw std::runtime_error("'" + path + "' is not a preflop save file");
    uint64_t len = 0;
    read_pod(f, len);
    std::string text(len, '\0');
    f.read(text.data(), (std::streamsize)len);
    auto solver = std::make_unique<PreflopSolver>(parse_preflop_config(text), equity);
    uint64_t storage = 0;
    int32_t iterations = 0;
    read_pod(f, storage);
    read_pod(f, iterations);
    if (storage != solver->tree_.storage) throw std::runtime_error("save file doesn't match the rebuilt tree");
    solver->iterations_ = iterations;
    std::vector<uint16_t> q;
    for (const PNode& n : solver->tree_.nodes) {
        if (n.type != PNodeType::ACTION) continue;
        const size_t count = (size_t)n.num_children * NUM_CLASSES;
        q.resize(count);
        f.read(reinterpret_cast<char*>(q.data()), (std::streamsize)(count * sizeof(uint16_t)));
        if (!f) throw std::runtime_error("save file is truncated");
        for (size_t i = 0; i < count; i++) solver->cum_[n.data + i] = q[i] / 65535.0f;
    }
    return solver;
}
