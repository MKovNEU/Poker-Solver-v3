#pragma once

#include<vector>

enum class ActionType : uint8_t {
    CHANCE,
    FOLD,
    CALL, //check/call
    RAISE, //increase price of entry to next street
    ALLIN //go all-in
};

struct Action {
    ActionType type;
    float pot_percentage; //for opens
    float raise_mult; //for re-raises

    Action(ActionType type): type(type) {}

    Action(float pp, float mult): type(ActionType::RAISE), pot_percentage(pp), raise_mult(mult) {}
};