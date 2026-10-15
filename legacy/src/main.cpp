#include <vector>
#include "include/solver/cfr.h"
#include "include/game/hand_eval.h"
#include "include/solver/isomorpher.h"

void preload() {
    load_hand_ranks("data/hand_ranks.dat");
}

int main() {
    preload();
    std::array<float, 169> oop_range;
    for (int i = 0; i < 169; i++) oop_range[i] = 0;
    std::array<float, 169> ip_range;
    for (int i = 0; i < 169; i++) ip_range[i] = 0;
    oop_range[0] = 1;
    ip_range[0] = 1;
    float pot = 2.0f;
    std::pair<float, float> stacks = std::make_pair(999.0f, 999.0f);
    std::vector<std::pair<float, float>> betting_sizes;
    betting_sizes.push_back(std::make_pair(0.75f, 3.0f));
    std::array<uint8_t, 3> cards = {0, 1, 2};
    CFR cfr = CFR(oop_range, ip_range, cards, pot, stacks, betting_sizes);
    cfr.buildTree();
    //cfr.run(1000);
    //test_indexer();
    //test_eval_hand();
}