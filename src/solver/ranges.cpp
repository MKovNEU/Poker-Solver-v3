#include "include/solver/range.h"
#include <vector>

std::array<float, 1326> convert(std::array<float, 169> preflop_range) {
    std::array<float, 1326> postflop_range; 
    //0-12 pocket pairs (22 - AA)
    for (int i = 0; i < 13; i++) {
        float pp_prob = preflop_range[i] / 6;
        for (int s1 = 1; s1 < 4; s1++) {
            for (int s2 = 0; s2 < s1; s2++) {
                int c1 = (i * 4) + s1;
                int c2 = (i * 4) + s2;
                int hand_index = ((c1 - 1) * c1) / 2 + c2;
                postflop_range[hand_index] = pp_prob;
            }
        }
    }

    //13-90 suited cards
    for (int i = 13; i < 91; i++) {
        float pp_prob = preflop_range[i] / 4;
        int r1 = 0;
        while ((r1 * (r1 - 1)) / 2 < (i - 13)) r1++;
        int r2 = (i - 13) - ((r1 * (r1 - 1)) / 2);
        for (int s = 0; s < 4; s++) {
            int c1 = (r1 * 4) + s;
            int c2 = (r2 * 4) + s;
            int hand_index = ((c1 - 1) * c1) / 2 + c2;
            postflop_range[hand_index] = pp_prob;
        }
    }

    //91-168 offsuit
    for (int i = 91; i < 169; i++) {
        float pp_prob = preflop_range[i] / 12;
        int r1 = 0;
        while ((r1 * (r1 - 1)) / 2 < (i - 91)) r1++;
        int r2 = (i - 91) - ((r1 * (r1 - 1)) / 2);
        for (int s1 = 0; s1 < 4; s1++) {
            for (int s2 = 0; s2 < 4; s2++) {
                if (s1 == s2) continue;
                int c1 = (r1 * 4) + s1;
                int c2 = (r2 * 4) + s2;
                int hand_index = ((c1 - 1) * c1) / 2 + c2;
                postflop_range[hand_index] = pp_prob;
            }
        }
    }
}

std::array<float, 1326> update_range(const std::array<float, 1326>& range, uint64_t cards, bool is_canon) { //is_canon diff between multiple real cards vs iso card option
    int card_count = 0;
    for (int i = 0; i < 52; i++) {
        if ((cards >> i) & 1) card_count++;
    }

    std::vector<int> count(1326);
    for (int i = 0; i < 52; i++) {
        if (!((cards >> i) & 1)) continue;
        for (int c2 = 0; c2 < 52; c2++) {
            if (i == c2) continue;
            if (i >= c2) {
                int hand_index = ((i - 1) * i) / 2 + c2;
                count[hand_index]++;
            } else {
                int hand_index = ((c2 - 1) * c2) / 2 + i;
                count[hand_index]++;
            }
        }
    }

    std::array<float, 1326> mask;
    if (is_canon) {
        for (int i = 0; i < 1326; i++) {
            mask[i] = (count[i] == 0) ? 1.0f : 0.0f; //binary, all hands containing seen cards are impossible
        }
    } else {
        for (int i = 0; i < 1326; i++) {
            //we are given canonical dealt card, masking corresponds to probaility any of collapsing cards were actually dealt
            //ie if we know a 6 was dealt, and all 4 suits are isomorphic, then each one of the sixes has a 1/4 chance of have being dealt
            //thus, a hand such as Ah6h has a 1/4 chance of not being possible, ie when 6h was actual dealt hand, thus 3/4 as likely as Ahkh
            //6h6d was seen twice, thus only 1/2 chance of being possible, when 6s, 6c was actually dealt
            mask[i] = 1 - (count[i] / card_count);
        }
    }
    
    std::array<float, 1326> new_range;
    for (int i = 0; i < 1326; i++) new_range[i] = range[i] * mask[i];

    return new_range;
}


std::array<int, 100 * 1326> hand_blockers() { //each hand blocks some other hands from existing
    std::array<int, 100 * 1326> blockers;
    
    for (int c1 = 1; c1 < 52; c1++) {
        for (int c2 = 0; c2 < c1; c2++) {
            int hand_index = ((c1 - 1) * c1) / 2 + c2;

            for (int oc = 0; oc < 52; oc++) {
                if (oc == c1 || oc == c2) continue;
                int o_hand_index = 0;
                if (oc > c1) o_hand_index = ((oc - 1) * oc) / 2 + c1;
                else o_hand_index = ((c1 - 1) * c1) / 2 + oc;
                int shift = (oc - (oc > c1 ? 1 : 0) - (oc > c2 ? 1 : 0));
                blockers[hand_index * 100 + shift] = o_hand_index;
            }

            for (int oc = 0; oc < 52; oc++) {
                if (oc == c2 || oc == c1) continue;
                int o_hand_index = 0;
                if (oc > c2) o_hand_index = ((oc - 1) * oc) / 2 + c2;
                else o_hand_index = ((c2 - 1) * c2) / 2 + oc;
                int shift = (oc - (oc > c1 ? 1 : 0) - (oc > c2 ? 1 : 0)) + 50;
                blockers[hand_index * 100 + shift] = o_hand_index;
            }
        }
    }

    return blockers;
}