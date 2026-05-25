#pragma once

#include <vector>
#include <iostream>
#include "include/enums/node_type.h"
#include "include/solver/node.h"
#include "include/solver/isomorpher.h"


struct Tree {
    //std::unordered_map<int, ChanceOutcomes> global_board_to_local; //global -> local index of given boards within flop

    std::vector<Node> nodes; //actual nodes in tree, accessed through nodes[node_idx]

    std::vector<uint32_t> children; //takes in index of a child, returns its node_idx

    std::vector<_Float16> regrets; //huge regret memory
    std::vector<_Float16> strategy;

    //private_state_gtl[local_index * 1326 + global_hand_index] -> local_hand_index
    std::vector<std::vector<int>> private_state_gtl{(1 + 49 + 49 * 24)}; // global -> local index of private hands

    //private_state_gtl[local_index * 1326 + local_hand_index] -> hand_strength 
    std::vector<int> hand_strength{(1 + 49 + 49 * 24) * 1326}; // hand_strength

    /*
    void set_indexing(uint64_t flop) {
        global_board_to_local = get_flop_info(flop).first;
        std::cout << global_board_to_local.size();
    }
    */
};

uint32_t add_action_node(Tree& tree, int board_idx, int num_actions, int num_private_hands, std::vector<int>& global_to_local);

uint32_t add_chance_node(Tree& tree, int num_outcomes);

uint32_t add_first_river_action_node(Tree& tree, int board_idx, int num_actions, int num_private_hands, std::vector<int>& global_to_local, uint64_t cards);

uint32_t add_river_action_node(Tree& tree, int board_idx, int num_actions, int num_private_hands, std::vector<int>& global_to_local);

uint32_t add_terminal_node(Tree &tree, int board_idx, float pot);