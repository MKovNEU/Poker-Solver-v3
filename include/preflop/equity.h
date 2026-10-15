#pragma once

#include <string>
#include <vector>
#include "include/core/cards.h"

// all-in equity of every starting hand against every other (1326 x 1326): ties count half, 0 where the two share a
// card. Exact over all 1,712,304 boards of each matchup, computed once (under a minute) and cached in a file.
class PreflopEquity {
public:
    // loads `path`, or computes the table and writes it there
    static PreflopEquity load_or_compute(const std::string& path, int threads, bool verbose);
    // boards = 0: every board, exact; otherwise that many random boards (tests)
    static PreflopEquity compute(int threads, int boards, bool verbose);

    float operator()(int a, int b) const { return eq_[(size_t)a * NUM_COMBOS + b]; }

    void save(const std::string& path) const;
    static bool load(const std::string& path, PreflopEquity& out);

private:
    std::vector<float> eq_;
};
