#include "include/game/hand_eval.h"
#include <vector>
#include <iostream>

using namespace std;

static unsigned int HR[32487834];

void load_hand_ranks(const std::string &path) {
    ifstream file(path, std::ios::binary);
    if (!file) throw runtime_error("Cannot open hand_Ranks.dat");

    file.read(reinterpret_cast<char*>(HR), sizeof(HR));

    if (!file) throw runtime_error("Failed reading hand_ranks.dat");
}


int evaluate_hand(const uint64_t hand, const uint64_t board) {
    int p = 53;
    for (size_t i = 0; i < 52; i++) {
        if ((board >> i) & 1 || (hand >> i) & 1) p = HR[p + (i+1)];
    }
    return HR[p];
}

void test_eval_hand(){
    std::cout << evaluate_hand(1052672, 274) << "\n"; //nothing (7, 5, 4, 3, 2(1))
    std::cout << evaluate_hand(4352, 19) << "\n"; //one pair (5, 4, 3, 2, 2)
    std::cout << evaluate_hand(768, 19) << "\n"; //2 pair (4, 4, 3, 2, 2)
    std::cout << evaluate_hand(272, 7) << "\n"; //3 of a (4, 3, 2, 2, 2)
    std::cout << evaluate_hand(69632, 274) << "\n"; //straight (6, 5, 4, 3, 2(1))
    std::cout << evaluate_hand(1052672, 273) << "\n"; //flush (7, 5, 4, 3, 2)
    std::cout << evaluate_hand(48, 7) << "\n"; //full house (3, 3, 2, 2, 2)
    std::cout << evaluate_hand(24, 7) << "\n"; //quads (3, 2, 2, 2, 2)
    std::cout << evaluate_hand(69632, 273) << "\n"; //straight flush (6, 5, 4, 3, 2)
}