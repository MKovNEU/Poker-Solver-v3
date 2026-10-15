#pragma once

#include <cstdint>

// strength of the best 5 card hand among 5..7 cards (bitmask over card indices), higher is better
// layout: category << 20 | five 4-bit tiebreak ranks, category 0 = high card ... 8 = straight flush
uint32_t evaluate_hand(uint64_t cards);

const char* hand_category_name(uint32_t strength);
