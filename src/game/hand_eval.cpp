#include "include/game/hand_eval.h"
#include <vector>

using namespace std;

static unsigned int HR[32487834];

void loadHandRanks(const std::string &path) {
    ifstream file(path, std::ios::binary);
    if (!file) throw runtime_error("Cannot open hand_Ranks.dat");

    file.read(reinterpret_cast<char*>(HR), sizeof(HR));

    if (!file) throw runtime_error("Failed reading hand_ranks.dat");
}


int evaluateHand(const std::vector<uint8_t> cards) {
    int p = 53;
    for (size_t i = 0; i < 7; i++) {
        p = HR[p + cards[i]];
    }
    return p;
}