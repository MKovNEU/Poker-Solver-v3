#include <array>

std::array<float, 1326> convert(std::array<float, 169> preflop_range);

std::array<float, 1326> update_range(const std::array<float, 1326>& range, uint64_t cards, bool is_canon);

std::array<int, 100 * 1326> hand_blockers();