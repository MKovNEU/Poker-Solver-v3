#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "include/core/cards.h"
#include "include/core/hand_eval.h"
#include "include/game/config.h"
#include "include/preflop/explorer.h"
#include "include/preflop/solver.h"
#include "include/solver/solver.h"

namespace {

int checks = 0, failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        checks++;                                                     \
        if (!(cond)) {                                                \
            failures++;                                               \
            std::printf("  FAIL %s:%d: ", __FILE__, __LINE__);        \
            std::printf(__VA_ARGS__);                                 \
            std::printf("\n");                                        \
        }                                                             \
    } while (0)

uint32_t eval_str(const char* cards) { return evaluate_hand(cards_mask(parse_cards(cards))); }

GameConfig config(const std::string& text) { return parse_config(text + "\nthreads = 4\n"); }

// ----------------------------------------------------------------------------------------------------------------

void test_hand_eval() {
    std::printf("hand evaluator\n");
    CHECK((eval_str("2c3c4c5c7d") >> 20) == 0, "high card");
    CHECK((eval_str("2c2d4c5c7d") >> 20) == 1, "pair");
    CHECK((eval_str("2c2d4c4d7d") >> 20) == 2, "two pair");
    CHECK((eval_str("2c2d2h5c7d") >> 20) == 3, "trips");
    CHECK((eval_str("As2c3d4h5s") >> 20) == 4, "wheel");
    CHECK((eval_str("2c4c6c8cTc") >> 20) == 5, "flush");
    CHECK((eval_str("2c2d2h5c5d") >> 20) == 6, "full house");
    CHECK((eval_str("2c2d2h2s5d") >> 20) == 7, "quads");
    CHECK((eval_str("As2s3s4s5s") >> 20) == 8, "steel wheel");
    CHECK(eval_str("As2c3d4h5s") < eval_str("2c3d4h5s6c"), "wheel is the lowest straight");
    CHECK(eval_str("AsKsQsJsTs") > eval_str("KsQsJsTs9s"), "royal beats king high straight flush");
    CHECK(eval_str("AhAdKcKsQc3d4h") > eval_str("AhAdKcKsJc3d4h"), "two pair kicker");
    CHECK(eval_str("AhAdKcKsQcQd4h") == eval_str("AhAdKcKsQcQd2h"), "third pair only plays as kicker");
    CHECK(eval_str("7h7d7c5s5c3d3h") == eval_str("7h7d7c5s5c2d2h"), "full house ignores the rest");
    CHECK(eval_str("7h7d7c5s5c5d3h") > eval_str("7h7d7c4s4c4d3h"), "two sets: higher second set wins as the pair");

    // compare against the 2+2 lookup table of the old base when it's around
    std::ifstream f("data/hand_ranks.dat", std::ios::binary);
    if (!f) {
        std::printf("  (data/hand_ranks.dat not found, skipping the 2+2 comparison)\n");
        return;
    }
    std::vector<uint32_t> hr(32487834);
    f.read(reinterpret_cast<char*>(hr.data()), hr.size() * sizeof(uint32_t));
    std::mt19937_64 rng(7);
    std::vector<std::pair<uint32_t, uint32_t>> v;
    for (int it = 0; it < 500000; it++) {
        int c[7];
        uint64_t m = 0;
        for (int k = 0; k < 7;) {
            int x = (int)(rng() % 52);
            if (m >> x & 1) continue;
            m |= 1ULL << x;
            c[k++] = x;
        }
        uint32_t p = 53;
        for (int k = 0; k < 7; k++) p = hr[p + c[k] + 1];
        v.push_back({p, evaluate_hand(m)});
    }
    std::sort(v.begin(), v.end());
    int bad = 0;
    for (size_t i = 1; i < v.size(); i++) {
        if ((v[i - 1].first < v[i].first) != (v[i - 1].second < v[i].second)) bad++;
    }
    CHECK(bad == 0, "%d ordering mismatches against the 2+2 table", bad);
}

void test_ranges() {
    std::printf("ranges\n");
    auto count = [](const std::string& r) {
        auto w = parse_range(r);
        double s = 0;
        for (float x : w) s += x;
        return s;
    };
    CHECK(count("AA") == 6, "AA");
    CHECK(count("AKs") == 4, "AKs");
    CHECK(count("AKo") == 12, "AKo");
    CHECK(count("AK") == 16, "AK");
    CHECK(count("22+") == 78, "22+");
    CHECK(count("A2s+") == 48, "A2s+");
    CHECK(count("K9o-K6o") == 48, "K9o-K6o");
    CHECK(count("TT-77") == 24, "TT-77");
    CHECK(std::fabs(count("AhKh:0.5, QQ:0.25") - 2.0) < 1e-6, "weights");
    CHECK(count("AA,AA") == 6, "duplicates don't stack");
    bool threw = false;
    try {
        parse_range("AKx");
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw, "bad entry throws");
}

// ----------------------------------------------------------------------------------------------------------------

// terminal values of the O(n) evaluators against a direct O(n^2) sum
void test_terminals() {
    std::printf("terminal evaluation vs brute force\n");
    Solver s(config("board = Kc 9s 7h 3d 2c\npot = 10\nstack = 100\noop_range = 22+,A2+,K2+,Q2+,J2+,T2+,92+,82+,72+,62+,52+,42+,32\n"
                     "ip_range = 22+,A2s+,K5s+,Q8s+,J8s+,T8s+,98s,ATo+,KTo+,QJo\nriver_bet = 50%\nriver_raise = 3x\n"));
    const Tree& t = s.tree();
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    int tested_fold = 0, tested_sd = 0;
    for (uint32_t id = 0; id < t.nodes.size(); id++) {
        const Node& n = t.nodes[id];
        if (n.type != NodeType::FOLD && n.type != NodeType::SHOWDOWN) continue;
        const uint64_t board = t.boards[n.board].mask;
        for (int p = 0; p < 2; p++) {
            const HandList& hp = s.hands(p);
            const HandList& ho = s.hands(p ^ 1);
            std::vector<float> reach(ho.size), out(hp.size);
            for (float& r : reach) r = uni(rng) < 0.2f ? 0.0f : uni(rng);
            s.node_values(id, p, reach.data(), out.data());
            double scale = 0;
            for (float r : reach) scale += r;
            scale *= n.payoff;
            double worst = 0;
            for (int h = 0; h < hp.size; h++) {
                double expect = 0;
                if (!(hp.mask[h] & board)) {
                    for (int o = 0; o < ho.size; o++) {
                        if (ho.mask[o] & (hp.mask[h] | board)) continue;
                        double sign;
                        if (n.type == NodeType::FOLD) {
                            sign = n.player == p ? -1 : 1;
                        } else {
                            uint32_t a = evaluate_hand(hp.mask[h] | board), b = evaluate_hand(ho.mask[o] | board);
                            sign = a > b ? 1 : a < b ? -1 : 0;
                        }
                        expect += sign * n.payoff * reach[o];
                    }
                }
                worst = std::max(worst, std::fabs(expect - out[h]));
            }
            CHECK(worst < 1e-5 * scale, "%s node %u player %d: max error %g of %g", n.type == NodeType::FOLD ? "fold" : "showdown", id, p,
                  worst, scale);
            (n.type == NodeType::FOLD ? tested_fold : tested_sd)++;
        }
    }
    CHECK(tested_fold > 0 && tested_sd > 0, "found terminals to test");
}

// ----------------------------------------------------------------------------------------------------------------

// independent evaluation of the average strategy profile: recursion over every (oop hand, ip hand) pair,
// enumerating the real chance cards one by one and following isomorphic cards with explicit hand permutations
struct NaiveEval {
    const Solver& s;
    const Tree& t;
    std::vector<std::vector<float>> strategy;

    explicit NaiveEval(const Solver& solver) : s(solver), t(solver.tree()), strategy(solver.tree().nodes.size()) {
        for (uint32_t id = 0; id < t.nodes.size(); id++) {
            const Node& n = t.nodes[id];
            if (n.type != NodeType::ACTION) continue;
            strategy[id].resize((size_t)n.num_children * s.hands(n.player).size);
            s.average_strategy(id, strategy[id].data());
        }
    }

    double value(uint32_t id, int h0, int h1) const { // payoff of OOP
        const Node& n = t.nodes[id];
        const HandList& H0 = s.hands(0);
        const HandList& H1 = s.hands(1);
        const uint64_t board = t.boards[n.board].mask;
        switch (n.type) {
            case NodeType::FOLD: return n.player == 0 ? -n.payoff : n.payoff;
            case NodeType::SHOWDOWN: {
                uint32_t a = evaluate_hand(H0.mask[h0] | board), b = evaluate_hand(H1.mask[h1] | board);
                return a > b ? n.payoff : a < b ? -n.payoff : 0.0;
            }
            case NodeType::ACTION: {
                const int N = s.hands(n.player).size;
                const int h = n.player == 0 ? h0 : h1;
                double v = 0;
                for (int a = 0; a < n.num_children; a++) {
                    const float pr = strategy[id][(size_t)a * N + h];
                    if (pr > 0) v += pr * value(n.first_child + a, h0, h1);
                }
                return v;
            }
            case NodeType::CHANCE: {
                const uint64_t used = board | H0.mask[h0] | H1.mask[h1];
                double v = 0;
                int count = 0;
                for (int c = 0; c < NUM_CARDS; c++) {
                    if (used & card_bit(c)) continue;
                    count++;
                    bool found = false;
                    for (int k = 0; k < n.num_children && !found; k++) {
                        if (t.meta[n.first_child + k].card == c) {
                            v += value(n.first_child + k, h0, h1);
                            found = true;
                        }
                    }
                    const ChanceData& cd = t.chance[n.offset];
                    for (uint32_t i = 0; i < cd.iso_count && !found; i++) {
                        const IsoCard& ic = t.iso[cd.iso_begin + i];
                        if (ic.card == c) {
                            v += value(n.first_child + ic.child, H0.perm[ic.swap][h0], H1.perm[ic.swap][h1]);
                            found = true;
                        }
                    }
                    if (!found) std::printf("  card %s missing below chance node %u\n", card_str(c).c_str(), id);
                }
                return v / count;
            }
        }
        return 0;
    }

    double oop_ev() const {
        const HandList& H0 = s.hands(0);
        const HandList& H1 = s.hands(1);
        double num = 0, den = 0;
        for (int h0 = 0; h0 < H0.size; h0++) {
            for (int h1 = 0; h1 < H1.size; h1++) {
                if (H0.mask[h0] & H1.mask[h1]) continue;
                const double w = (double)H0.weight[h0] * H1.weight[h1];
                num += w * value(0, h0, h1);
                den += w;
            }
        }
        return num / den;
    }
};

void naive_check(const char* name, const std::string& text, int iterations) {
    Solver s(config(text + "\niterations = " + std::to_string(iterations) + "\ntarget_exploitability = 0\ncheck_every = 1000000\n"));
    s.solve(false);
    const double fast = s.expected_values()[0] - s.config().pot / 2.0;
    const double slow = NaiveEval(s).oop_ev();
    std::printf("  %-34s %7zu nodes, %3zu iso cards, OOP EV %.6f vs naive %.6f\n", name, s.tree().nodes.size(), s.tree().iso.size(), fast,
                slow);
    CHECK(std::fabs(fast - slow) < 1e-3 * s.config().pot, "%s: vectorized %.6f vs naive %.6f", name, fast, slow);
}

void test_against_naive() {
    std::printf("vectorized EV vs naive per-hand recursion\n");
    const std::string small = "oop_range = AA,KK,AKs,QJs,T9s\nip_range = QQ,JJ,AQs,KQs,98s\n";
    const std::string sizes = "flop_bet = 50%\nturn_bet = 50%\nriver_bet = 50%\nmax_raises = 0\n";
    naive_check("river", "board = Kc 9s 7h 3d 2c\npot = 10\nstack = 50\n" + small + sizes + "river_raise = 3x\nmax_raises = 2\n", 30);
    naive_check("turn, 4-flush board (isomorphic)", "board = Ks 9s 5s 2s\npot = 10\nstack = 50\n" + small + sizes, 30);
    naive_check("turn, rainbow", "board = Kc 9s 5h 2d\npot = 10\nstack = 50\n" + small + sizes, 30);
    naive_check("flop, monotone, all-in runouts", "board = 7s 5s 2s\npot = 10\nstack = 15\n" + small + sizes, 20);
    naive_check("flop, paired (isomorphic)", "board = 8c 8d 3h\npot = 10\nstack = 40\n" + small + sizes, 20);
    naive_check("flop, paired, 32 bit storage", "board = 8c 8d 3h\npot = 10\nstack = 40\ncompress = false\n" + small + sizes, 20);
}

// isomorphism is exact: after one iteration (before the float noise sensitivity of regret matching can make the two
// trajectories diverge) both solves must agree to float precision,
// and once converged both have to describe the same game value
void test_isomorphism() {
    std::printf("isomorphism on vs off\n");
    const std::string text = "board = Qs 8s 3s\npot = 10\nstack = 40\noop_range = 22+,AT+,KT+,QT+,JT,T9s,98s,87s,76s,65s\n"
                             "ip_range = 22+,A2s+,KTs+,QTs+,JTs,AJo+\nflop_bet = 50%\nturn_bet = 75%\nriver_bet = 75%\n"
                             "flop_raise = 3x\nmax_raises = 1\ntarget_exploitability = 0\n";
    for (int iterations : {1, 300}) {
        const std::string extra = "iterations = " + std::to_string(iterations) + "\ncheck_every = " + std::to_string(iterations) + "\n";
        Solver a(config(text + extra));
        Solver b(config(text + extra + "isomorphism = false\n"));
        a.solve(false);
        b.solve(false);
        const auto ea = a.expected_values(), eb = b.expected_values();
        const double xa = a.exploitability(), xb = b.exploitability();
        std::printf("  %3d iterations: nodes %zu vs %zu, OOP EV %.6f vs %.6f, exploitability %.6f vs %.6f\n", iterations,
                    a.tree().nodes.size(), b.tree().nodes.size(), ea[0], eb[0], xa, xb);
        CHECK(a.tree().nodes.size() < b.tree().nodes.size() / 2, "isomorphism should shrink a monotone flop tree a lot");
        if (iterations == 1) {
            CHECK(std::fabs(ea[0] - eb[0]) < 1e-4, "EV mismatch");
            CHECK(std::fabs(xa - xb) < 1e-4, "exploitability mismatch");
        } else {
            CHECK(std::fabs(ea[0] - eb[0]) <= xa + xb, "EVs differ by more than the exploitability allows");
        }
    }
}

// polarized range vs bluff catchers on a river, the equilibrium is known in closed form:
// pot sized bet -> bluffs are 1/3 of the betting range, the bluff catcher calls 1/2
void test_toy_game() {
    std::printf("toy river game vs closed form\n");
    Solver s(config("board = Kc 9s 7h 3d 2c\npot = 10\nstack = 100\noop_range = 99,65\nip_range = AK\n"
                     "oop_river_bet = 100%\nmax_raises = 0\nadd_allin_threshold = 0\niterations = 3000\n"
                     "target_exploitability = 0.01\n"));
    s.solve(false);
    const Tree& t = s.tree();
    const Node& root = t.nodes[0];
    std::vector<float> sigma((size_t)root.num_children * s.hands(0).size);
    s.average_strategy(0, sigma.data());
    const HandList& oop = s.hands(0);
    double bet_value = 0, bet_bluff = 0, n_value = 0, n_bluff = 0;
    for (int h = 0; h < oop.size; h++) {
        const bool value = card_rank(oop.c1[h]) == 7; // 99
        (value ? bet_value : bet_bluff) += sigma[oop.size + h];
        (value ? n_value : n_bluff) += 1;
    }
    const Node& facing = t.nodes[root.first_child + 1];
    std::vector<float> call((size_t)facing.num_children * s.hands(1).size);
    s.average_strategy(root.first_child + 1, call.data());
    double call_freq = 0;
    for (int h = 0; h < s.hands(1).size; h++) call_freq += call[s.hands(1).size + h] / s.hands(1).size;
    const double value_freq = bet_value / n_value, bluff_combos = bet_bluff;
    std::printf("  OOP bets 99 %.1f%%, bluff combos %.3f (theory %.3f), IP calls %.1f%% (theory 50%%), exploitability %.4f%%\n",
                100 * value_freq, bluff_combos, n_value / 2, 100 * call_freq, 100 * s.exploitability() / 10);
    CHECK(value_freq > 0.99, "value always bets");
    CHECK(std::fabs(bluff_combos - n_value / 2) < 0.05, "bluff combos");
    CHECK(std::fabs(call_freq - 0.5) < 0.02, "call frequency");
}

void test_convergence() {
    std::printf("convergence on a turn spot, 32 and 16 bit storage\n");
    for (const char* compress : {"false", "true"}) {
        Solver s(config(std::string("board = Qs Jh 2c 8d\npot = 20\nstack = 80\n"
                                    "oop_range = 22+,A2s+,K9s+,Q9s+,J9s+,T8s+,97s+,86s+,75s+,64s+,54s,ATo+,KJo+,QJo\n"
                                    "ip_range = 22+,A2s+,K5s+,Q8s+,J8s+,T8s+,97s+,86s+,76s,65s,A9o+,KTo+,QTo+,JTo\n"
                                    "turn_bet = 50%, 100%\nturn_raise = 3x\nriver_bet = 50%, 100%\nriver_raise = 3x\nmax_raises = 2\n"
                                    "iterations = 400\ntarget_exploitability = 0.1\ncheck_every = 50\ncompress = ") +
                        compress + "\n"));
        const auto t0 = std::chrono::steady_clock::now();
        const double pct = s.solve(false);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  compress %-5s %zu nodes, %d iterations, exploitability %.4f%% of pot, %.2fs\n", compress, s.tree().nodes.size(),
                    s.iterations_done(), pct, secs);
        CHECK(pct < 0.1, "should reach 0.1%% of pot");
    }
}

void test_save_load() {
    std::printf("save / load\n");
    Solver s(config("board = Kc 9s 7h 3d\npot = 10\nstack = 50\noop_range = AA,KK,AKs,QJs,T9s\nip_range = QQ,JJ,AQs,KQs,98s\n"
                     "turn_bet = 50%\nriver_bet = 50%\niterations = 50\ntarget_exploitability = 0\ncheck_every = 50\n"));
    s.solve(false);
    const std::string path = "tests/tmp_save.sol";
    s.save(path);
    auto l = Solver::load(path);
    std::remove(path.c_str());
    double worst = 0;
    for (uint32_t id = 0; id < s.tree().nodes.size(); id++) {
        const Node& n = s.tree().nodes[id];
        if (n.type != NodeType::ACTION) continue;
        const size_t len = (size_t)n.num_children * s.hands(n.player).size;
        std::vector<float> a(len), b(len);
        s.average_strategy(id, a.data());
        l->average_strategy(id, b.data());
        for (size_t i = 0; i < len; i++) worst = std::max(worst, (double)std::fabs(a[i] - b[i]));
    }
    CHECK(worst < 1e-4, "strategy changed by %g", worst);
}

// ---------------------------------------------------------------------------------------------------------- preflop

// exact equity of one matchup by running every board
double exact_equity(int a, int b) {
    const uint64_t dead = combo_mask(a) | combo_mask(b);
    std::vector<int> deck;
    for (int c = 0; c < NUM_CARDS; c++)
        if (!(dead & card_bit(c))) deck.push_back(c);
    double won = 0, total = 0;
    const int n = (int)deck.size();
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            for (int k = j + 1; k < n; k++)
                for (int l = k + 1; l < n; l++)
                    for (int m = l + 1; m < n; m++) {
                        const uint64_t board = card_bit(deck[i]) | card_bit(deck[j]) | card_bit(deck[k]) | card_bit(deck[l]) | card_bit(deck[m]);
                        const uint32_t x = evaluate_hand(board | combo_mask(a)), y = evaluate_hand(board | combo_mask(b));
                        won += x > y ? 1.0 : x == y ? 0.5 : 0.0;
                        total += 1;
                    }
    return won / total;
}

int combo_of(const char* s) { return combo_index(parse_card(std::string(s, 2)), parse_card(std::string(s + 2, 2))); }

// a table from random boards is enough for the solver tests, the cached exact one is checked when it exists
const PreflopEquity& test_equity() {
    static const PreflopEquity table = PreflopEquity::compute(4, 40000, false);
    return table;
}

PreflopConfig preflop_config(const std::string& text) { return parse_preflop_config(text + "\nthreads = 4\n"); }

void test_preflop_equity() {
    std::printf("preflop equity table\n");
    const PreflopEquity& sampled = test_equity();
    PreflopEquity exact;
    const bool cached = PreflopEquity::load("data/preflop_equity.bin", exact);
    for (const auto& [a, b] : std::vector<std::pair<const char*, const char*>>{{"AsAh", "KdKc"}, {"AhKh", "QsQd"}, {"7c2d", "2s2h"}, {"AsKs", "AhKh"}}) {
        const int x = combo_of(a), y = combo_of(b);
        const double e = exact_equity(x, y);
        std::printf("  %s vs %s: exact %.4f, sampled %.4f%s\n", a, b, e, sampled(x, y),
                    cached ? (", cached " + std::to_string(exact(x, y)).substr(0, 6)).c_str() : "");
        CHECK(std::fabs(sampled(x, y) - e) < 0.01, "sampled equity %s vs %s", a, b);
        CHECK(std::fabs(sampled(x, y) + sampled(y, x) - 1.0) < 1e-6, "equities add up to 1");
        if (cached) CHECK(std::fabs(exact(x, y) - e) < 1e-6, "cached table %s vs %s", a, b);
    }
}

// EV of seat p in a heads-up preflop tree by walking it for every ordered pair of hands, no class matrices
double naive_preflop_ev(const PreflopSolver& s, const PreflopEquity& eq, int p) {
    const PreflopTree& t = s.tree();
    std::vector<std::vector<float>> strat(t.nodes.size());
    for (uint32_t id = 0; id < t.nodes.size(); id++) {
        if (t.nodes[id].type != PNodeType::ACTION) continue;
        strat[id].resize((size_t)t.nodes[id].num_children * NUM_CLASSES);
        s.average_strategy(id, strat[id].data());
    }
    const std::array<float, NUM_CLASSES> play = playability_factors(s.config().playability);
    int hand[2];
    std::function<double(uint32_t)> walk = [&](uint32_t id) -> double {
        const PNode& n = t.nodes[id];
        if (n.type == PNodeType::ACTION) {
            double v = 0;
            const int cls = combo_class(hand[n.player]);
            for (int a = 0; a < n.num_children; a++) {
                const float pr = strat[id][(size_t)a * NUM_CLASSES + cls];
                if (pr > 0) v += pr * walk(n.first_child + a);
            }
            return v;
        }
        const PTerminal& term = t.terminals[n.data];
        if (n.type == PNodeType::FOLD) return (n.player == p ? term.pot : 0.0) - term.invested[p];
        const double e = eq(hand[p], hand[p ^ 1]);
        const double mine = preflop_realization(s.config(), play, combo_class(hand[p]), p == term.oop, term.depth);
        const double theirs = preflop_realization(s.config(), play, combo_class(hand[p ^ 1]), p != term.oop, term.depth);
        return term.pot * e * mine / (e * mine + (1 - e) * theirs) - term.invested[p];
    };
    double total = 0, pairs = 0;
    for (int a = 0; a < NUM_COMBOS; a++) {
        for (int b = 0; b < NUM_COMBOS; b++) {
            if (combo_mask(a) & combo_mask(b)) continue;
            hand[0] = a;
            hand[1] = b;
            total += walk(0);
            pairs += 1;
        }
    }
    return total / pairs;
}

void test_preflop_vs_naive() {
    std::printf("preflop class vectors vs a naive walk over every hand pair\n");
    for (const char* extra : {"stack = 100\n", "stack = 25\nrealization = 0.6\n"}) {
        PreflopSolver s(preflop_config(std::string("players = 2\niterations = 40\ncheck_every = 1000\n") + extra), test_equity());
        s.solve(false);
        const std::vector<double> ev = s.expected_values();
        for (int p = 0; p < 2; p++) {
            const double naive = naive_preflop_ev(s, test_equity(), p);
            std::printf("  %-26s seat %d: EV %.6f vs naive %.6f\n", std::string(extra).substr(0, std::string(extra).find('\n')).c_str(), p, ev[p], naive);
            CHECK(std::fabs(ev[p] - naive) < 2e-5, "preflop EV mismatch");
        }
        CHECK(std::fabs(ev[0] + ev[1]) < 1e-3, "heads-up EVs are zero-sum");
    }
}

void test_pushfold() {
    std::printf("heads-up push / fold at 10bb\n");
    PreflopSolver s(preflop_config("players = 2\nstack = 10\nlimp = none\nallin_raise = 1\niterations = 3000\ntarget = 0.02\ncheck_every = 50\n"),
                    test_equity());
    const double e = s.solve(false);
    PreflopExplorer x(s);
    const PreflopTree& t = s.tree();
    std::vector<float> push(2 * NUM_CLASSES), call(2 * NUM_CLASSES);
    s.average_strategy(0, push.data());
    s.average_strategy(t.nodes[0].first_child + 1, call.data());
    double pushed = 0, called = 0;
    for (int i = 0; i < NUM_CLASSES; i++) {
        pushed += class_combos(i) * push[NUM_CLASSES + i] / NUM_COMBOS;
        called += class_combos(i) * call[NUM_CLASSES + i] / NUM_COMBOS;
    }
    const int AA = 0, K7o = 7 * 13 + 1, T2s = 4 * 13 + 12, s32o = 12 * 13 + 11, s72o = 12 * 13 + 7;
    std::printf("  %d iterations, exploitability %.4f mbb/hand, SB shoves %.1f%%, BB calls %.1f%%\n", s.iterations_done(), e,
                100 * pushed, 100 * called);
    std::printf("  shove AA %.2f K7o %.2f T2s %.2f 32o %.2f   call AA %.2f K7o %.2f 72o %.2f\n", push[NUM_CLASSES + AA],
                push[NUM_CLASSES + K7o], push[NUM_CLASSES + T2s], push[NUM_CLASSES + s32o], call[NUM_CLASSES + AA],
                call[NUM_CLASSES + K7o], call[NUM_CLASSES + s72o]);
    CHECK(e < 0.05, "push / fold should converge");
    CHECK(push[NUM_CLASSES + AA] > 0.999 && call[NUM_CLASSES + AA] > 0.999, "aces shove and call");
    CHECK(push[NUM_CLASSES + s32o] < 0.01 && call[NUM_CLASSES + s72o] < 0.01, "32o folds, 72o doesn't call");
    CHECK(pushed > 0.5 && pushed < 0.65 && called > 0.3 && called < 0.45, "shove / call frequencies");
}

void test_preflop_6max() {
    std::printf("6-max preflop\n");
    PreflopSolver s(preflop_config("players = 6\niterations = 300\ntarget = 0\ncheck_every = 100\n"), test_equity());
    const double e0 = s.exploitability();
    const double e = s.solve(false);
    double worst = 0;
    for (uint32_t id = 0; id < s.tree().nodes.size(); id++) {
        const PNode& n = s.tree().nodes[id];
        if (n.type != PNodeType::ACTION) continue;
        std::vector<float> sigma((size_t)n.num_children * NUM_CLASSES);
        s.average_strategy(id, sigma.data());
        for (int i = 0; i < NUM_CLASSES; i++) {
            double sum = 0;
            for (int a = 0; a < n.num_children; a++) sum += sigma[(size_t)a * NUM_CLASSES + i];
            worst = std::max(worst, std::fabs(sum - 1.0));
        }
    }
    double ev_sum = 0;
    for (double v : s.expected_values()) ev_sum += v;
    std::printf("  %zu nodes, exploitability %.1f -> %.2f mbb/hand after %d iterations, EVs sum to %.4f bb\n", s.tree().nodes.size(),
                e0, e, s.iterations_done(), ev_sum);
    CHECK(worst < 1e-4, "strategies sum to 1");
    CHECK(e < e0 / 20, "exploitability should drop");
    // each seat sees exact card removal against every opponent but treats the opponents as independent of each
    // other, so with 3+ seats the EVs only nearly add up (heads-up they do exactly, see above)
    CHECK(std::fabs(ev_sum) < 0.1, "EVs nearly zero-sum");

    // a heads-up flop line exports a postflop config
    PreflopExplorer x(s);
    std::string err;
    for (const char* step : {"fold", "fold", "fold", "raise", "fold", "call"}) CHECK(x.take(step, err), "%s", err.c_str());
    const std::string cfg = x.postflop_config();
    std::printf("  BTN open, BB call -> %s", cfg.substr(cfg.find("pot")).c_str());
    CHECK(cfg.find("pot = 5.5") != std::string::npos && cfg.find("stack = 97.5") != std::string::npos, "pot and stack");
    for (const char* key : {"oop_range = ", "ip_range = "}) {
        const size_t at = cfg.find(key) + std::string(key).size();
        const auto weights = parse_range(cfg.substr(at, cfg.find('\n', at) - at));
        double combos = 0;
        for (float w : weights) combos += w;
        CHECK(combos > 200 && combos < 900, "%s%.0f combos", key, combos);
    }
}

void test_preflop_save_load() {
    std::printf("preflop save / load\n");
    PreflopSolver s(preflop_config("players = 3\niterations = 60\ntarget = 0\ncheck_every = 1000\n"), test_equity());
    s.solve(false);
    const std::string path = "tests/tmp_preflop.sol";
    s.save(path);
    auto l = PreflopSolver::load(path, test_equity());
    std::remove(path.c_str());
    double worst = 0;
    for (uint32_t id = 0; id < s.tree().nodes.size(); id++) {
        const PNode& n = s.tree().nodes[id];
        if (n.type != PNodeType::ACTION) continue;
        std::vector<float> a((size_t)n.num_children * NUM_CLASSES), b(a.size());
        s.average_strategy(id, a.data());
        l->average_strategy(id, b.data());
        for (size_t i = 0; i < a.size(); i++) worst = std::max(worst, (double)std::fabs(a[i] - b[i]));
    }
    CHECK(worst < 1e-4, "strategy changed by %g", worst);
}

} // namespace

int main() {
    test_hand_eval();
    test_ranges();
    test_terminals();
    test_against_naive();
    test_isomorphism();
    test_toy_game();
    test_convergence();
    test_save_load();
    test_preflop_equity();
    test_preflop_vs_naive();
    test_pushfold();
    test_preflop_6max();
    test_preflop_save_load();
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
