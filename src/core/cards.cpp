#include "include/core/cards.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace {

const char RANK_CHARS[] = "23456789TJQKA";
const char SUIT_CHARS[] = "cdhs";
const int SWAP_A[NUM_SUIT_SWAPS] = {0, 0, 0, 1, 1, 2};
const int SWAP_B[NUM_SUIT_SWAPS] = {1, 2, 3, 2, 3, 3};

int parse_rank(char ch) {
    ch = (char)std::toupper((unsigned char)ch);
    for (int r = 0; r < 13; r++) {
        if (RANK_CHARS[r] == ch) return r;
    }
    return -1;
}

int parse_suit(char ch) {
    ch = (char)std::tolower((unsigned char)ch);
    for (int s = 0; s < 4; s++) {
        if (SUIT_CHARS[s] == ch) return s;
    }
    return -1;
}

std::array<ComboCards, NUM_COMBOS> make_combo_cards() {
    std::array<ComboCards, NUM_COMBOS> table{};
    for (int hi = 1; hi < NUM_CARDS; hi++) {
        for (int lo = 0; lo < hi; lo++) table[combo_index(hi, lo)] = {(uint8_t)hi, (uint8_t)lo};
    }
    return table;
}

enum class ClassType { PAIR, SUITED, OFFSUIT, ANY };

struct HandClass {
    int hi, lo;
    ClassType type;
};

// "AA", "AK", "AKs", "AKo"
bool parse_class(const std::string& s, HandClass& out) {
    if (s.size() < 2 || s.size() > 3) return false;
    int a = parse_rank(s[0]);
    int b = parse_rank(s[1]);
    if (a < 0 || b < 0) return false;
    if (a == b) {
        if (s.size() == 3) return false;
        out = {a, a, ClassType::PAIR};
        return true;
    }
    if (a < b) std::swap(a, b);
    ClassType type = ClassType::ANY;
    if (s.size() == 3) {
        char k = (char)std::tolower((unsigned char)s[2]);
        if (k == 's') type = ClassType::SUITED;
        else if (k == 'o') type = ClassType::OFFSUIT;
        else return false;
    }
    out = {a, b, type};
    return true;
}

void set_class(std::array<float, NUM_COMBOS>& weights, const HandClass& hc, float w) {
    for (int s1 = 0; s1 < 4; s1++) {
        for (int s2 = 0; s2 < 4; s2++) {
            if (hc.type == ClassType::PAIR && s1 >= s2) continue;
            if (hc.type == ClassType::SUITED && s1 != s2) continue;
            if (hc.type == ClassType::OFFSUIT && s1 == s2) continue;
            weights[combo_index(hc.hi * 4 + s1, hc.lo * 4 + s2)] = w;
        }
    }
}

void apply_spec(std::array<float, NUM_COMBOS>& weights, const std::string& spec, float w) {
    auto fail = [&]() { throw std::runtime_error("bad range entry '" + spec + "'"); };

    // exact combo, e.g. AhKh
    if (spec.size() == 4) {
        int c1 = parse_card(spec.substr(0, 2));
        int c2 = parse_card(spec.substr(2, 2));
        if (c1 >= 0 && c2 >= 0) {
            if (c1 == c2) fail();
            weights[combo_index(c1, c2)] = w;
            return;
        }
    }

    size_t dash = spec.find('-');
    if (dash != std::string::npos) { // TT-77, A9s-A5s
        HandClass a, b;
        if (!parse_class(spec.substr(0, dash), a) || !parse_class(spec.substr(dash + 1), b) || a.type != b.type) fail();
        if (a.type == ClassType::PAIR) {
            for (int r = std::min(a.hi, b.hi); r <= std::max(a.hi, b.hi); r++) set_class(weights, {r, r, a.type}, w);
        } else {
            if (a.hi != b.hi) fail();
            for (int r = std::min(a.lo, b.lo); r <= std::max(a.lo, b.lo); r++) set_class(weights, {a.hi, r, a.type}, w);
        }
        return;
    }

    if (spec.back() == '+') { // 77+, A9s+
        HandClass a;
        if (!parse_class(spec.substr(0, spec.size() - 1), a)) fail();
        if (a.type == ClassType::PAIR) {
            for (int r = a.hi; r < 13; r++) set_class(weights, {r, r, a.type}, w);
        } else {
            for (int r = a.lo; r < a.hi; r++) set_class(weights, {a.hi, r, a.type}, w);
        }
        return;
    }

    HandClass a;
    if (!parse_class(spec, a)) fail();
    set_class(weights, a, w);
}

} // namespace

const std::array<ComboCards, NUM_COMBOS>& combo_cards() {
    static const std::array<ComboCards, NUM_COMBOS> table = make_combo_cards();
    return table;
}

int swap_index(int a, int b) {
    if (a > b) std::swap(a, b);
    for (int i = 0; i < NUM_SUIT_SWAPS; i++) {
        if (SWAP_A[i] == a && SWAP_B[i] == b) return i;
    }
    return -1;
}

int swap_suit(int suit, int swap) {
    if (suit == SWAP_A[swap]) return SWAP_B[swap];
    if (suit == SWAP_B[swap]) return SWAP_A[swap];
    return suit;
}

int swap_card(int card, int swap) { return card_rank(card) * 4 + swap_suit(card_suit(card), swap); }

int parse_card(const std::string& s) {
    if (s.size() != 2) return -1;
    int r = parse_rank(s[0]);
    int su = parse_suit(s[1]);
    if (r < 0 || su < 0) return -1;
    return r * 4 + su;
}

std::string card_str(int card) { return {RANK_CHARS[card_rank(card)], SUIT_CHARS[card_suit(card)]}; }

std::vector<int> parse_cards(const std::string& text) {
    std::string clean;
    for (char ch : text) {
        if (!std::isspace((unsigned char)ch) && ch != ',') clean += ch;
    }
    if (clean.size() % 2) throw std::runtime_error("bad card list '" + text + "'");
    std::vector<int> cards;
    uint64_t seen = 0;
    for (size_t i = 0; i < clean.size(); i += 2) {
        int c = parse_card(clean.substr(i, 2));
        if (c < 0) throw std::runtime_error("bad card '" + clean.substr(i, 2) + "'");
        if (seen & card_bit(c)) throw std::runtime_error("duplicate card " + card_str(c));
        seen |= card_bit(c);
        cards.push_back(c);
    }
    return cards;
}

std::string combo_str(int combo) {
    const ComboCards& cc = combo_cards()[combo];
    return card_str(cc.hi) + card_str(cc.lo);
}

std::string combo_str(int c1, int c2) { return c1 > c2 ? card_str(c1) + card_str(c2) : card_str(c2) + card_str(c1); }

uint64_t cards_mask(const std::vector<int>& cards) {
    uint64_t mask = 0;
    for (int c : cards) mask |= card_bit(c);
    return mask;
}

std::array<float, NUM_COMBOS> parse_range(const std::string& text) {
    std::array<float, NUM_COMBOS> weights{};
    std::string token;
    auto flush = [&]() {
        if (token.empty()) return;
        float w = 1.0f;
        std::string spec = token;
        size_t colon = token.find(':');
        if (colon != std::string::npos) {
            spec = token.substr(0, colon);
            try {
                w = std::stof(token.substr(colon + 1));
            } catch (const std::exception&) {
                throw std::runtime_error("bad weight in range entry '" + token + "'");
            }
            if (w < 0.0f || w > 1.0f) throw std::runtime_error("weight must be in [0, 1]: '" + token + "'");
        }
        apply_spec(weights, spec, w);
        token.clear();
    };
    for (char ch : text) {
        if (ch == ',' || std::isspace((unsigned char)ch)) flush();
        else token += ch;
    }
    flush();
    return weights;
}

void combo_grid_pos(int combo, int& row, int& col) {
    const ComboCards& cc = combo_cards()[combo];
    int r1 = card_rank(cc.hi);
    int r2 = card_rank(cc.lo);
    if (r1 == r2) {
        row = col = 12 - r1;
    } else if (card_suit(cc.hi) == card_suit(cc.lo)) {
        row = 12 - r1;
        col = 12 - r2;
    } else {
        row = 12 - r2;
        col = 12 - r1;
    }
}

std::string grid_label(int row, int col) {
    if (row == col) return {RANK_CHARS[12 - row], RANK_CHARS[12 - row]};
    if (row < col) return {RANK_CHARS[12 - row], RANK_CHARS[12 - col], 's'};
    return {RANK_CHARS[12 - col], RANK_CHARS[12 - row], 'o'};
}
