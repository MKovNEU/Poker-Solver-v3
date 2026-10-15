#include "include/core/hand_eval.h"

#include <bit>

// only used while building the tree (river hand rankings), so it favours simplicity over raw speed

namespace {

int straight_high(uint32_t ranks) {
    for (int high = 12; high >= 4; high--) {
        uint32_t need = 0x1Fu << (high - 4);
        if ((ranks & need) == need) return high;
    }
    if ((ranks & 0x100Fu) == 0x100Fu) return 3; // wheel, five high
    return -1;
}

int highest(uint32_t ranks) { return 31 - std::countl_zero(ranks); }

// packs tiebreak ranks, highest priority first, into the 5 nibbles below the category
struct Packer {
    uint32_t value = 0;
    int count = 0;

    void add(int rank) {
        value |= uint32_t(rank) << (16 - 4 * count);
        count++;
    }

    void add_top(uint32_t ranks, int n) {
        for (int r = 12; r >= 0 && n > 0; r--) {
            if (ranks >> r & 1) {
                add(r);
                n--;
            }
        }
    }
};

uint32_t make(uint32_t category, const Packer& p) { return category << 20 | p.value; }

} // namespace

uint32_t evaluate_hand(uint64_t cards) {
    uint32_t suits[4] = {0, 0, 0, 0};
    while (cards) {
        int c = std::countr_zero(cards);
        cards &= cards - 1;
        suits[c & 3] |= 1u << (c >> 2);
    }

    // with at most 7 cards a flush can't coexist with quads or a full house, so checking it first is safe
    for (int s = 0; s < 4; s++) {
        if (std::popcount(suits[s]) >= 5) {
            Packer p;
            int sf = straight_high(suits[s]);
            if (sf >= 0) {
                p.add(sf);
                return make(8, p);
            }
            p.add_top(suits[s], 5);
            return make(5, p);
        }
    }

    const uint32_t s0 = suits[0], s1 = suits[1], s2 = suits[2], s3 = suits[3];
    const uint32_t any = s0 | s1 | s2 | s3;
    const uint32_t two = (s0 & s1) | (s0 & s2) | (s0 & s3) | (s1 & s2) | (s1 & s3) | (s2 & s3);
    const uint32_t three = (s0 & s1 & s2) | (s0 & s1 & s3) | (s0 & s2 & s3) | (s1 & s2 & s3);
    const uint32_t four = s0 & s1 & s2 & s3;

    Packer p;
    if (four) {
        int q = highest(four);
        p.add(q);
        p.add_top(any & ~(1u << q), 1);
        return make(7, p);
    }
    if (three) {
        int t = highest(three);
        uint32_t pairs = two & ~(1u << t);
        if (pairs) {
            p.add(t);
            p.add(highest(pairs));
            return make(6, p);
        }
    }
    int st = straight_high(any);
    if (st >= 0) {
        p.add(st);
        return make(4, p);
    }
    if (three) {
        int t = highest(three);
        p.add(t);
        p.add_top(any & ~(1u << t), 2);
        return make(3, p);
    }
    if (two) {
        int p1 = highest(two);
        uint32_t rest = two & ~(1u << p1);
        if (rest) {
            int p2 = highest(rest);
            p.add(p1);
            p.add(p2);
            p.add_top(any & ~(1u << p1) & ~(1u << p2), 1);
            return make(2, p);
        }
        p.add(p1);
        p.add_top(any & ~(1u << p1), 3);
        return make(1, p);
    }
    p.add_top(any, 5);
    return make(0, p);
}

const char* hand_category_name(uint32_t strength) {
    static const char* names[] = {"high card", "pair",  "two pair",   "trips",         "straight",
                                  "flush",     "full house", "quads", "straight flush"};
    uint32_t c = strength >> 20;
    return c < 9 ? names[c] : "?";
}
