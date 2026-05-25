#pragma once

#include<vector>
#include "include/enums/node_type.h"
#include "include/solver/node.h"
#include "include/solver/tree.h"


uint32_t add_action_node(Tree& tree, int board_idx, int num_actions, int num_private_hands, std::vector<int>& global_to_local) {
    uint32_t node_idx = tree.nodes.size();

    Node node{};

    node.type = NodeType::ACTION;
    //node.board_idx = tree.global_board_to_local.at(board_idx).local_index;
    node.regret_begin = tree.regrets.size();
    node.strategy_begin = tree.strategy.size();
    node.child_begin = tree.children.size();
    node.child_count = num_actions;

    int total = num_actions * num_private_hands;

    tree.regrets.resize(tree.regrets.size() + total, 0);
    tree.strategy.resize(tree.strategy.size() + total, 0);
    tree.children.resize(tree.children.size() + num_actions, 0);
    //tree.private_state_gtl[node.board_idx] = global_to_local;
    tree.nodes.push_back(node);

    return node_idx;
}


uint32_t add_chance_node(Tree& tree, int num_outcomes) {
    uint32_t node_idx = tree.nodes.size();

    Node node{};

    node.type = NodeType::CHANCE;

    node.child_begin = tree.children.size();
    node.child_count = num_outcomes;

    tree.children.resize(tree.children.size() + num_outcomes, 0);

    tree.nodes.push_back(node);

    return node_idx;
}

uint32_t add_first_river_action_node(Tree& tree, int board_idx, int num_actions, int num_private_hands, std::vector<int>& global_to_local, uint64_t cards) {
    uint32_t node_idx = tree.nodes.size();

    Node node{};

    node.type = NodeType::ACTION;
    //node.board_idx = tree.global_board_to_local.at(board_idx).local_index;
    node.regret_begin = tree.regrets.size();
    node.strategy_begin = tree.strategy.size();
    node.child_begin = tree.children.size();
    node.child_count = num_actions;

    int total = num_actions * num_private_hands;

    tree.regrets.resize(tree.regrets.size() + total, 0);
    tree.strategy.resize(tree.strategy.size() + total, 0);
    tree.children.resize(tree.children.size() + num_actions, 0);
    //tree.private_state_gtl[node.board_idx] = global_to_local;

    tree.nodes.push_back(node);

    return node_idx;
}


uint32_t add_river_action_node(Tree& tree, int board_idx, int num_actions, int num_private_hands, std::vector<int>& global_to_local) {
    uint32_t node_idx = tree.nodes.size();

    Node node{};

    node.type = NodeType::ACTION;
    //node.board_idx = tree.global_board_to_local.at(board_idx).local_index;
    node.regret_begin = tree.regrets.size();
    node.strategy_begin = tree.strategy.size();
    node.child_begin = tree.children.size();
    node.child_count = num_actions;

    int total = num_actions * num_private_hands;

    tree.regrets.resize(tree.regrets.size() + total, 0);
    tree.strategy.resize(tree.strategy.size() + total, 0);
    tree.children.resize(tree.children.size() + num_actions, 0);
    //tree.private_state_gtl[node.board_idx] = global_to_local;

    tree.nodes.push_back(node);

    return node_idx;
}

uint32_t add_terminal_node(Tree &tree, int board_idx, float pot) {
    uint32_t node_idx = tree.nodes.size();

    Node node{};

    node.type = NodeType::TERMINAL;
    //node.board_idx = tree.global_board_to_local.at(board_idx).local_index;
    node.pot = pot;

    tree.nodes.push_back(node);

    return node_idx;
}

