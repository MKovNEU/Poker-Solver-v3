#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// card = rank * 4 + suit, rank 0..12 = 2..A, suit 0..3 = c, d, h, s
constexpr int NUM_CARDS = 52;
constexpr int NUM_COMBOS = 1326;
constexpr int NUM_SUIT_SWAPS = 6; // transpositions of two suits

inline int card_rank(int card) { return card >> 2; }
inline int card_suit(int card) { return card & 3; }
inline uint64_t card_bit(int card) { return 1ULL << card; }

// combo index of two different cards, hi * (hi - 1) / 2 + lo (same layout as the old base)
inline int combo_index(int a, int b) {
    int hi = a > b ? a : b;
    int lo = a > b ? b : a;
    return hi * (hi - 1) / 2 + lo;
}

struct ComboCards {
    uint8_t hi, lo; // hi > lo
};

const std::array<ComboCards, NUM_COMBOS>& combo_cards();

inline uint64_t combo_mask(int combo) {
    const ComboCards& cc = combo_cards()[combo];
    return card_bit(cc.hi) | card_bit(cc.lo);
}

// suit swaps, swap_index(a, b) is the transposition exchanging suits a and b
int swap_index(int a, int b);
int swap_suit(int suit, int swap);
int swap_card(int card, int swap);

int parse_card(const std::string& s); // -1 if invalid
std::string card_str(int card);
std::vector<int> parse_cards(const std::string& text); // "Qs Jh 2c" or "QsJh2c", throws std::runtime_error
std::string combo_str(int combo);
std::string combo_str(int c1, int c2);
uint64_t cards_mask(const std::vector<int>& cards);

// range in PioSolver syntax ("AA,KK:0.5,AKs,A5s-A2s,QJo+,AhKh") -> weight per combo, throws std::runtime_error
std::array<float, NUM_COMBOS> parse_range(const std::string& text);

// 13x13 grid: row/col 0 = ace, pairs on the diagonal, suited above it, offsuit below
void combo_grid_pos(int combo, int& row, int& col);
std::string grid_label(int row, int col);
