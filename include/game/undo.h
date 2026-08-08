#pragma once 

#include "include/enums/action_type.h"

struct UndoInfo {
    ActionType last_move;
    float oop_stack;
    float ip_stack;
    float oop_total_com;
    float ip_total_com;
    float oop_street_com;
    float ip_street_com;
    int player;
    float prev_bet;
    int num_bets;
    int street;
    std::array<uint64_t, 3> cards;
};