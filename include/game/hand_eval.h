#pragma once 

#include <fstream>
#include <vector>
#include <stdexcept>


void loadHandRanks(const std::string &path);

int evaluateHand(const std::vector<uint8_t> cards);