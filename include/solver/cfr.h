#pragma once 

#include <cstdint>
#include <vector>
#include <utility>
#include <cassert>
#include <array>
#include "include/solver/node.h"
#include "include/solver/tree.h"
#include "include/game/state.h"

class CFR {
private:
    std::array<float, 169> oop_range;
    std::array<float, 169> ip_range;
    std::array<uint8_t, 3> flop;
    State state;
    Tree tree;

public:
    CFR(std::array<float, 169>& oop_range, std::array<float, 169> &ip_range, std::array<uint8_t, 3> &flop,
        float pot, std::pair<float, float> &stacks, std::vector<std::pair<float, float>> &betting_sizes) : 
        oop_range(oop_range), ip_range(ip_range), flop(flop), state(std::make_pair(pot / 2.0f, pot / 2.0f), stacks, betting_sizes, flop), tree{}
        {
            /*
            uint64_t cards = 0;
            for (const uint8_t card : flop) {
                cards |= (1ULL << card);
            }
            tree.set_indexing(cards);
            */
        }

    void buildTree(); 

    void run(int iterations);
};