//What we need this class to do
// - Take in board (3, 4, 5 cards) and output index (accounting for isomorphism)
// - Take in Board (3, 4, 5 cards) and output dense hand index (hand_index[#of hands] -> canonical_hand_index[less than # of hands])
// - Take in 
#pragma once

#include<vector>
#include <unordered_map>

struct ChanceOutcomes {
    int local_index;
    uint8_t multiplicity; 

    ChanceOutcomes() : local_index(-1), multiplicity(0) {}

    ChanceOutcomes(int index): local_index(index), multiplicity(1) {}
};

int index_board(uint64_t board);

std::pair<uint64_t, uint8_t> board_indexer(uint64_t board);

std::pair<std::vector<int>, int> hand_indexer(uint64_t board);

void test_board_indexer();

void test_board_hand_indexer();

std::pair<std::unordered_map<int, ChanceOutcomes>, int> get_flop_info(uint64_t flop);