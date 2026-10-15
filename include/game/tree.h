#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "include/game/config.h"

enum class NodeType : uint8_t { ACTION, CHANCE, FOLD, SHOWDOWN };

enum class ActionKind : uint8_t { ROOT, FOLD, CHECK, CALL, BET, RAISE, ALLIN, DEAL };

// hot node data, read every iteration, children of a node are stored contiguously
struct Node {
    NodeType type;
    uint8_t player;         // ACTION: player to act, FOLD: player who folded
    uint16_t num_children;
    uint32_t first_child;
    uint32_t board;         // index into Tree::boards
    float payoff;           // FOLD / SHOWDOWN: chips the winner nets (each player owns half the starting pot)
    uint64_t offset;        // ACTION: offset into the regret / strategy storage, CHANCE: index into Tree::chance,
                            // FOLD: index into Tree::symmetries
};

// cold node data, only used for building and printing
struct NodeMeta {
    uint32_t parent;
    ActionKind action;      // action that led to this node
    uint8_t card;           // DEAL: dealt card
    uint8_t street;         // 0 = flop, 1 = turn, 2 = river
    float amount;           // BET / RAISE / ALLIN: total street bet after the action, CALL: chips added
    float pot;              // pot after the action, current street bets included
    float stack[2];         // stacks after the action
};

// a dealt card without its own subtree: it's isomorphic to chance child `child` after exchanging two suits
struct IsoCard {
    uint16_t child;
    uint8_t card;
    uint8_t swap;
};

struct ChanceData {
    uint32_t iso_begin;
    uint32_t iso_count;
    uint32_t symmetry; // index into Tree::symmetries
};

// suits that are interchangeable at a node: class[s] = lowest suit equivalent to s, symmetries[0] is the identity
using SuitClasses = std::array<uint8_t, 4>;

struct BoardData {
    uint64_t mask;
    int num_cards;
};

struct Tree {
    std::vector<Node> nodes; // nodes[0] is the root
    std::vector<NodeMeta> meta;
    std::vector<ChanceData> chance;
    std::vector<IsoCard> iso;
    std::vector<BoardData> boards;
    std::unordered_map<uint64_t, uint32_t> board_ids;
    std::vector<SuitClasses> symmetries;
    uint64_t storage_size = 0; // floats per regret / strategy array
    uint64_t num_action_nodes = 0, num_chance_nodes = 0, num_terminal_nodes = 0;
};

// builds the full game tree, an action node of player p reserves num_hands[p] floats per action,
// swap_ok[s] says whether both ranges are symmetric under suit swap s (needed for chance isomorphism)
void build_tree(Tree& tree, const GameConfig& config, const int num_hands[2], const bool swap_ok[NUM_SUIT_SWAPS]);

std::string action_str(const NodeMeta& meta); // "check", "bet 3.3", "Kd", ...
