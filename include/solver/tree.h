#pragma once

#include <vector>
#include <iostream>
#include "include/enums/node_type.h"
#include "include/solver/node.h"
#include "include/solver/isomorpher.h"


struct Tree {
    std::unordered_map<int, BoardInfo> global_board_to_local; //global -> local index of given boards within flop

    std::vector<Node> nodes; //actual nodes in tree, accessed through nodes[node_idx]

    std::vector<uint32_t> children; //takes in index of a child, returns its node_idx

    std::vector<_Float16> regrets; //huge regret memory
    std::vector<_Float16> strategy;

    void set_indexing(uint64_t flop) {
        global_board_to_local = get_flop_info(flop).first;
        std::cout << global_board_to_local.size();
    }
};

uint32_t add_action_node(Tree& tree, int board_idx, int num_actions);

uint32_t add_chance_node(Tree& tree, int num_outcomes);

uint32_t add_first_river_action_node(Tree& tree, int board_idx, int num_actions, uint64_t cards);

uint32_t add_river_action_node(Tree& tree, int board_idx, int num_actions);

uint32_t add_terminal_node(Tree &tree, int board_idx, float pot);