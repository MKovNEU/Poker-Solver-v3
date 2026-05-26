#pragma once

#include<vector>
#include "include/enums/node_type.h"

struct Node {
    NodeType type;
    int board_idx; //local index

    uint32_t child_begin;
    uint16_t child_count;

    uint32_t regret_begin;

    uint32_t strategy_begin;

    float pot;
};

