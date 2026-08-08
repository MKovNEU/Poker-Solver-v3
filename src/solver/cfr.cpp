#include <algorithm>
#include <iostream>
#include "include/enums/node_type.h"
#include "include/solver/node.h"
#include "include/game/state.h"
#include "include/solver/tree.h"
#include "include/game/undo.h"
#include "include/enums/action_type.h"
#include "include/solver/cfr.h"
#include "include/solver/range.h"


void get_strategy(Tree &tree, int node_idx, std::vector<int>&data, int nph, int na) { //num private hands, num actions
    Node node = tree.nodes[node_idx];
    for (int hand = 0; hand < nph; hand++) {
        _Float16 sum = 0;
        std::vector<_Float16> regrets(na);
        int base = node.regret_begin + hand * na;
        for (int action = 0; action < na; action++) {
            _Float16 regret = std::max(tree.regrets[base + action], (_Float16)0.0f);
            sum += regret;
            regrets[action] = regret;
        }
        if (sum == (_Float16)0.0f) {
            _Float16 prob = 1.0f / na;
            for (int action = 0; action < na; action++) data[hand * na + action] = prob;
        } else {
            for (int action = 0; action < na; action++) data[hand * na + action] = regrets[action] / sum;
        }
    }
}


std::array<float, 1326> traverse(Tree &tree, int node_idx, std::array<float, 1326>& oop_range, std::array<float, 1326>& ip_range, int current_player, int t) { 
    Node node = tree.nodes[node_idx];
    if (node.type == NodeType::TERMINAL) {
        //always oop perspective
        float win_prob = 1;
        float tie_prob = 0;
        std::array<float, 1326> ev;
        std::vector<std::pair<int, int>> hss = tree.board_info[node.board_idx].hand_strength_sorted;
        std::vector<int> hs = tree.board_info[node.board_idx].hand_strength;
        int i = 0;
        while (i < 1326) {
            int hand_index = hss[i].second;
            int hand_strength = hss[i].first;
            int j = 0;
            while (j < 1326 && hss[j+1].first == hand_strength) {
                j++;
                tie_prob += ()
            }
            float util = node.pot * win_prob - node.pot * (1 - win_prob - tie_prob);
            ev[hand_index] = 
            i+= j;
        }
    } 
    else if (node.type == NodeType::CHANCE) {
        uint32_t b = node.child_begin;
        uint16_t n = node.child_count;
        std::array<float, 1326> ev;
        for (int i = 0; i < n; i++) {
            std::array<float, 1326> new_oop_range = update_range(oop_range, tree.board_info[node.board_idx].indexing_cards, false);
            std::array<float, 1326> new_ip_range = update_range(ip_range, tree.board_info[node.board_idx].indexing_cards, false);

            std::array<float, 1326> curr_ev = traverse(tree, tree.children[b + i], new_oop_range, new_ip_range, 0, t);
            for (int j = 0; j < 1326; j++) ev[j] += curr_ev[j];
        }
        for (int j = 0; j < 1326; j++) ev[j] /= n;
        return ev;
    }

    int num_private_hands = tree.board_info[node.board_idx].num_private_hands;
    std::vector<int> strategy(num_private_hands * node.child_count);
    get_strategy(tree, node_idx, strategy, num_private_hands, node.child_count);

    uint32_t b = node.child_begin;
    uint16_t n = node.child_count;

    std::vector<std::array<float, 1326>> action_utils(n);
    std::array<float, 1326> ev;
    std::vector<int> global_to_local = tree.board_info[node.board_idx].global_to_local;
    for (int i = 0; i < n; i++) {
        if (current_player == 0) {
            std::array<float, 1326> new_oop_range;
            //for impossible hands, strategy is wrong index, but ip_range is 0, so it doesnt matter
            for (int j = 0; j < 1326; j++) new_oop_range[j] = oop_range[j] * strategy[num_private_hands * i + global_to_local[j]];
            action_utils[i] = traverse(tree, tree.children[b + i], new_oop_range, ip_range, 1 - current_player, t);
        } else {
            std::array<float, 1326> new_ip_range;
            for (int j = 0; j < 1326; j++) new_ip_range[j] = ip_range[j] * strategy[num_private_hands * i + global_to_local[j]];
            action_utils[i] = traverse(tree, tree.children[b + i], oop_range, new_ip_range, 1 - current_player, t);
        }
        for (int j = 0; j < n; j++) ev[j] = action_utils[i][j] * strategy[j * n + i];
    }
    int sign = 1 - (2 * current_player);
    std::array<float, 1326> p_reach = (current_player == 0) ? oop_range : ip_range;
    std::array<float, 1326> o_reach = (current_player == 0) ? ip_range : oop_range;

    for (int a = 0; a < n; a++) {
        std::vector<bool> seen(1326, false);
        for (int hand = 0; hand < 1326; hand++) {
            int regret = sign * (action_utils[a][hand] - ev[hand]);
            int local_hand = global_to_local[hand];
            if (seen[local_hand]) continue; //only update regret for one of the iso hands, since they are all the same
            tree.regrets[node.regret_begin + local_hand * n + a] = std::max(0.0f, 
                tree.regrets[node.regret_begin + local_hand * n + a] + o_reach[hand] * regret);
            
            tree.strategy[node.strategy_begin + local_hand * n + a] = p_reach[hand] + strategy[local_hand * n + a];

            seen[local_hand] = true;
        }
    }

    return ev;
}

void CFR::run(int iterations) {
    uint64_t cards = 0;
    for (const uint8_t card : flop) {
        cards |= (1ULL << card);
    }
    
    std::array<float, 1326> pf_oop_range = update_range(convert(oop_range), cards, true); //convert range to postflop 
    std::array<float, 1326> pf_ip_range = update_range(convert(ip_range), cards, true); //convert range to postflop 

    for (int i = 1; i <= iterations; i++) {
        if (i % 100 == 0) std::cout << i;
        traverse(tree, 0, pf_oop_range, pf_ip_range, 0, i); 
    }
}



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
        int board_idx = game_state.get_board_index();
        if (game_state.is_river()) {
            Node parent_node = tree.nodes[parent_idx];
            if (parent_node.type == NodeType::CHANCE) { //first river node, make hand strength ranking
                node_idx = add_first_river_action_node(tree, board_idx, num_actions, game_state.get_board());
            } else { //not first, just copy parents strength ranking
                node_idx = add_river_action_node(tree, board_idx, num_actions);
            }
        }
        else {
            node_idx = add_action_node(tree, board_idx, num_actions);
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