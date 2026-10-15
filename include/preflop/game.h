#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

constexpr int MAX_PLAYERS = 6;
constexpr int NUM_CLASSES = 169;     // AA, AKs, AKo, ... in the 13x13 grid layout (row * 13 + col, row 0 = aces)
constexpr int MAX_PREFLOP_ACTIONS = 3; // fold / check / call, and one raise

// Preflop game: up to 6 players with equal stacks, a fixed raise size at every level, and no multiway flops (a call
// that would put a third player into the flop isn't offered). A pot that reaches the flop is valued with all-in
// equity, each hand realizing it relative to the other's by position (OOP: `realization`) and by its playability
// (suited / connected hands more, offsuit gappers less), both fading out towards SPR 1. See preflop_realization.
struct PreflopConfig {
    int players = 6;           // UTG, HJ, CO, BTN, SB, BB (fewer players drop the earliest seats; 2 = BTN/SB vs BB)
    double stack = 100.0;      // starting stack of everyone, in big blinds
    double sb = 0.5;           // the big blind is 1
    double ante = 0.0;         // per player
    double open = 2.5;         // first raise, raise to (bb)
    double sb_open = 3.0;      // first raise from the small blind
    double iso = 3.5;          // raise over a limp, +1 bb for every extra limper
    double threebet_ip = 3.0;  // re-raises: raise to this multiple of the bet faced, by position against the raiser
    double threebet_oop = 4.0;
    double squeeze = 1.0;      // a 3-bet grows by this many times the open for every caller in between
    double fourbet_ip = 2.25;
    double fourbet_oop = 2.5;
    int allin_raise = 4;       // that raise is all-in and the last one (4 = the 5-bet)
    double allin_threshold = 0.4; // a raise putting in more than this share of the stack is all-in instead
    int limp = 1;              // 0: nobody limps, 1: the small blind can complete when folded to, 2: anyone can limp
    double realization = 0.7;  // OOP's equity realization relative to IP at SPR >= realization_spr
    double realization_spr = 8.0;
    double playability = 1.0;  // strength of the per-hand playability factors, 0 = off

    // solver
    int iterations = 2000;
    double target = 0.5;       // stop at this exploitability, mbb per hand per player
    int threads = 0;
    int check_every = 25;
    std::string equity_file = "data/preflop_equity.bin";

    std::string text;          // source text, stored in save files
};

PreflopConfig parse_preflop_config(const std::string& text); // throws std::runtime_error

std::vector<std::string> position_names(int players);
int small_blind_seat(int players);
int big_blind_seat(int players);
int postflop_order(int players, int seat); // 0 acts first after the flop

enum class PNodeType : uint8_t { ACTION, FOLD, SHOWDOWN };
enum class PAction : uint8_t { ROOT, FOLD, CHECK, CALL, RAISE, ALLIN };

struct PNode {
    PNodeType type;
    uint8_t player;       // ACTION: to act, FOLD: the one player left
    uint8_t num_children;
    uint32_t first_child;
    uint32_t data;        // ACTION: offset of its [action][class] blocks, terminals: index into PreflopTree::terminals
    float invested;       // ACTION: what the player to act has put in so far (what folding loses)
};

struct PMeta {
    uint32_t parent;
    PAction action;
    uint8_t actor;
    bool limp;            // CALL of the big blind
    float amount;         // RAISE / ALLIN: raised to, CALL: total in after calling
    float pot;            // pot after the action
};

struct PTerminal {
    std::array<float, MAX_PLAYERS> invested{}; // blinds, antes and bets of every seat
    float pot = 0.0f;
    uint8_t oop = 0, ip = 0;   // SHOWDOWN: the two players left, by position after the flop
    bool allin = false;        // SHOWDOWN: no chips behind, plain equity
    float behind = 0.0f;       // SHOWDOWN: effective stack left for the flop
    float depth = 0.0f;        // SHOWDOWN: how much realization applies, 1 at SPR >= realization_spr down to 0 at
                               // SPR 1 and all-in, in steps of 0.01 (the solver keeps one share matrix per step)
};

struct PreflopTree {
    std::vector<PNode> nodes; // nodes[0] is the root
    std::vector<PMeta> meta;
    std::vector<PTerminal> terminals;
    uint64_t storage = 0;     // floats per regret / strategy array
    uint32_t num_action_nodes = 0;
};

void build_preflop_tree(PreflopTree& tree, const PreflopConfig& cfg);

std::string preflop_action_str(const PMeta& meta); // "fold", "limp", "raise 2.5", "allin 100", ...

// hand classes
int combo_class(int combo);
std::string class_name(int cls);
int class_combos(int cls); // 6, 4 or 12

// relative postflop playability of each class, combo-weighted mean 1: suited hands +8%, offsuit -6%, connectors +6%
// down to -3% for gaps of 4+, times `strength`. A heuristic for what all-in equity misses (implied odds, nut
// potential), meant to be calibrated against postflop solves.
std::array<float, NUM_CLASSES> playability_factors(double strength);

// how well a hand of class `cls` realizes its equity at a flop of the given depth, relative to the other player:
// share of the pot = e R / (e R + (1 - e) R_other)
float preflop_realization(const PreflopConfig& cfg, const std::array<float, NUM_CLASSES>& playability, int cls, bool oop,
                          float depth);
