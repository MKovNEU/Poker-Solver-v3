#pragma once

#include<vector>
#include "include/enums/node_type.h"
#include "include/solver/node.h"
#include "include/solver/tree.h"


uint32_t add_action_node(Tree& tree, int board_idx, int num_actions) {
    uint32_t node_idx = tree.nodes.size();

    Node node{};
    int local_idx = 0;
    local_idx = tree.global_board_to_local.at(board_idx);
    BoardInfo& info = tree.board_info[local_idx];

    node.type = NodeType::ACTION;
    node.board_idx = local_idx;
    node.regret_begin = tree.regrets.size();
    node.strategy_begin = tree.strategy.size();
    node.child_begin = tree.children.size();
    node.child_count = num_actions;

    int total = num_actions * info.num_private_hands;

    tree.regrets.resize(tree.regrets.size() + total, 0);
    tree.strategy.resize(tree.strategy.size() + total, 0);
    tree.children.resize(tree.children.size() + num_actions, 0);
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

uint32_t add_first_river_action_node(Tree& tree, int board_idx, int num_actions, uint64_t cards) {
    uint32_t node_idx = tree.nodes.size();

    Node node{};
    int local_idx = 0;
    local_idx = tree.global_board_to_local.at(board_idx);

    BoardInfo& info = tree.board_info[local_idx];

    node.type = NodeType::ACTION;
    node.board_idx = local_idx;
    node.regret_begin = tree.regrets.size();
    node.strategy_begin = tree.strategy.size();
    node.child_begin = tree.children.size();
    node.child_count = num_actions;

    int total = num_actions * info.num_private_hands;

    tree.regrets.resize(tree.regrets.size() + total, 0);
    tree.strategy.resize(tree.strategy.size() + total, 0);
    tree.children.resize(tree.children.size() + num_actions, 0);

    tree.nodes.push_back(node);

    return node_idx;
}


uint32_t add_river_action_node(Tree& tree, int board_idx, int num_actions) {
    uint32_t node_idx = tree.nodes.size();

    Node node{};
    int local_idx = 0;
    local_idx = tree.global_board_to_local.at(board_idx);
    BoardInfo& info = tree.board_info[local_idx];

    node.type = NodeType::ACTION;
    node.board_idx = local_idx;
    node.regret_begin = tree.regrets.size();
    node.strategy_begin = tree.strategy.size();
    node.child_begin = tree.children.size();
    node.child_count = num_actions;

    int total = num_actions * info.num_private_hands;

    tree.regrets.resize(tree.regrets.size() + total, 0);
    tree.strategy.resize(tree.strategy.size() + total, 0);
    tree.children.resize(tree.children.size() + num_actions, 0);

    tree.nodes.push_back(node);

    return node_idx;
}

uint32_t add_terminal_node(Tree &tree, int board_idx, float pot) {
    uint32_t node_idx = tree.nodes.size();

    Node node{};
    int local_idx = 0;
    local_idx = tree.global_board_to_local.at(board_idx);
    
    BoardInfo& info = tree.board_info[local_idx];

    node.type = NodeType::TERMINAL;
    node.board_idx = local_idx;
    node.pot = pot;

    tree.nodes.push_back(node);

    return node_idx;
}

