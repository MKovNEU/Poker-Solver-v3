#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "include/core/cards.h"

enum class SizeKind : uint8_t {
    POT,   // fraction of the pot (for raises: of the pot after calling)
    MULT,  // raise to a multiple of the bet faced
    CHIPS, // fixed amount (for raises: on top of the bet faced), limit poker uses this
    ALLIN
};

struct BetSize {
    SizeKind kind;
    double value;
};

struct StreetSizes {
    std::vector<BetSize> bet[2];   // per player, 0 = oop, 1 = ip
    std::vector<BetSize> raise[2];
};

struct GameConfig {
    // game
    std::vector<int> board; // 3, 4 or 5 cards
    double pot = 0.0;       // pot at the start of the solved street
    double stack = 0.0;     // effective stack behind
    std::string range_text[2];
    std::array<float, NUM_COMBOS> range[2];
    StreetSizes streets[3]; // flop, turn, river
    int max_raises = 3;     // raises per street after the first bet (limit hold'em cap = 3)
    double add_allin_threshold = 1.5;   // add an all-in option when it's at most this many pots
    double force_allin_threshold = 0.15; // bets leaving less than this * pot behind become all-in
    bool isomorphism = true;

    // solver
    int iterations = 1000;
    double target_exploitability = 0.3; // percent of the starting pot
    int threads = 0;                    // 0 = all hardware threads
    int check_every = 10;               // iterations between exploitability checks
    bool compress = true;               // fp16 regrets / strategy: half the memory and memory traffic

    std::string text; // source text, stored in save files
};

GameConfig parse_config(const std::string& text); // throws std::runtime_error
GameConfig load_config(const std::string& path);

std::vector<BetSize> parse_sizes(const std::string& text, bool is_raise);
std::string size_str(const BetSize& size);
std::string format_chips(double chips);
