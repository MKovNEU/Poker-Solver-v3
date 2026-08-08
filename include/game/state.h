#pragma once 

#include <vector>
#include <utility>
#include "include/game/undo.h"
#include "include/enums/action_type.h"

class State {
private:
    std::pair<float, float> committed;
    std::pair<float, float> comm_street;
    std::pair<float, float> stacks;
    std::vector<Action> raises; //allowed raising moves;
    ActionType last_move;
    int player; //0 = oop, 1 = ip
    int street;
    float prev_bet;
    int num_bets;
    std::array<uint64_t, 3> cards;


public:
    State(std::pair<float, float> committed, std::pair<float, float> &stacks, std::vector<std::pair<float, float>> &betting_sizes, std::array<uint8_t, 3> &flop): 
        committed(committed), comm_street(std::make_pair(0.0f, 0.0f)), stacks(stacks), last_move(ActionType::CHANCE), 
        player(0), street(1), prev_bet(0.0f), num_bets(0), cards({0, 0, 0}) {
            for (const std::pair<float, float> &p : betting_sizes){
                Action action(p.first, p.second);
                raises.push_back(action);
            }
            for (const uint8_t card : flop) {
                cards[0] |= (1ULL << card);
            }
        }


    bool is_terminal(); // is gamestate terminal

    bool is_chance();

    bool is_river();

    float get_utility();

    uint64_t get_board();

    std::vector<Action> get_legal_actions();

    std::pair<uint64_t, uint8_t> get_outcomes();

    std::pair<std::vector<int>, int> get_private_hands();

    int get_board_index(); //dense board index

    UndoInfo move(Action &action);

    UndoInfo apply_outcome(int outcome);

    void undo(UndoInfo &undo);
};