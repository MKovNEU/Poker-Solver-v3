//What we need this class to do
// - Take in board (3, 4, 5 cards) and output index (accounting for isomorphism)
// - Take in Board (3, 4, 5 cards) and output dense hand index (hand_index[#of hands] -> canonical_hand_index[less than # of hands])
// - Take in 
#pragma once

#include<vector>
#include <unordered_map>

struct BoardInfo {
    int local_index; //local board index;
    int num_private_hands; //num of private hands (using board cards + suit isomorphism)
    std::vector<int> global_to_local; //global -> local private hand index;
    std::vector<std::pair<int, int>> hand_strength; //sorted hand strength of private cards (local index), only created for 

    BoardInfo() : local_index(-1) {}

    BoardInfo(int index, std::vector<int>& g2l, int num_private_hands): local_index(index), global_to_local(g2l), 
        num_private_hands(num_private_hands) {}

    BoardInfo(int index, std::vector<int>& g2l, std::vector<std::pair<int, int>>& hand_str, int num_private_hands): local_index(index), global_to_local(g2l), 
        num_private_hands(num_private_hands), hand_strength(hand_str) {}
};

int index_board(uint64_t board);

std::pair<uint64_t, uint8_t> board_indexer(uint64_t board);

std::pair<std::vector<int>, int> hand_indexer(uint64_t board);

void test_board_indexer();

void test_board_hand_indexer();

//global -> local index, local index -> BoardInfo
std::pair<std::unordered_map<int, int>, std::vector<BoardInfo>> get_flop_info(uint64_t flop);