#include "include/preflop/equity.h"

#include <omp.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <unordered_map>

#include "include/core/hand_eval.h"

// Matchups are reduced to one representative per suit permutation (47k of the 812k hand pairs). Instead of running
// each matchup over its own boards, every board is visited once: all hands are evaluated on it and every
// representative matchup that doesn't touch the board takes a win / tie count from the two strengths.

namespace {

const char MAGIC[8] = {'P', 'F', 'E', 'Q', 'U', 'I', 'T', '1'};
constexpr double BOARDS_PER_MATCHUP = 1712304.0; // C(48, 5)

int permute_combo(int combo, const std::array<int, 4>& perm) {
    const ComboCards& cc = combo_cards()[combo];
    return combo_index(card_rank(cc.hi) * 4 + perm[card_suit(cc.hi)], card_rank(cc.lo) * 4 + perm[card_suit(cc.lo)]);
}

} // namespace

PreflopEquity PreflopEquity::compute(int threads, int boards, bool verbose) {
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();

    std::vector<std::array<int, 4>> perms;
    std::array<int, 4> perm = {0, 1, 2, 3};
    do perms.push_back(perm);
    while (std::next_permutation(perm.begin(), perm.end()));

    // representative of every unordered pair a < b: link = matchup index, top bit set when a is the second hand
    std::vector<uint32_t> link((size_t)NUM_COMBOS * NUM_COMBOS, UINT32_MAX);
    std::unordered_map<uint32_t, uint32_t> index_of;
    std::vector<uint16_t> first, second;
    std::vector<uint64_t> mask;
    for (int a = 0; a < NUM_COMBOS; a++) {
        for (int b = a + 1; b < NUM_COMBOS; b++) {
            if (combo_mask(a) & combo_mask(b)) continue;
            uint32_t best = UINT32_MAX;
            bool flipped = false;
            for (const auto& p : perms) {
                const int pa = permute_combo(a, p), pb = permute_combo(b, p);
                const uint32_t key = (uint32_t)std::min(pa, pb) * NUM_COMBOS + (uint32_t)std::max(pa, pb);
                if (key < best) {
                    best = key;
                    flipped = pa > pb;
                }
            }
            auto it = index_of.find(best);
            if (it == index_of.end()) {
                it = index_of.emplace(best, (uint32_t)first.size()).first;
                first.push_back((uint16_t)(best / NUM_COMBOS));
                second.push_back((uint16_t)(best % NUM_COMBOS));
                mask.push_back(combo_mask(best / NUM_COMBOS) | combo_mask(best % NUM_COMBOS));
            }
            link[(size_t)a * NUM_COMBOS + b] = it->second | (flipped ? 0x80000000u : 0u);
        }
    }
    const int U = (int)first.size();

    std::vector<uint64_t> board_list;
    if (boards <= 0) {
        for (int c1 = 0; c1 < 52; c1++)
            for (int c2 = c1 + 1; c2 < 52; c2++)
                for (int c3 = c2 + 1; c3 < 52; c3++)
                    for (int c4 = c3 + 1; c4 < 52; c4++)
                        for (int c5 = c4 + 1; c5 < 52; c5++)
                            board_list.push_back(card_bit(c1) | card_bit(c2) | card_bit(c3) | card_bit(c4) | card_bit(c5));
    } else {
        std::mt19937_64 rng(12345);
        while ((int)board_list.size() < boards) {
            uint64_t m = 0;
            while (std::popcount(m) < 5) m |= card_bit((int)(rng() % 52));
            board_list.push_back(m);
        }
    }
    if (verbose) {
        std::printf("preflop equities: %d matchups over %zu boards, %d threads...\n", U, board_list.size(), threads);
        std::fflush(stdout);
    }

    std::vector<uint32_t> wins(U, 0), ties(U, 0), seen(U, 0);
#pragma omp parallel num_threads(threads)
    {
        std::vector<uint32_t> w(U, 0), t(U, 0), n(U, 0);
        std::vector<uint32_t> strength(NUM_COMBOS, 0);
#pragma omp for schedule(dynamic, 512)
        for (int64_t k = 0; k < (int64_t)board_list.size(); k++) {
            const uint64_t board = board_list[k];
            for (int c = 0; c < NUM_COMBOS; c++) {
                const uint64_t m = combo_mask(c);
                if (!(m & board)) strength[c] = evaluate_hand(m | board);
            }
            for (int u = 0; u < U; u++) { // branch free: about half the matchups touch the board, unpredictably
                const uint32_t ok = (mask[u] & board) == 0;
                const uint32_t x = strength[first[u]], y = strength[second[u]];
                w[u] += ok & (x > y);
                t[u] += ok & (x == y);
                n[u] += ok;
            }
        }
#pragma omp critical
        for (int u = 0; u < U; u++) {
            wins[u] += w[u];
            ties[u] += t[u];
            seen[u] += n[u];
        }
    }

    PreflopEquity table;
    table.eq_.assign((size_t)NUM_COMBOS * NUM_COMBOS, 0.0f);
    for (int a = 0; a < NUM_COMBOS; a++) {
        for (int b = a + 1; b < NUM_COMBOS; b++) {
            const uint32_t l = link[(size_t)a * NUM_COMBOS + b];
            if (l == UINT32_MAX) continue;
            const uint32_t u = l & 0x7FFFFFFFu;
            const double total = boards <= 0 ? BOARDS_PER_MATCHUP : std::max<double>(seen[u], 1.0);
            double e = (wins[u] + 0.5 * ties[u]) / total;
            if (l & 0x80000000u) e = 1.0 - e;
            table.eq_[(size_t)a * NUM_COMBOS + b] = (float)e;
            table.eq_[(size_t)b * NUM_COMBOS + a] = (float)(1.0 - e);
        }
    }
    if (verbose) {
        std::printf("preflop equities done in %.1fs\n", std::chrono::duration<double>(clock::now() - start).count());
        std::fflush(stdout);
    }
    return table;
}

void PreflopEquity::save(const std::string& path) const {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write '" + path + "'");
    f.write(MAGIC, sizeof MAGIC);
    f.write(reinterpret_cast<const char*>(eq_.data()), (std::streamsize)(eq_.size() * sizeof(float)));
    if (!f) throw std::runtime_error("failed writing '" + path + "'");
}

bool PreflopEquity::load(const std::string& path, PreflopEquity& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[sizeof MAGIC];
    f.read(magic, sizeof magic);
    if (!f || std::memcmp(magic, MAGIC, sizeof MAGIC) != 0) return false;
    out.eq_.resize((size_t)NUM_COMBOS * NUM_COMBOS);
    f.read(reinterpret_cast<char*>(out.eq_.data()), (std::streamsize)(out.eq_.size() * sizeof(float)));
    return (bool)f;
}

PreflopEquity PreflopEquity::load_or_compute(const std::string& path, int threads, bool verbose) {
    PreflopEquity table;
    if (load(path, table)) return table;
    table = compute(threads, 0, verbose);
    table.save(path);
    if (verbose) std::printf("saved %s\n", path.c_str());
    return table;
}
