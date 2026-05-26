#include <iostream>
#include <cmath>
#include "include/game/state.h"
#include "include/game/undo.h"
#include "include/solver/isomorpher.h"

UndoInfo State::move(Action &action) {
    ActionType type = action.type;
    UndoInfo undo;
    undo.last_move = last_move;
    undo.oop_stack = stacks.first;
    undo.ip_stack = stacks.second;
    undo.oop_total_com = committed.first;
    undo.ip_total_com = committed.second;
    undo.oop_street_com = comm_street.first;
    undo.ip_street_com = comm_street.second;
    undo.player = player;
    undo.prev_bet = prev_bet;
    undo.num_bets = num_bets;
    undo.street = street;
    undo.cards = cards.first;
    undo.num_cards = cards.second;


    if (type == ActionType::FOLD) {
        last_move = ActionType::FOLD;
    } else if (type == ActionType::CALL) {
        if (last_move != ActionType::CALL && last_move != ActionType::CHANCE) { //Calling a bet, moves money only
            float toCall = prev_bet - (!player ? comm_street.first: comm_street.second);
            if (!player) {
                stacks.first -= toCall;
                committed.first += toCall;
                comm_street.first += toCall;
            } else {
                stacks.second -= toCall;
                committed.second += toCall;
                comm_street.second += toCall;
            }
        }
        if (last_move == ActionType::CHANCE) { //first check
            last_move = ActionType::CALL;
            player = 1 - player;
        }
        else { //second check or calling a bet
            street++;
            prev_bet = 0;
            num_bets = 0;
            comm_street.first = 0;
            comm_street.second = 0;
            if (street > 3 || last_move == ActionType::ALLIN) { 
                last_move = ActionType::CALL;
            } else {
                last_move = ActionType::CHANCE;
            }
            player = 0; //oop first always
        }
    } else { //raise 
        num_bets++;
        float new_bet = 0;
        float curr_commited = (!player ? comm_street.first: comm_street.second);
        float toCall = prev_bet - curr_commited;
        float effStack = std::min(stacks.first + comm_street.first, stacks.second + comm_street.second);
        if (type == ActionType::ALLIN) {
            new_bet = std::max(1.0f, effStack);
        } else {
            float pp = action.pot_percentage;
            float mult = action.raise_mult;
            float pot = committed.first + committed.second;
            new_bet = std::max(1.0f, (prev_bet == 0.0f) ? pp * (pot) : mult * toCall + curr_commited);
        }
        if (!player) {
            stacks.first -= (new_bet - curr_commited);
            committed.first += (new_bet - curr_commited);
            comm_street.first += (new_bet - curr_commited);
        } else {
            stacks.second -= (new_bet - curr_commited);
            committed.second += (new_bet - curr_commited);
            comm_street.second += (new_bet - curr_commited);
        }
        prev_bet = new_bet;
        last_move = type; //raise or allin
        player = 1 - player;
    }
    return undo;
}


std::vector<Action> State::get_legal_actions() {
    float curr_commited = (!player ? comm_street.first: comm_street.second);
    float to_call = prev_bet - curr_commited;
    float eff_stack = std::min(stacks.first + comm_street.first, stacks.second + comm_street.second);
    std::vector<Action> actions;

    if (prev_bet == 0) {
        actions.push_back(Action(ActionType::CALL)); //check

        for (const Action action : raises) {
            if (eff_stack > action.pot_percentage * (2 * committed.first)) {
                actions.push_back(action);
            }
        }

        actions.push_back(Action(ActionType::ALLIN));
    } 
    else {
        actions.push_back(Action(ActionType::FOLD));
        actions.push_back(Action(ActionType::CALL));

       for (const Action action : raises) {
            if (eff_stack - (action.raise_mult * to_call) > curr_commited && num_bets < 2) {
                actions.push_back(action);
            }
        }

        if (eff_stack > prev_bet)
            actions.push_back(Action(ActionType::ALLIN));
    }
    return actions;
}

std::pair<uint64_t, uint8_t> State::get_outcomes() {
    return board_indexer(cards.first);
}

UndoInfo State::apply_outcome(int outcome) {
    UndoInfo undo;
    undo.last_move = last_move;
    undo.oop_stack = stacks.first;
    undo.ip_stack = stacks.second;
    undo.oop_total_com = committed.first;
    undo.ip_total_com = committed.second;
    undo.oop_street_com = comm_street.first;
    undo.ip_street_com = comm_street.second;
    undo.player = player;
    undo.prev_bet = prev_bet;
    undo.num_bets = num_bets;
    undo.street = street;
    undo.cards = cards.first;
    undo.num_cards = cards.second;

    cards.first |= (1ULL << outcome);
    cards.second++;

    return undo;
}


void State::undo(UndoInfo &undo) {
    last_move = undo.last_move;
    stacks.first = undo.oop_stack;
    stacks.second = undo.ip_stack;
    committed.first = undo.oop_total_com;
    committed.second = undo.ip_total_com;
    comm_street.first = undo.oop_street_com;
    comm_street.second = undo.ip_street_com;
    player = undo.player;
    prev_bet = undo.prev_bet;
    num_bets = undo.num_bets;
    street = undo.street;
    cards.first = undo.cards;
    cards.second = undo.num_cards;
}

bool State::is_terminal() {
    //folds or river action finished or called all-in
    return (last_move == ActionType::FOLD || street > 3 || ((stacks.first <= 0.5f || stacks.second <= 0.5f) && last_move == ActionType::CALL));
}

bool State::is_chance() {
    return (cards.second - 2 != street); 
}

bool State::is_river() {
    return (street == 3);
}

uint64_t State::get_board() {
    return cards.first;
}

std::pair<std::vector<int>, int> State::get_private_hands() {
    return hand_indexer(cards.first);
}

int State::get_board_index() {
    if (cards.second == 3) {
        return index_board(cards.first);
    } else if (cards.second == 4) {
        return 1755 + index_board(cards.first);
    } else if (cards.second == 5) {
        return 1755 + 16432 + index_board(cards.first);
    } else {
        std::cout << "Bug in get_board_index" << "\n";
    }
}

float State::get_utility(){
    //should only get called in terminal states
    if (last_move == ActionType::FOLD) {
        return (!player ? committed.first : committed.second);
    } else { //showdown, equal commitited
        return committed.first;
    }
}