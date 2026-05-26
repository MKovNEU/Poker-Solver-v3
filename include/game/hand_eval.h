#pragma once 

#include <fstream>
#include <vector>
#include <stdexcept>


void loadHandRanks(const std::string &path);

int evaluateHand(const uint64_t hand, const uint64_t board);