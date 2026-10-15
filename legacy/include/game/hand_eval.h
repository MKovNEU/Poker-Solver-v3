#pragma once 

#include <fstream>
#include <vector>
#include <cstdint>
#include <stdexcept>


void load_hand_ranks(const std::string &path);

int evaluate_hand(const uint64_t hand, const uint64_t board);

void test_eval_hand();