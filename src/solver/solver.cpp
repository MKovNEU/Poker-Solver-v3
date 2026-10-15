#include "include/solver/solver.h"

#include <immintrin.h>
#include <omp.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

#include "include/core/hand_eval.h"

// Vector-form CFR: one traversal per player per iteration walks the public tree with the opponent's reach
// vector going down and the traverser's counterfactual values coming back up, so a node costs O(actions * hands)
// for every hand at once and the game logic never runs during solving.
//  - DCFR (alpha 1.5, beta 0.5, gamma 3) with alternating updates
//  - fold / showdown terminals in O(hands + 52) with exact card removal (inclusion-exclusion over the 52 cards)
//  - suit isomorphic turn / river cards are never built, their values are a hand permutation of the representative's
//  - subtrees the opponent never reaches are skipped
//  - chance nodes fan out into OpenMP tasks, each thread works from its own scratch arena
//  - fp16 storage (per node scale, stochastic rounding for the average) halves memory and memory traffic

namespace {

const char MAGIC[8] = {'P', 'F', 'S', 'O', 'L', 'V', 'E', '1'};

// flush denormals to zero, tiny reach products would otherwise slow the float units down badly
void set_fast_float_mode() { _mm_setcsr(_mm_getcsr() | 0x8040); }

// turns non-negative weights per [action][hand] into probabilities, uniform where a hand has no weight
void normalize(float* sigma, int A, int N, float* sum) {
    const float uniform = 1.0f / A;
    for (int h = 0; h < N; h++) sum[h] = sum[h] > 0.0f ? 1.0f / sum[h] : 0.0f;
    for (int a = 0; a < A; a++) {
        float* s = sigma + (size_t)a * N;
        for (int h = 0; h < N; h++) s[h] = sum[h] > 0.0f ? s[h] * sum[h] : uniform;
    }
}

// regret and sigma may be the same buffer
void regret_matching(const float* regret, int A, int N, float* sigma, float* sum) {
    for (int h = 0; h < N; h++) sum[h] = 0.0f;
    for (int a = 0; a < A; a++) {
        const float* r = regret + (size_t)a * N;
        float* s = sigma + (size_t)a * N;
        for (int h = 0; h < N; h++) {
            float v = r[h] > 0.0f ? r[h] : 0.0f;
            s[h] = v;
            sum[h] += v;
        }
    }
    normalize(sigma, A, N, sum);
}

bool all_zero(const float* v, int n) {
    float m = 0.0f;
    for (int i = 0; i < n; i++) m = std::max(m, v[i]);
    return m <= 0.0f;
}

// ---- fp16 storage, every node block is stored divided by its largest magnitude

inline float half_to_float(uint16_t h) {
    _Float16 f;
    std::memcpy(&f, &h, sizeof h);
    return (float)f;
}

inline uint16_t float_to_half(float x) {
    _Float16 f = (_Float16)x;
    uint16_t h;
    std::memcpy(&h, &f, sizeof h);
    return h;
}

void decode_half(const uint16_t* src, float scale, float* dst, size_t n) {
    size_t i = 0;
#ifdef __F16C__
    const __m256 s = _mm256_set1_ps(scale);
    for (; i + 8 <= n; i += 8) {
        __m256 v = _mm256_cvtph_ps(_mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i)));
        _mm256_storeu_ps(dst + i, _mm256_mul_ps(v, s));
    }
#endif
    for (; i < n; i++) dst[i] = half_to_float(src[i]) * scale;
}

inline uint32_t hash32(uint32_t x) {
    x *= 0x9E3779B1u;
    x ^= x >> 16;
    x *= 0x85EBCA6Bu;
    x ^= x >> 13;
    return x;
}

// fp32 -> fp16, the block divided by its largest magnitude. With Stochastic, random bits below fp16 precision are
// added to the fp32 bit pattern and the conversion truncates, which is unbiased: round-to-nearest biases accumulators
// that grow much larger than their increments (the cumulative strategy grows ~t/4 times each update) and stalls
// convergence around 0.07% of pot. The noise is a hash of (seed, index), so results don't depend on thread scheduling.
// Returns the scale to decode with.
template <bool Stochastic>
float encode_half(const float* src, uint16_t* dst, size_t n, uint32_t seed) {
    float m = 0.0f;
    for (size_t i = 0; i < n; i++) m = std::max(m, std::fabs(src[i]));
    const float scale = m > 0.0f ? m : 1.0f;
    const float inv = 1.0f / scale;
    size_t i = 0;
#if defined(__F16C__) && defined(__AVX2__)
    const __m256 s = _mm256_set1_ps(inv);
    const __m256i k1 = _mm256_set1_epi32((int)0x9E3779B1u), k2 = _mm256_set1_epi32((int)0x85EBCA6Bu);
    const __m256i low_bits = _mm256_set1_epi32(0x1FFF);
    __m256i idx = _mm256_add_epi32(_mm256_set1_epi32((int)seed), _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7));
    for (; i + 8 <= n; i += 8) {
        const __m256 x = _mm256_mul_ps(_mm256_loadu_ps(src + i), s);
        __m128i half;
        if constexpr (Stochastic) {
            __m256i h = _mm256_mullo_epi32(idx, k1);
            h = _mm256_xor_si256(h, _mm256_srli_epi32(h, 16));
            h = _mm256_mullo_epi32(h, k2);
            h = _mm256_xor_si256(h, _mm256_srli_epi32(h, 13));
            const __m256i bits = _mm256_add_epi32(_mm256_castps_si256(x), _mm256_and_si256(h, low_bits));
            half = _mm256_cvtps_ph(_mm256_castsi256_ps(bits), _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
            idx = _mm256_add_epi32(idx, _mm256_set1_epi32(8));
        } else {
            half = _mm256_cvtps_ph(x, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
        }
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i), half);
    }
#endif
    for (; i < n; i++) {
        float x = src[i] * inv;
        if constexpr (Stochastic) {
            uint32_t bits;
            std::memcpy(&bits, &x, sizeof bits);
            bits = (bits + (hash32(seed + (uint32_t)i) & 0x1FFF)) & ~0x1FFFu; // truncated, so exactly representable
            std::memcpy(&x, &bits, sizeof bits);
        }
        dst[i] = float_to_half(x);
    }
    return scale;
}

// P[0] = 0, P[j + 1] = P[j] + v[j], vectorized so the adds don't form one long dependency chain
void prefix_sum(const float* v, int n, float* P) {
    P[0] = 0.0f;
    int j = 0;
#ifdef __AVX2__
    __m256 carry = _mm256_setzero_ps();
    for (; j + 8 <= n; j += 8) {
        __m256 x = _mm256_loadu_ps(v + j);
        x = _mm256_add_ps(x, _mm256_castsi256_ps(_mm256_slli_si256(_mm256_castps_si256(x), 4)));
        x = _mm256_add_ps(x, _mm256_castsi256_ps(_mm256_slli_si256(_mm256_castps_si256(x), 8)));
        __m256 low = _mm256_permute2f128_ps(x, x, 0x08); // low lane zeroed, high lane = low lane of x
        x = _mm256_add_ps(x, _mm256_shuffle_ps(low, low, _MM_SHUFFLE(3, 3, 3, 3)));
        x = _mm256_add_ps(x, carry);
        _mm256_storeu_ps(P + j + 1, x);
        carry = _mm256_permute2f128_ps(x, x, 0x11);
        carry = _mm256_shuffle_ps(carry, carry, _MM_SHUFFLE(3, 3, 3, 3));
    }
#endif
    for (; j < n; j++) P[j + 1] = P[j] + v[j];
}

template <class T> void write_pod(std::ofstream& f, const T& v) { f.write(reinterpret_cast<const char*>(&v), sizeof(T)); }

template <class T> void read_pod(std::ifstream& f, T& v) {
    f.read(reinterpret_cast<char*>(&v), sizeof(T));
    if (!f) throw std::runtime_error("save file is truncated");
}

} // namespace

Discount Discount::at(int t) {
    // DCFR with alpha = 1.5, beta = 0 (negative regrets halved) and gamma = 3. b-inary's postflop-solver also restarts
    // the average at every power of 4, but measured here that only adds an exploitability spike after each restart
    // (with unbiased fp16 rounding there's no precision reason left for it), plain gamma = 3 is as good or better.
    double ta = std::max(t - 1, 0);
    double pa = ta * std::sqrt(ta);
    double tg = t;
    Discount d;
    d.alpha = float(pa / (pa + 1.0));
    d.beta = 0.5f;
    d.gamma = float(std::pow(tg / (tg + 1.0), 3.0));
    d.iteration = (uint32_t)t;
    return d;
}

Solver::Solver(const GameConfig& config) : config_(config) {
    threads_ = config.threads > 0 ? config.threads : omp_get_max_threads();
    compress_ = config.compress;
    arenas_.resize(threads_);
    init_hands();
    const int num_hands[2] = {hands_[0].size, hands_[1].size};
    build_tree(tree_, config_, num_hands, swap_ok_);
    build_rankings();
    build_orbits();

    std::vector<float> mass(hands_[0].size);
    compatible_mass(0, hands_[1].weight.data(), cards_mask(config_.board), mass.data());
    norm_ = 0.0;
    for (int h = 0; h < hands_[0].size; h++) norm_ += (double)hands_[0].weight[h] * mass[h];
    if (norm_ <= 0.0) throw std::runtime_error("the ranges have no compatible hand pairs");
}

void Solver::init_hands() {
    const uint64_t board = cards_mask(config_.board);
    for (int p = 0; p < 2; p++) {
        HandList& hl = hands_[p];
        hl = HandList{};
        hl.index.fill(-1);
        for (int combo = 0; combo < NUM_COMBOS; combo++) {
            float w = config_.range[p][combo];
            if (w <= 0.0f || (combo_mask(combo) & board)) continue;
            const ComboCards& cc = combo_cards()[combo];
            hl.index[combo] = (int16_t)hl.size++;
            hl.combo.push_back((uint16_t)combo);
            hl.c1.push_back(cc.hi);
            hl.c2.push_back(cc.lo);
            hl.mask.push_back(combo_mask(combo));
            hl.weight.push_back(w);
        }
        if (hl.size == 0) throw std::runtime_error(std::string(p == 0 ? "OOP" : "IP") + " range is empty on this board");
        for (int h = 0; h < hl.size; h++) {
            hl.with_card[hl.c1[h]].push_back((uint16_t)h);
            hl.with_card[hl.c2[h]].push_back((uint16_t)h);
        }
    }
    for (int p = 0; p < 2; p++) {
        HandList& hl = hands_[p];
        hl.same.resize(hl.size);
        for (int h = 0; h < hl.size; h++) hl.same[h] = hands_[p ^ 1].index[hl.combo[h]];
    }

    // suit swaps under which both ranges are symmetric, only those can merge isomorphic chance cards
    for (int s = 0; s < NUM_SUIT_SWAPS; s++) {
        bool ok = true;
        for (int p = 0; p < 2 && ok; p++) {
            HandList& hl = hands_[p];
            hl.perm[s].assign(hl.size, 0);
            for (int h = 0; h < hl.size && ok; h++) {
                int j = hl.index[combo_index(swap_card(hl.c1[h], s), swap_card(hl.c2[h], s))];
                if (j < 0 || std::fabs(hl.weight[j] - hl.weight[h]) > 1e-6f) ok = false;
                else hl.perm[s][h] = (uint16_t)j;
            }
        }
        swap_ok_[s] = ok;
        if (!ok) {
            for (int p = 0; p < 2; p++) hands_[p].perm[s].clear();
        }
    }
}

void Solver::build_rankings() {
    ranking_of_board_.assign(tree_.boards.size(), -1);
    std::vector<uint32_t> rivers;
    for (uint32_t b = 0; b < tree_.boards.size(); b++) {
        if (tree_.boards[b].num_cards == 5) {
            ranking_of_board_[b] = (int32_t)rivers.size();
            rivers.push_back(b);
        }
    }
    const size_t count = rivers.size() * 2;
    ranking_offset_.assign(count, 0);
    ranking_size_.assign(count, 0);
    uint64_t total = 0;
    for (size_t r = 0; r < rivers.size(); r++) {
        const uint64_t board = tree_.boards[rivers[r]].mask;
        for (int p = 0; p < 2; p++) {
            uint32_t n = 0;
            for (int h = 0; h < hands_[p].size; h++) n += (hands_[p].mask[h] & board) == 0;
            ranking_offset_[r * 2 + p] = total;
            ranking_size_[r * 2 + p] = n;
            total += n;
        }
    }
    rankings_.resize(total);
    std::vector<std::vector<ShowdownGroup>> groups(count);

#pragma omp parallel for schedule(dynamic, 8) num_threads(threads_)
    for (int64_t r = 0; r < (int64_t)rivers.size(); r++) {
        const uint64_t board = tree_.boards[rivers[r]].mask;
        for (int p = 0; p < 2; p++) {
            const HandList& hl = hands_[p];
            RankedHand* out = &rankings_[ranking_offset_[r * 2 + p]];
            int k = 0;
            for (int h = 0; h < hl.size; h++) {
                if (hl.mask[h] & board) continue;
                out[k++] = {evaluate_hand(hl.mask[h] | board), (uint16_t)h, hl.c1[h], hl.c2[h]};
            }
            std::sort(out, out + k, [](const RankedHand& a, const RankedHand& b) { return a.strength < b.strength; });
        }
        for (int p = 0; p < 2; p++) {
            const RankedHand* mine = &rankings_[ranking_offset_[r * 2 + p]];
            const RankedHand* theirs = &rankings_[ranking_offset_[r * 2 + (p ^ 1)]];
            const int nm = (int)ranking_size_[r * 2 + p], nt = (int)ranking_size_[r * 2 + (p ^ 1)];
            int i = 0, lt = 0;
            while (i < nm) {
                const uint32_t s = mine[i].strength;
                int end = i;
                while (end < nm && mine[end].strength == s) end++;
                while (lt < nt && theirs[lt].strength < s) lt++;
                int le = lt;
                while (le < nt && theirs[le].strength == s) le++;
                groups[r * 2 + p].push_back({(uint16_t)end, (uint16_t)lt, (uint16_t)le});
                i = end;
            }
        }
    }

    group_offset_.assign(count, 0);
    group_count_.assign(count, 0);
    groups_.clear();
    for (size_t i = 0; i < count; i++) {
        group_offset_[i] = groups_.size();
        group_count_[i] = (uint32_t)groups[i].size();
        groups_.insert(groups_.end(), groups[i].begin(), groups[i].end());
    }
}

// Chance isomorphism is exact only while the reach into an isomorphic chance node is symmetric under the suit swap.
// That holds mathematically, but float noise can push one hand's regret across 0 and not its suit twin's, and regret
// matching is discontinuous there, so the symmetry would slowly drift. Averaging every orbit of hands at chance nodes
// and early folds keeps all nodes above isomorphic chance nodes exactly symmetric: the solver works on the quotient
// game, whose equilibria are equilibria of the full game.
void Solver::build_orbits() {
    orbits_.assign(tree_.symmetries.size() * 2, Orbits{});
    for (size_t sym = 1; sym < tree_.symmetries.size(); sym++) {
        const SuitClasses& classes = tree_.symmetries[sym];
        for (int p = 0; p < 2; p++) {
            const HandList& hl = hands_[p];
            std::vector<int> parent(hl.size);
            for (int h = 0; h < hl.size; h++) parent[h] = h;
            auto find = [&](int x) {
                while (parent[x] != x) x = parent[x] = parent[parent[x]];
                return x;
            };
            for (int suit = 0; suit < 4; suit++) {
                if (classes[suit] == suit) continue;
                const std::vector<uint16_t>& perm = hl.perm[swap_index(classes[suit], suit)];
                for (int h = 0; h < hl.size; h++) parent[find(h)] = find(perm[h]);
            }
            std::vector<std::vector<uint16_t>> groups(hl.size);
            for (int h = 0; h < hl.size; h++) groups[find(h)].push_back((uint16_t)h);
            Orbits& o = orbits_[sym * 2 + p];
            for (const auto& g : groups) {
                if (g.size() < 2) continue;
                o.begin.push_back((uint32_t)o.members.size());
                o.members.insert(o.members.end(), g.begin(), g.end());
            }
            o.begin.push_back((uint32_t)o.members.size());
        }
    }
}

void Solver::symmetrize(uint32_t symmetry, int p, float* values) const {
    if (symmetry == 0) return;
    const Orbits& o = orbits_[symmetry * 2 + p];
    for (size_t i = 0; i + 1 < o.begin.size(); i++) {
        const uint16_t* m = &o.members[o.begin[i]];
        const uint32_t count = o.begin[i + 1] - o.begin[i];
        float sum = 0.0f;
        for (uint32_t k = 0; k < count; k++) sum += values[m[k]];
        sum /= (float)count;
        for (uint32_t k = 0; k < count; k++) values[m[k]] = sum;
    }
}

void Solver::ensure_storage(bool with_regrets) {
    const uint64_t n = tree_.storage_size;
    if (compress_) {
        if (with_regrets && regret16_.size() != n) {
            regret16_.assign(n, 0);
            regret_scale_.assign(tree_.nodes.size(), 1.0f);
        }
        if (cum16_.size() != n) {
            cum16_.assign(n, 0);
            cum_scale_.assign(tree_.nodes.size(), 1.0f);
        }
    } else {
        if (with_regrets && regret_.size() != n) regret_.assign(n, 0.0f);
        if (cum_strategy_.size() != n) cum_strategy_.assign(n, 0.0f);
    }
}

// average strategy [action][hand] of an action node
void Solver::average_into(const Node& n, float* sigma, float* sum) const {
    const int A = n.num_children, N = hands_[n.player].size;
    if (compress_) decode_half(&cum16_[n.offset], cum_scale_[node_id(n)], sigma, (size_t)A * N);
    else std::memcpy(sigma, &cum_strategy_[n.offset], (size_t)A * N * sizeof(float));
    for (int h = 0; h < N; h++) sum[h] = sigma[h];
    for (int a = 1; a < A; a++) {
        const float* s = sigma + (size_t)a * N;
        for (int h = 0; h < N; h++) sum[h] += s[h];
    }
    normalize(sigma, A, N, sum);
}

Arena& Solver::arena() { return arenas_[omp_get_thread_num()].arena; }

// ---------------------------------------------------------------- traversal

template <Solver::Mode M>
void Solver::run(uint32_t root, int p, const float* opp_reach, float* out, const Discount& d) {
    if (threads_ > 1) {
#pragma omp parallel num_threads(threads_)
        {
            set_fast_float_mode();
#pragma omp single
            traverse<M>(root, p, opp_reach, out, d);
        }
    } else {
        set_fast_float_mode();
        traverse<M>(root, p, opp_reach, out, d);
    }
}

template <Solver::Mode M>
void Solver::traverse(uint32_t id, int p, const float* opp_reach, float* out, const Discount& d) {
    const Node& n = tree_.nodes[id];
    switch (n.type) {
        case NodeType::FOLD: fold_node(n, p, opp_reach, out); return;
        case NodeType::SHOWDOWN: showdown_node(n, p, opp_reach, out); return;
        case NodeType::CHANCE: chance_node<M>(n, p, opp_reach, out, d); return;
        case NodeType::ACTION:
            if (n.player == p) own_node<M>(n, p, opp_reach, out, d);
            else opp_node<M>(n, p, opp_reach, out, d);
            return;
    }
}

// the traverser acts: values are the strategy-weighted mix of the children, regrets get updated
template <Solver::Mode M>
void Solver::own_node(const Node& n, int p, const float* opp_reach, float* out, const Discount& d) {
    const int A = n.num_children, N = hands_[p].size;
    const size_t len = (size_t)A * N;
    ArenaScope scope(arena());
    float* cfv = scope.alloc(len);
    int first = 0;
    if (A >= 2) { // facing a river bet: fold and call see the same reach, evaluate both in one pass
        const Node& fold = tree_.nodes[n.first_child];
        const Node& call = tree_.nodes[n.first_child + 1];
        if (fold.type == NodeType::FOLD && call.type == NodeType::SHOWDOWN && fold.offset == 0) {
            showdown_node(call, p, opp_reach, cfv + N, cfv, &fold);
            first = 2;
        }
    }
    for (int a = first; a < A; a++) traverse<M>(n.first_child + a, p, opp_reach, cfv + (size_t)a * N, d);

    if constexpr (M == Mode::BEST_RESPONSE) {
        std::memcpy(out, cfv, N * sizeof(float));
        for (int a = 1; a < A; a++) {
            const float* c = cfv + (size_t)a * N;
            for (int h = 0; h < N; h++) out[h] = std::max(out[h], c[h]);
        }
        return;
    }

    float* sigma = scope.alloc(len);
    float* tmp = scope.alloc(N);
    float* regret = nullptr;
    if constexpr (M == Mode::CFR) {
        regret = compress_ ? scope.alloc(len) : &regret_[n.offset];
        if (compress_) decode_half(&regret16_[n.offset], regret_scale_[node_id(n)], regret, len);
        regret_matching(regret, A, N, sigma, tmp);
    } else {
        average_into(n, sigma, tmp);
    }

    for (int h = 0; h < N; h++) out[h] = sigma[h] * cfv[h];
    for (int a = 1; a < A; a++) {
        const float* s = sigma + (size_t)a * N;
        const float* c = cfv + (size_t)a * N;
        for (int h = 0; h < N; h++) out[h] += s[h] * c[h];
    }

    if constexpr (M == Mode::CFR) {
        const float alpha = d.alpha, beta = d.beta;
        for (int a = 0; a < A; a++) {
            float* r = regret + (size_t)a * N;
            const float* c = cfv + (size_t)a * N;
            for (int h = 0; h < N; h++) {
                float x = r[h];
                x *= x > 0.0f ? alpha : beta;
                r[h] = x + c[h] - out[h];
            }
        }
        if (compress_) regret_scale_[node_id(n)] = encode_half<false>(regret, &regret16_[n.offset], len, seed(n, d, 0));
    }
}

// the opponent acts: its strategy goes into the reach passed down, values of the children just add up,
// the opponent's average strategy is accumulated here, weighted by its own reach
template <Solver::Mode M>
void Solver::opp_node(const Node& n, int p, const float* opp_reach, float* out, const Discount& d) {
    const int q = p ^ 1, A = n.num_children, Nq = hands_[q].size, Np = hands_[p].size;
    const size_t len = (size_t)A * Nq;
    ArenaScope scope(arena());
    float* reach = scope.alloc(len);
    float* tmp = scope.alloc(std::max(Nq, Np));
    if constexpr (M == Mode::CFR) {
        if (compress_) {
            decode_half(&regret16_[n.offset], regret_scale_[node_id(n)], reach, len);
            regret_matching(reach, A, Nq, reach, tmp);
        } else {
            regret_matching(&regret_[n.offset], A, Nq, reach, tmp);
        }
    } else {
        average_into(n, reach, tmp);
    }

    for (int a = 0; a < A; a++) {
        float* r = reach + (size_t)a * Nq;
        for (int h = 0; h < Nq; h++) r[h] *= opp_reach[h];
    }
    if constexpr (M == Mode::CFR) {
        const float gamma = d.gamma;
        float* cum = compress_ ? scope.alloc(len) : &cum_strategy_[n.offset];
        if (compress_) decode_half(&cum16_[n.offset], cum_scale_[node_id(n)], cum, len);
        for (size_t i = 0; i < len; i++) cum[i] = cum[i] * gamma + reach[i];
        if (compress_) cum_scale_[node_id(n)] = encode_half<true>(cum, &cum16_[n.offset], len, seed(n, d, 1));
    }

    bool written = false;
    for (int a = 0; a < A; a++) {
        const float* r = reach + (size_t)a * Nq;
        if (all_zero(r, Nq)) continue; // the opponent never takes this action, nothing below matters
        if (!written) {
            traverse<M>(n.first_child + a, p, r, out, d);
            written = true;
        } else {
            traverse<M>(n.first_child + a, p, r, tmp, d);
            for (int h = 0; h < Np; h++) out[h] += tmp[h];
        }
    }
    if (!written) std::fill(out, out + Np, 0.0f);
}

template <Solver::Mode M>
void Solver::chance_node(const Node& n, int p, const float* opp_reach, float* out, const Discount& d) {
    const int q = p ^ 1, K = n.num_children, Np = hands_[p].size, Nq = hands_[q].size;
    ArenaScope scope(arena());
    float* cfv = scope.alloc((size_t)K * Np);

    auto deal = [&](int k) {
        ArenaScope inner(arena()); // the arena of whichever thread runs this task
        float* dst = cfv + (size_t)k * Np;
        const uint32_t child = n.first_child + k;
        const int card = tree_.meta[child].card;
        float* reach = inner.alloc(Nq);
        std::memcpy(reach, opp_reach, Nq * sizeof(float));
        for (uint16_t h : hands_[q].with_card[card]) reach[h] = 0.0f;
        if (all_zero(reach, Nq)) std::fill(dst, dst + Np, 0.0f);
        else traverse<M>(child, p, reach, dst, d);
    };
    if (threads_ > 1 && K > 1) {
#pragma omp taskloop grainsize(1) default(shared)
        for (int k = 0; k < K; k++) deal(k);
    } else {
        for (int k = 0; k < K; k++) deal(k);
    }

    // summed in a fixed order so results don't depend on thread scheduling
    std::memcpy(out, cfv, Np * sizeof(float));
    for (int k = 1; k < K; k++) {
        const float* c = cfv + (size_t)k * Np;
        for (int h = 0; h < Np; h++) out[h] += c[h];
    }
    // an isomorphic card's values are the representative's with the suits of every hand exchanged
    const ChanceData& cd = tree_.chance[n.offset];
    for (uint32_t i = 0; i < cd.iso_count; i++) {
        const IsoCard& ic = tree_.iso[cd.iso_begin + i];
        const uint16_t* perm = hands_[p].perm[ic.swap].data();
        const float* src = cfv + (size_t)ic.child * Np;
        for (int h = 0; h < Np; h++) out[h] += src[perm[h]];
    }
    // given both hands every one of the remaining cards is equally likely
    const float factor = 1.0f / float(NUM_CARDS - tree_.boards[n.board].num_cards - 4);
    for (int h = 0; h < Np; h++) out[h] *= factor;
    symmetrize(cd.symmetry, p, out);
}

// value of every hand = payoff * (opponent reach not sharing a card with it), via inclusion-exclusion:
// total - reach holding card 1 - reach holding card 2 + reach of the identical combo (subtracted twice)
void Solver::fold_node(const Node& n, int p, const float* opp_reach, float* out) const {
    const HandList& hp = hands_[p];
    const HandList& ho = hands_[p ^ 1];
    float total = 0.0f;
    for (int h = 0; h < ho.size; h++) total += opp_reach[h];
    float card_sum[NUM_CARDS] = {};
    for (int h = 0; h < ho.size; h++) {
        const float v = opp_reach[h];
        card_sum[ho.c1[h]] += v;
        card_sum[ho.c2[h]] += v;
    }
    const float payoff = n.player == p ? -n.payoff : n.payoff;
    const uint64_t board = tree_.boards[n.board].mask;
    for (int h = 0; h < hp.size; h++) {
        float v = total - card_sum[hp.c1[h]] - card_sum[hp.c2[h]];
        const int s = hp.same[h];
        if (s >= 0) v += opp_reach[s];
        out[h] = (hp.mask[h] & board) ? 0.0f : payoff * v;
    }
    symmetrize((uint32_t)n.offset, p, out);
}

// Both rankings are sorted by strength. One ascending sweep over the opponent's ranking keeps per-card reach sums,
// at every tie group of our hands it reads off the compatible reach strictly weaker (W_lt) and not stronger (W_le),
// the totals coming from a prefix sum. With T the compatible total, the stronger reach is T - W_le, so
// value = W_lt - (T - W_le). An identical combo always ties and is subtracted twice and added once in W_le, which
// cancels against the same error in T, so no special case is needed.
void Solver::showdown_node(const Node& n, int p, const float* opp_reach, float* out, float* fold_out, const Node* fold) {
    const int q = p ^ 1;
    const int r = ranking_of_board_[n.board];
    const RankedHand* mine = &rankings_[ranking_offset_[r * 2 + p]];
    const RankedHand* theirs = &rankings_[ranking_offset_[r * 2 + q]];
    const int nm = (int)ranking_size_[r * 2 + p];
    const int nt = (int)ranking_size_[r * 2 + q];
    const ShowdownGroup* groups = &groups_[group_offset_[r * 2 + p]];
    const uint32_t num_groups = group_count_[r * 2 + p];

    ArenaScope scope(arena());
    float* reach = scope.alloc(nt);
    float* prefix = scope.alloc(nt + 1);
    float* partial = scope.alloc(nm);
    for (int j = 0; j < nt; j++) reach[j] = opp_reach[theirs[j].hand];
    prefix_sum(reach, nt, prefix);

    float card_sum[NUM_CARDS] = {};
    int i = 0, j = 0;
    for (uint32_t g = 0; g < num_groups; g++) {
        const ShowdownGroup& grp = groups[g];
        for (; j < grp.lt; j++) {
            card_sum[theirs[j].c1] += reach[j];
            card_sum[theirs[j].c2] += reach[j];
        }
        const float weaker = prefix[grp.lt];
        for (int k = i; k < grp.end; k++) partial[k] = weaker - card_sum[mine[k].c1] - card_sum[mine[k].c2];
        for (; j < grp.le; j++) {
            card_sum[theirs[j].c1] += reach[j];
            card_sum[theirs[j].c2] += reach[j];
        }
        const float not_stronger = prefix[grp.le];
        for (int k = i; k < grp.end; k++) partial[k] += not_stronger - card_sum[mine[k].c1] - card_sum[mine[k].c2];
        i = grp.end;
    }
    for (; j < nt; j++) {
        card_sum[theirs[j].c1] += reach[j];
        card_sum[theirs[j].c2] += reach[j];
    }

    std::fill(out, out + hands_[p].size, 0.0f);
    const float total = prefix[nt];
    const float payoff = n.payoff;
    for (int k = 0; k < nm; k++) {
        const RankedHand& m = mine[k];
        out[m.hand] = payoff * (partial[k] - total + card_sum[m.c1] + card_sum[m.c2]);
    }

    if (fold_out) { // card_sum now covers the whole opponent range, exactly what a fold needs
        const HandList& hp = hands_[p];
        const float fold_payoff = fold->player == p ? -fold->payoff : fold->payoff;
        const uint64_t board = tree_.boards[n.board].mask;
        for (int h = 0; h < hp.size; h++) {
            float v = total - card_sum[hp.c1[h]] - card_sum[hp.c2[h]];
            const int s = hp.same[h];
            if (s >= 0) v += opp_reach[s];
            fold_out[h] = (hp.mask[h] & board) ? 0.0f : fold_payoff * v;
        }
    }
}

// ---------------------------------------------------------------- solving

void Solver::iterate(int t) {
    ensure_storage(true);
    const Discount d = Discount::at(t);
    std::vector<float> root(std::max(hands_[0].size, hands_[1].size));
    for (int p = 0; p < 2; p++) run<Mode::CFR>(0, p, hands_[p ^ 1].weight.data(), root.data(), d);
    iterations_ = t + 1;
}

double Solver::solve(bool verbose) {
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    double pct = std::numeric_limits<double>::infinity();
    for (int t = iterations_; t < config_.iterations; t++) {
        iterate(t);
        const bool last = t + 1 == config_.iterations;
        if ((t + 1) % config_.check_every == 0 || last) {
            const double e = exploitability();
            pct = 100.0 * e / config_.pot;
            if (verbose) {
                const double secs = std::chrono::duration<double>(clock::now() - start).count();
                std::printf("iteration %5d   exploitability %8.4f%% of pot (%.4f chips)   %7.1fs\n", t + 1, pct, e, secs);
                std::fflush(stdout);
            }
            if (pct <= config_.target_exploitability) break;
        }
    }
    return pct;
}

double Solver::exploitability() {
    ensure_storage(false);
    double br[2];
    for (int p = 0; p < 2; p++) {
        std::vector<float> v(hands_[p].size);
        run<Mode::BEST_RESPONSE>(0, p, hands_[p ^ 1].weight.data(), v.data(), Discount{});
        double s = 0.0;
        for (int h = 0; h < hands_[p].size; h++) s += (double)hands_[p].weight[h] * v[h];
        br[p] = s / norm_;
    }
    return (br[0] + br[1]) / 2.0;
}

std::array<double, 2> Solver::expected_values() {
    ensure_storage(false);
    std::array<double, 2> ev{};
    for (int p = 0; p < 2; p++) {
        std::vector<float> v(hands_[p].size);
        run<Mode::AVERAGE>(0, p, hands_[p ^ 1].weight.data(), v.data(), Discount{});
        double s = 0.0;
        for (int h = 0; h < hands_[p].size; h++) s += (double)hands_[p].weight[h] * v[h];
        ev[p] = s / norm_ + config_.pot / 2.0;
    }
    return ev;
}

// ---------------------------------------------------------------- queries

void Solver::average_strategy(uint32_t node, float* out) const {
    const Node& n = tree_.nodes[node];
    if (n.type != NodeType::ACTION) throw std::runtime_error("not an action node");
    const int A = n.num_children, N = hands_[n.player].size;
    if (compress_ ? cum16_.empty() : cum_strategy_.empty()) {
        std::fill(out, out + (size_t)A * N, 1.0f / A);
        return;
    }
    std::vector<float> sum(N);
    average_into(n, out, sum.data());
}

void Solver::node_values(uint32_t node, int p, const float* opp_reach, float* out) {
    ensure_storage(false);
    run<Mode::AVERAGE>(node, p, opp_reach, out, Discount{});
}

void Solver::compatible_mass(int p, const float* opp_reach, uint64_t board, float* out) const {
    const HandList& hp = hands_[p];
    const HandList& ho = hands_[p ^ 1];
    float card_sum[NUM_CARDS] = {};
    float total = 0.0f;
    for (int h = 0; h < ho.size; h++) {
        if (ho.mask[h] & board) continue;
        const float v = opp_reach[h];
        total += v;
        card_sum[ho.c1[h]] += v;
        card_sum[ho.c2[h]] += v;
    }
    for (int h = 0; h < hp.size; h++) {
        if (hp.mask[h] & board) {
            out[h] = 0.0f;
            continue;
        }
        float v = total - card_sum[hp.c1[h]] - card_sum[hp.c2[h]];
        const int s = hp.same[h];
        if (s >= 0) v += opp_reach[s];
        out[h] = v;
    }
}

// ---------------------------------------------------------------- save / load

void Solver::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write '" + path + "'");
    f.write(MAGIC, sizeof MAGIC);
    write_pod(f, (uint64_t)config_.text.size());
    f.write(config_.text.data(), (std::streamsize)config_.text.size());
    write_pod(f, (uint64_t)tree_.storage_size);
    write_pod(f, (int32_t)iterations_);
    std::vector<float> sigma;
    std::vector<uint16_t> q;
    for (uint32_t id = 0; id < tree_.nodes.size(); id++) {
        const Node& n = tree_.nodes[id];
        if (n.type != NodeType::ACTION) continue;
        const size_t len = (size_t)n.num_children * hands_[n.player].size;
        sigma.resize(len);
        q.resize(len);
        average_strategy(id, sigma.data());
        for (size_t i = 0; i < len; i++) q[i] = (uint16_t)std::lround(std::clamp(sigma[i], 0.0f, 1.0f) * 65535.0f);
        f.write(reinterpret_cast<const char*>(q.data()), (std::streamsize)(len * sizeof(uint16_t)));
    }
    if (!f) throw std::runtime_error("failed writing '" + path + "'");
}

std::unique_ptr<Solver> Solver::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open '" + path + "'");
    char magic[sizeof MAGIC];
    f.read(magic, sizeof magic);
    if (!f || std::memcmp(magic, MAGIC, sizeof MAGIC) != 0) throw std::runtime_error("'" + path + "' is not a solver save file");
    uint64_t len = 0;
    read_pod(f, len);
    std::string text(len, '\0');
    f.read(text.data(), (std::streamsize)len);
    auto solver = std::make_unique<Solver>(parse_config(text));
    uint64_t storage = 0;
    int32_t iterations = 0;
    read_pod(f, storage);
    read_pod(f, iterations);
    if (storage != solver->tree_.storage_size) throw std::runtime_error("save file doesn't match the rebuilt tree");
    solver->iterations_ = iterations;
    solver->compress_ = false; // the loaded strategy lives in 32 bit, there are no regrets
    solver->cum_strategy_.assign(storage, 0.0f);
    std::vector<uint16_t> q;
    for (const Node& n : solver->tree_.nodes) {
        if (n.type != NodeType::ACTION) continue;
        const size_t count = (size_t)n.num_children * solver->hands_[n.player].size;
        q.resize(count);
        f.read(reinterpret_cast<char*>(q.data()), (std::streamsize)(count * sizeof(uint16_t)));
        if (!f) throw std::runtime_error("save file is truncated");
        for (size_t i = 0; i < count; i++) solver->cum_strategy_[n.offset + i] = q[i] / 65535.0f;
    }
    return solver;
}
