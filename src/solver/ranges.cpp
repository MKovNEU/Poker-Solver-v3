#include "include/solver/range.h"

std::array<float, 1326> convert(std::array<float, 169> preflop_range) {
    std::array<float, 1326> postflop_range; 
    //0-12 pocket pairs (22 - AA) -> 0-78 pocket pairs ((all 22) - (all AA))
    for (int i = 0; i < 13; i++) {
        float pp_prob = preflop_range[i] / 6;
        for (int j = 0; j < 6; j++) {
            postflop_range[i * 6 + j] = pp_prob; 
        }
    }

    //13-90 suited cards
    for (int i = 13; i < 91; i++) {
        float pp_prob = preflop_range[i] / 4;
        for (int j = 0; j < 4; j++) {
            postflop_range[(6 * 13) + (i-13) * 4 + j] = pp_prob;
        }
    }

    //91-168 offsuit
    for (int i = 91; i < 169; i++) {
        float pp_prob = preflop_range[i] / 12;
        for (int j = 0; j < 12; j++) {
            postflop_range[(6 * 13) + (78 * 4) + (i - 91) * 12 + j] = pp_prob;
        }
    }
}