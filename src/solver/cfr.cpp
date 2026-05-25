#include <algorithm>
#include <iostream>
#include "include/enums/node_type.h"
#include "include/solver/node.h"
#include "include/game/state.h"
#include "include/solver/tree.h"
#include "include/game/undo.h"
#include "include/enums/action_type.h"
#include "include/solver/cfr.h"

/*
void get_strategy(Tree &tree, int node_idx, float* strategy) {
    Node node = tree.nodes[node_idx];
    uint16_t n = node.child_count;
    uint32_t b = node.child_begin;

    for (int i = 0; i < 1326; i++) {
        float sum = 0.0f;

        for (int j = 0; j < n; j++) {
            float r = tree.regrets[b + j];
            if (r > 0.0f) sum += r;
        }

        if (sum == 0.0f) {
            float uniform = 1.0f / n;
            for (int j = 0; j < n; j++) {
                strategy[i * n + j] = uniform;
            }
        } else {
            float inv_sum = 1.0f / sum;
            for (int j = 0; j < n; j++) {
                float r = tree.regrets[b + j];
                strategy[i * n + j] = (r > 0.0f) ? (r * inv_sum) : 0.0f;
            }
        }
    }
}


std::array<float, 1326> traverse(Tree &tree, int node_idx, std::array<float, 1326> oop_range, std::array<float, 1326> ip_range, int current_player, int t) { 
    Node node = tree.
    //return util from OOP perspective
    if (node.type == TERMINAL_NODE) {
        
    } 
    else if (node.type == CHANCE_NODE) {
        int n = node.children.size();
        std::array<float, 1326> ev = { 0 };
        for (PublicNode *child : node.children) {
            std::array<float, 1326> res = traverse(*child, oop_range, ip_range, 0, t);
            for (int i = 0; i < 1326; i++) ev[i] += res[i];
        }
        for (int i = 0; i < 1326; i++) ev[i] /= n;
        return ev;
    }

    int n = node.children.size();
    std::vector<std::array<float, 1326>> action_utils(n);
    std::array<float, 1326> ev = { 0 };
    
    std::vector<float> strategy(1326 * n);
    get_strategy(node, n, strategy.data());

    for (int i = 0; i < n; i++) {
        std::array<float, 1326> val;
        for (int j = 0; j < 1326; j++){
            val[j] = (current_player ? ip_range : oop_range)[j] * strategy[j * n + i];
        }

        if (current_player) {
            action_utils[i] = traverse(*node.children[i], oop_range, val, 1 - current_player, t);
        } else {
            action_utils[i] = traverse(*node.children[i], val, ip_range, 1 - current_player, t);
        }
        for (int j = 0; j < 1326; j++) ev[j] += strategy[j * n + i] * action_utils[i][j];
    }

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < 1326; j++) {
            float regret = (current_player ? -1 : 1) * (action_utils[i][j] - ev[j]);
            node.regrets[j * n + i] += (current_player ? oop_range[j] : ip_range[j]) * regret;
            node.strategySum[j * n + i] +=  (current_player ? ip_range[j] : oop_range[j]) * t * strategy[j * n + i];
        }

    }

    return ev;
}

void CFR::run(int iterations) {
    std::array<float, 1326> pf_oop_range = convert(oop_range); //convert range to postflop 
    std::array<float, 1326> pf_ip_range = convert(ip_range); //convert range to postflop 
    for (const uint8_t card : flop) {
        update_range(pf_oop_range, card);
        update_range(pf_ip_range, card);
    }
    for (int i = 1; i <= iterations; i++) {
        traverse(tree, 0, pf_oop_range, pf_ip_range, 0, i); 
    }
}
*/


//initial run, just builds tree for traversal
void build_run(Tree &tree, State &game_state, uint32_t parent_idx, uint32_t parent_child_begin, int action_idx, int depth) {
    if (tree.nodes.size() % 1000 == 0) std::cout << tree.nodes.size() << "\n";
    uint32_t node_idx = -1;
    if (game_state.is_terminal()){
        int board_idx = game_state.get_board_index();
        node_idx = add_terminal_node(tree, board_idx, game_state.get_utility());
    }
    else if (game_state.is_chance()) {
        std::pair<uint64_t, uint8_t> outcomes = game_state.get_outcomes();
        node_idx = add_chance_node(tree, outcomes.second);
        int child_idx = 0;
        for (int i = 0; i < 52; i++) {
            if (!((outcomes.first >> i) & 1)) continue;
            UndoInfo undo = game_state.apply_outcome(i);
            build_run(tree, game_state, node_idx, tree.nodes[node_idx].child_begin, child_idx, depth + 1);
            game_state.undo(undo);
            child_idx++;
        }
    }
    else { //action node
        std::vector<Action> actions = game_state.get_legal_actions();
        int num_actions = actions.size();
        std::pair<std::vector<int>, int> global_to_local = game_state.get_private_hands();
        int board_idx = game_state.get_board_index();
        if (game_state.is_river()) {
            Node parent_node = tree.nodes[parent_idx];
            if (parent_node.type == NodeType::CHANCE) { //first river node, make hand strength ranking
                node_idx = add_first_river_action_node(tree, board_idx, num_actions, global_to_local.second, global_to_local.first, game_state.get_board());
            } else { //not first, just copy parents strength ranking
                node_idx = add_river_action_node(tree, board_idx, num_actions, global_to_local.second, global_to_local.first);
            }
        }
        else {
            node_idx = add_action_node(tree, board_idx, num_actions, global_to_local.second, global_to_local.first);
        }
        for (int i = 0; i < num_actions; i++) {
            UndoInfo undo = game_state.move(actions[i]);
            build_run(tree, game_state, node_idx, tree.nodes[node_idx].child_begin, i, depth + 1);
            game_state.undo(undo);
        }
    }
    if (parent_idx != -1) { //not root node
        tree.children[parent_child_begin + action_idx] = node_idx;
    }
}

void CFR::buildTree() {
    build_run(tree, state, -1, -1, -1, 0);
    std::cout << tree.nodes.size() << "\n";
    int a_nodes = 0;
    int c_nodes = 0;
    int t_nodes = 0;
    for (const Node node : tree.nodes) {
        if (node.type == NodeType::ACTION) a_nodes++;
        if (node.type == NodeType::CHANCE) c_nodes++;
        if (node.type == NodeType::TERMINAL) t_nodes++;
    }
    std::cout << a_nodes << " " << c_nodes << " " << t_nodes;
}