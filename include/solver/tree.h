#pragma once

#include <vector>
#include <iostream>
#include "include/enums/node_type.h"
#include "include/solver/node.h"
#include "include/solver/isomorpher.h"
#include "include/solver/range.h"


struct Tree {
    std::unordered_map<int, int> global_board_to_local; //global -> local index of given boards within flop
    std::vector<BoardInfo> board_info; //local index -> boardInfo of said board
    std::array<int, 100 * 1326> blockers; 

    std::vector<Node> nodes; //actual nodes in tree, accessed through nodes[node_idx]

    std::vector<uint32_t> children; //takes in index of a child, returns its node_idx

    std::vector<_Float16> regrets; //huge regret memory
    std::vector<_Float16> strategy;

    void set_indexing(uint64_t flop) {
        auto info = get_flop_info(flop);
        global_board_to_local = info.first;
        board_info = info.second;
        blockers = hand_blockers();
    }
};

uint32_t add_action_node(Tree& tree, int board_idx, int num_actions);

uint32_t add_chance_node(Tree& tree, int num_outcomes);

uint32_t add_first_river_action_node(Tree& tree, int board_idx, int num_actions, uint64_t cards);

uint32_t add_river_action_node(Tree& tree, int board_idx, int num_actions);

uint32_t add_terminal_node(Tree &tree, int board_idx, float pot);