#include "include/solver/isomorpher.h"
#include "include/game/hand_eval.h"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <iostream>
#include <set>
#include <cassert>
#include <array>
#include <cmath>


std::unordered_map<int, int> board_config;
std::unordered_map<int, int> board_hand_config;
std::unordered_map<int, std::unordered_map<long long, int>> full_config;


long long nCr(int n, int r) {
    if (r > n) return 0;
    if (r == 0 || r == n) return 1;
    if (r > n / 2) r = n - r;

    long long res = 1;
    for (int i = 1; i <= r; ++i) {
        res = res * (n - i + 1) / i;
    }
    return res;
}

bool suit_info_comp(std::tuple<int, int, std::vector<int>>& a, std::tuple<int, int, std::vector<int>>& b) {
    if (std::get<0>(a) == std::get<0>(b)) {
        return std::get<1>(a) > std::get<1>(b);
    }
    return (std::get<0>(a) > std::get<0>(b));
}

bool suit_info_comp2(std::tuple<int, int, int, std::vector<int>>& a, std::tuple<int, int, int, std::vector<int>>& b) {
    if (std::get<0>(a) == std::get<0>(b)) {
        if (std::get<1>(a) == std::get<1>(b)) {
            return (std::get<2>(a) > std::get<2>(b));
        } else {
            return (std::get<1>(a) > std::get<1>(b));
        }
    }
    return (std::get<0>(a) > std::get<0>(b));
}

bool suit_info_comp3(const std::array<int, 5>& a, const std::array<int, 5>& b) {
    for (int i = 0; i < 5; i++) {
        if (a[i] == b[i]) continue;
        else return (a[i] > b[i]);
    }
}

bool hand_str_comp(std::pair<int, int>& a, std::pair<int, int>& b) {
    if (a.first == b.first) {
        return a.second > b.second;
    }
    return a.first > b.first;
}

int digit(int num, int digit) {
    return (num / static_cast<int>(std::pow(10, digit))) % 10;
}

int board_config_size(int config) {
    int index = 0;
    int i = 0;
    int mult = 1;
    while (i < 4) {
        int n = digit(config, 3-i);
        if (n == 0) break;
        int j = i+1;
        while(j < 4 && digit(config, 3-j) == n) j+=1;

        int size = nCr(13, n);
        mult *= nCr(size + (j-i) - 1, (j - i));
    
        i = j;
    }
    return mult;
}

void create_board_config() {
    std::vector<int> flop_configs;
    std::vector<int> turn_configs;
    std::vector<int> river_configs;
    for (int s1 = 5; s1 >= 0; s1--) {
        for (int s2 = s1; s2 >= 0; s2--) {
            for (int s3 = s2; s3 >= 0; s3--) {
                for (int s4 = s3; s4 >= 0; s4--) {
                    if (s1 + s2 + s3 + s4 == 3) flop_configs.push_back(1000 * s1 + 100 * s2 + 10 * s3 + s4); //already sorted
                    if (s1 + s2 + s3 + s4 == 4) turn_configs.push_back(1000 * s1 + 100 * s2 + 10 * s3 + s4); //already sorted
                    if (s1 + s2 + s3 + s4 == 5) river_configs.push_back(1000 * s1 + 100 * s2 + 10 * s3 + s4); //already sorted
                }
            }
        }
    }

    std::unordered_map<int, int> offsets;
    int flop_offset = 0;
    for (const int config : flop_configs) {
        offsets[config] = flop_offset;
        flop_offset += board_config_size(config);
    }
    int turn_offset = 0;
    for (const int config : turn_configs) {
        offsets[config] = turn_offset;
        turn_offset += board_config_size(config);
    }
    int river_offset = 0;
    for (const int config : river_configs) {
        offsets[config] = river_offset;
        river_offset += board_config_size(config);
    }

    board_config = offsets;
}

int board_hand_config_size(int config) {
    int index = 0;
    int i = 0;
    int mult = 1;
    while (i < 4) {
        int b = digit(config, 7 - 2 * i);
        int h = digit(config, 6 - 2 * i);
        if (b == 0 && h == 0) break;
        int j = i+1;
        while(j < 4 && digit(config, 7 - 2 * j) == b && digit(config, 6 - 2 * j) == h) j+=1;

        int size = nCr(13, b) * nCr(13 - b, h);
        mult *= nCr(size + (j-i) - 1, (j - i));
    
        i = j;
    }
    return mult;
}

void create_board_hand_config() {
    std::vector<int> flop_configs;
    std::vector<int> turn_configs;
    std::vector<int> river_configs;
    for (int sb1 = 5; sb1 >= 0; sb1--) {
        for (int sb2 = 5; sb2 >= 0; sb2--) {
            for (int sb3 = 5; sb3 >= 0; sb3--) {
                for (int sb4 = 5; sb4 >= 0; sb4--) {
                    for (int sh1 = 2; sh1 >= 0; sh1--) {
                        for (int sh2 = 2; sh2 >= 0; sh2--) {
                            for (int sh3 = 2; sh3 >= 0; sh3--) {
                                for (int sh4 = 2; sh4 >= 0; sh4--) {
                                    if (sh1 + sh2 + sh3 + sh4 != 2) continue;
                                    if (sb1 + sb2 + sb3 + sb4 == 3) flop_configs.push_back(10000000 * sb1 + 1000000 * sh1 + 100000 * sb2 +
                                                                                            10000 * sh2 + 1000 * sb3 + 100 * sh3 + 10 * sb4 + sh4); //already sorted
                                    if (sb1 + sb2 + sb3 + sb4 == 4) turn_configs.push_back(10000000 * sb1 + 1000000 * sh1 + 100000 * sb2 +
                                                                                            10000 * sh2 + 1000 * sb3 + 100 * sh3 + 10 * sb4 + sh4); //already sorted
                                    if (sb1 + sb2 + sb3 + sb4 == 5) river_configs.push_back(10000000 * sb1 + 1000000 * sh1 + 100000 * sb2 +
                                                                                            10000 * sh2 + 1000 * sb3 + 100 * sh3 + 10 * sb4 + sh4); //already sorted
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    std::unordered_map<int, int> offsets;
    int flop_offset = 0;
    for (const int config : flop_configs) {
        offsets[config] = flop_offset;
        flop_offset += board_hand_config_size(config);
    }
    int turn_offset = 0;
    for (const int config : turn_configs) {
        offsets[config] = turn_offset;
        turn_offset += board_hand_config_size(config);
    }
    int river_offset = 0;
    for (const int config : river_configs) {
        offsets[config] = river_offset;
        river_offset += board_hand_config_size(config);
    }

    board_hand_config = offsets;
}

int config_size(const std::array<std::vector<int>, 4>& config) {
    int num_groups = config[0].size();
    int i = 0;
    int mult = 1;
    while (i < 4) {
        bool done = true;
        //true if suit has no cards
        for (int g = 0; g < num_groups; g++) done = done && (config[i][g] == 0);
        if (done) break;
        int j = i + 1;
        while (j < 4) {
            bool equal = true;
            for (int g = 0; g < num_groups; g++) {
                if (config[i][g] != config[j][g]) {
                    equal = false;
                    break;
                }
            }
            if (!equal)
                break;
            j++;
        }

        int size = 1;
        int rem_cards = 13;
        for (int k = 0; k < num_groups; k++) {
            size *= nCr(rem_cards, config[i][k]);
            rem_cards -= config[i][k];
        }
        mult *= nCr(size + (j-i) - 1, (j - i));
    
        i = j;
    }
    return mult;
}

void fill_config(int s, std::array<std::vector<int>, 4>& curr_config, 
    std::vector<std::vector<std::tuple<int, int, int, int>>>& combos, std::vector<std::array<std::vector<int>, 4>>& configs) {
    if (s == combos.size()) {
        configs.push_back(curr_config);
        return;
    }
    for (const auto combo : combos[s]) {
        std::array<std::vector<int>, 4> new_curr_config(curr_config);
        new_curr_config[0].push_back(std::get<0>(combo));
        new_curr_config[1].push_back(std::get<1>(combo));
        new_curr_config[2].push_back(std::get<2>(combo));
        new_curr_config[3].push_back(std::get<3>(combo));
        fill_config(s+1, new_curr_config, combos, configs);
    }
}

void create_config(const std::vector<int>& groups_count, int config_key) {
    int num_groups = groups_count.size();
    std::vector<std::vector<std::tuple<int, int, int, int>>> combos(num_groups);
    for (int i = 0; i < num_groups; i++) {
        int g = groups_count[i];
        for (int s1 = g; s1 >= 0; s1--) {
            for (int s2 = g - s1; s2 >= 0; s2--) {
                for (int s3 = g - s1 - s2; s3 >= 0; s3--) {
                    int s4 = g - s1 - s2 - s3;
                    if (s4 < 0) continue;
                    combos[i].push_back(std::make_tuple(s1, s2, s3, s4));
                }
            }
        }
    }

    std::vector<std::array<std::vector<int>, 4>> configs;
    std::array<std::vector<int>, 4> temp;
    fill_config(0, temp, combos, configs);

    for (int i = 0; i < configs.size(); i++) {
        std::sort(configs[i].begin(), configs[i].end(), std::greater<>());
    }
    std::sort(configs.begin(), configs.end(), std::greater<>());

    std::unordered_map<long long, int> offsets;
    int offset = 0;
    for (const auto config : configs) {
        long long key = 0; // only works if num_groups <= 4, which it always should be
        int mult = 1;
        for (int suit = 3; suit >= 0; suit--) {
            for (int group = num_groups - 1; group >= 0; group--) {
                key += mult * config[suit][group];
                mult *= 10;
            }
        }
        if (offsets.find(key) == offsets.end()) {
            offsets[key] = offset;
            offset += config_size(config);
        }
    }

    full_config[config_key] = offsets;
}

int indexer(const std::vector<uint64_t>& groups, const std::vector<int>& groups_count) {
    int config_key = 0;
    int p = 1;
    for (int i = 0; i < groups_count.size(); i++) {
        config_key += groups_count[i] * p;
        p *= 10;
    }
    if (full_config.find(config_key) == full_config.end()) create_config(groups_count, config_key);

    int num_groups = groups.size();
    
    std::vector<std::array<int, 5>> grouped_suits(4); //4 is max # of groups (+1 for index place)
    std::vector<std::array<int, 13>> ranks(4); //max 13 ranks per suit
    std::vector<int> suit_count(4);

    for (int j = 0; j < num_groups; j++) {
        uint64_t group = groups[j];
        for (int i = 51; i >= 0; i--) {
            if ((group >> i) & 1) {
                int suit = i % 4;
                grouped_suits[suit][j]++;
                ranks[suit][suit_count[suit]] = (i / 4); //ordered by group and within group orded by rank
                suit_count[suit]++;
            }
        }
    }

    for (int suit = 0; suit < 4; suit++) {
        int index = 0;
        int mult = 1;
        int num_prev_cards = 0;
        for (int g = 0; g < num_groups; g++) {
            int num_cards = grouped_suits[suit][g];
            for (int i = 0; i < num_cards; i++) {
                int rank = ranks[suit][num_prev_cards + i];
                int true_rank = rank;
                for (int r = 0; r < num_prev_cards; r++) {
                    if (rank > ranks[suit][r]) true_rank--;
                }
                index += mult * nCr(true_rank, num_cards - i);
            }
            mult *= nCr(13 - num_prev_cards, num_cards);
            num_prev_cards += num_cards;
        }
        grouped_suits[suit][num_groups] = index;
    }

    std::sort(grouped_suits.begin(), grouped_suits.end(), suit_info_comp3);
    
    int index = 0;
    int i = 0;
    int mult = 1;
    while (i < 4) {
        bool done = true;
        //true if suit has no cards
        for (int g = 0; g < num_groups; g++) done = done && (grouped_suits[i][g] == 0);
        if (done) break;
        int j = i + 1;
        while (j < 4) {
            bool equal = true;
            for (int g = 0; g < num_groups; g++) {
                if (grouped_suits[i][g] != grouped_suits[j][g]) {
                    equal = false;
                    break;
                }
            }
            if (!equal)
                break;
            j++;
        }

        for (int k = i; k < j; k++) {
            index += mult * nCr(grouped_suits[k][num_groups] + (j - k) - 1, (j - k));
        }

        int size = 1;
        int rem_cards = 13;
        for (int k = 0; k < num_groups; k++) {
            size *= nCr(rem_cards, grouped_suits[i][k]);
            rem_cards -= grouped_suits[i][k];
        }
        mult *= nCr(size + (j-i) - 1, (j - i));
    
        i = j;
    }

    long long key = 0; // only works if num_groups <= 4, which it always should be
    int key_mult = 1;
    for (int suit = 3; suit >= 0; suit--) {
        for (int group = num_groups - 1; group >= 0; group--) {
            key += key_mult * grouped_suits[suit][group];
            key_mult *= 10;
        }
    }

    return full_config[config_key][key] + index;
}

void test_indexer() {
    std::unordered_map<int, int> map;
    int count = 0;
    int max_idx = 0;
    for (int c1 = 0; c1 < 52; c1++) {
        std::cout << c1 << "\n";
        for (int c2 = c1+1; c2 < 52; c2++) {
            for (int c3=c2+1; c3 < 52; c3++) {
                uint64_t board = 0;
                board |= (1ULL << c1);
                board |= (1ULL << c2);
                board |= (1ULL << c3);
                
                for (int c4 = 0; c4 < 52; c4++) {
                    if (c4 == c1 || c4 == c2 || c4 == c3) continue;

                    uint64_t turn = 0;
                    turn |= (1ULL << c4);

                    /*
                    for (int h1 = 0; h1 < 52; h1++) {
                        if (h1 == c1 || h1 == c2 || h1 == c3 || h1 == c4) continue;
                        for (int h2 = h1+1; h2 < 52; h2++) {
                            if (h2 == c1 || h2 == c2 || h2 == c3 || h2 == c4) continue;
                            uint64_t hand = 0;
                            hand |= (1ULL << h1);
                            hand |= (1ULL << h2);

                            int index = indexer({board, turn, hand}, {3, 1, 2});
                            max_idx = std::max(max_idx, index);
                            if (map.find(index) == map.end()) {
                                count++;
                                map[index] = 1;
                            }
                        }
                    }
                    */
                    int index = indexer({board, turn}, {3, 1});
                    max_idx = std::max(max_idx, index);
                    if (map.find(index) == map.end()) {
                        count++;
                        map[index] = 1;
                    }
                }
            }
        }
    } 
    std::cout << count << " " << max_idx;
}


int index_board(uint64_t board) {
    if (board_config.empty()) create_board_config();
    std::vector<std::tuple<int, int, std::vector<int>>> suit_info = {std::make_tuple(0, 0, std::vector<int>{}), 
                                                                    std::make_tuple(0, 0, std::vector<int>{}),
                                                                    std::make_tuple(0, 0, std::vector<int>{}),
                                                                    std::make_tuple(0, 0, std::vector<int>{})};
    for (int i = 51; i >= 0; i--) {
        if ((board >> i) & 1) {
            std::get<0>(suit_info[i % 4])++;
            std::get<2>(suit_info[i % 4]).push_back(i / 4); //alraedy sorted by rank
        }
    }

    for (auto& info : suit_info) {
        int index = 0;
        int n = std::get<0>(info);
        for (int i = 0; i < n; i++) {
            index += nCr(std::get<2>(info)[i], n - i);
        }
        std::get<1>(info) = index;
    }

    std::sort(suit_info.begin(), suit_info.end(), suit_info_comp);

    int index = 0;
    int i = 0;
    int mult = 1;
    while (i < 4) {
        if (std::get<0>(suit_info[i]) == 0) break;
        int j = i+1;
        while(j < 4 && std::get<0>(suit_info[j]) == std::get<0>(suit_info[i])) j+=1;

        for (int k = i; k < j; k++) {
            index += mult * nCr(std::get<1>(suit_info[k]) + (j - k) - 1, (j - k));
        }

        int size = nCr(13, std::get<0>(suit_info[i]));
        mult *= nCr(size + (j-i) - 1, (j - i));
    
        i = j;
    }
    int config_key = 1000 * std::get<0>(suit_info[0]) + 100 * std::get<0>(suit_info[1]) + 10 * std::get<0>(suit_info[2]) + std::get<0>(suit_info[3]);
    return (board_config[config_key] + index);
}


int hand_board_indexer(uint64_t hand, uint64_t board) {
    if (board_hand_config.empty()) create_board_hand_config();
    std::vector<std::tuple<int, int, int, std::vector<int>>> suit_info = {std::make_tuple(0, 0, 0, std::vector<int>{}), 
                                                                    std::make_tuple(0, 0, 0, std::vector<int>{}),
                                                                    std::make_tuple(0, 0, 0, std::vector<int>{}),
                                                                    std::make_tuple(0, 0, 0, std::vector<int>{})};
    for (int i = 51; i >= 0; i--) {
        if ((board >> i) & 1) {
            std::get<0>(suit_info[i % 4])++;
            std::get<3>(suit_info[i % 4]).push_back(i / 4); //already sorted by rank
        }
    }

    for (int i = 51; i >= 0; i--) {
        if ((hand >> i) & 1) {
            std::get<1>(suit_info[i % 4])++;
            std::get<3>(suit_info[i % 4]).push_back(i / 4); //already sorted by rank
        }
    }

    for (auto& info : suit_info) {
        int index = 0;
        int b = std::get<0>(info);
        int h = std::get<1>(info);
        std::vector<int> ranks = std::get<3>(info);
        for (int i = 0; i < b; i++) {
            index += nCr(ranks[i], b - i);
        }

        int mult = nCr(13, b);
        for (int i = 0; i < h; i++) {
            int true_rank = ranks[b+i];
            for (int j = 0; j < b; j++) {
                if (ranks[b+i] > ranks[j]) true_rank--;
            }
            index += mult * nCr(true_rank, h - i);
        }
        std::get<2>(info) = index;
    }

    std::sort(suit_info.begin(), suit_info.end(), suit_info_comp2);
    //std::cout << std::get<2>(suit_info[0]) << "\n";

    int index = 0;
    int i = 0;
    int mult = 1;
    while (i < 4) {
        if (std::get<0>(suit_info[i]) == 0 && std::get<1>(suit_info[i]) == 0) break;
        int j = i+1;
        while(j < 4 && std::get<0>(suit_info[j]) == std::get<0>(suit_info[i]) && std::get<1>(suit_info[j]) == std::get<1>(suit_info[i])) j+=1;

        for (int k = i; k < j; k++) {
            index += mult * nCr(std::get<2>(suit_info[k]) + (j - k) - 1, (j - k));
        }

        int size = nCr(13, std::get<0>(suit_info[i])) * nCr(13 - std::get<0>(suit_info[i]), std::get<1>(suit_info[i]));
        mult *= nCr(size + (j-i) - 1, (j - i));
    
        i = j;
    }
    
    int config_key = 0;
    for (int i = 0; i < 4; i++) {
        config_key += static_cast<int>(std::pow(10, 7 - 2 * i)) * std::get<0>(suit_info[i]) + 
                        static_cast<int>(std::pow(10, 6 - 2 * i)) * std::get<1>(suit_info[i]);
    }
    return board_hand_config.at(config_key) + index;
}


std::pair<uint64_t, uint8_t> board_indexer(uint64_t board) {
    std::set<int> indicies;
    uint64_t canon_cards = 0;
    for (int card_one = 0; card_one < 52; card_one++) {
        if ((board >> card_one) & 1) continue;
        uint64_t new_board = board | (1ULL << card_one);
        int index = index_board(new_board);
        if (indicies.find(index) == indicies.end()) {
            canon_cards |= (1ULL << card_one);
            indicies.insert(index);
        }
    }
    return std::make_pair(canon_cards, (uint8_t)indicies.size());
}


std::pair<std::vector<int>, int> hand_indexer(std::array<uint64_t, 3> board) {
    std::unordered_map<int, int> index_to_local;
    int local = 0;
    int global = 0;
    std::vector<int> global_to_local(1326);

    std::vector<uint64_t> new_board;
    for (const auto group : board) {
        if (group != 0) new_board.push_back(group);
    }
    std::vector<int> group_size;
    if (new_board.size() == 1) group_size.assign({3, 2});
    else if (new_board.size() == 2) group_size.assign({3, 1, 2});
    else if (new_board.size() == 3) group_size.assign({3, 1, 1, 2});

    for (int card_one = 1; card_one < 52; card_one++) {
        for (int card_two = 0; card_two < card_one; card_two++) {
            bool skip = false;
            for (const auto b : new_board) {
                if (((b >> card_one) & 1) || ((b >> card_two) & 1)) {
                    skip = true;
                    break;
                }
            }
            if (skip) {
                global++;
                continue;
            }
            uint64_t hand = 0;
            hand |= (1ULL << card_one);
            hand |= (1ULL << card_two);
            new_board.push_back(hand);
            int hand_index = indexer(new_board, group_size);
            new_board.pop_back();
            //std::cout << hand_index << "\n";
            if (index_to_local.find(hand_index) == index_to_local.end()) {
                global_to_local[global] = local;
                index_to_local[hand_index] = local;
                local++;
            } else {
                int local_index = index_to_local[hand_index];
                global_to_local[global] = local_index;
            }
            global++;
        }
    }
    //std::cout << local << "\n";
    return std::make_pair(global_to_local, local);
}

std::pair<std::vector<int>, std::vector<int>> river_hand_indexer(std::array<uint64_t, 3> board) {
    std::unordered_map<int, int> index_to_local;
    int local = 0;
    int global = 0;
    std::vector<int> global_to_local(1326);
    std::vector<std::pair<int, int>> hand_strength_sorted(1326);
    std::vector<int> hand_strength(1326);

    std::vector<uint64_t> new_board;
    for (const auto group : board) {
        if (group != 0) new_board.push_back(group);
    }
    std::vector<int> group_size;
    if (new_board.size() == 1) group_size.assign({3, 2});
    else if (new_board.size() == 2) group_size.assign({3, 1, 2});
    else if (new_board.size() == 3) group_size.assign({3, 1, 1, 2});

    uint64_t full_board = 0;
    for (const auto group : new_board) full_board |= group;

    for (int card_one = 1; card_one < 52; card_one++) {
        for (int card_two = 0; card_two < card_one; card_two++) {
            bool skip = false;
            for (const auto b : new_board) {
                if (((b >> card_one) & 1) || ((b >> card_two) & 1)) {
                    skip = true;
                    break;
                }
            }
            if (skip) {
                hand_strength_sorted[global] = std::make_pair(-1, global);
                hand_strength[global] = -1;
                global++;
                continue;
            }
            uint64_t hand = 0;
            hand |= (1ULL << card_one);
            hand |= (1ULL << card_two);
            new_board.push_back(hand);
            int hand_index = indexer(new_board, group_size);
            new_board.pop_back();
            //std::cout << hand_index << "\n";
            if (index_to_local.find(hand_index) == index_to_local.end()) {
                global_to_local[global] = local;
                index_to_local[hand_index] = local;
                int hand_str = evaluate_hand(hand, full_board);
                hand_strength_sorted[global] = std::make_pair(hand_str, global);
                hand_strength[global] = hand_str;
                local++;
            } else {
                int local_index = index_to_local[hand_index];
                int hand_str = evaluate_hand(hand, full_board);
                hand_strength_sorted[global] = std::make_pair(hand_str, global);
                hand_strength[global] = hand_str;
                global_to_local[global] = local_index;
            }
            global++;
        }
    }
    sort(hand_strength_sorted.begin(), hand_strength_sorted.end(), hand_str_comp);
    return std::make_pair(global_to_local, hand_strength);
}


std::pair<std::unordered_map<int, int>, std::vector<BoardInfo>> get_flop_info(uint64_t flop) {
    int local = 0;
    std::unordered_map<int, int> global_board_to_local;
    std::vector<BoardInfo> local_to_board;
    std::vector<uint64_t> last_cards; //cards in last deal that res ml';
    ;'ult in same index
    std::vector<std::vector<int>> hand_strengths; //[local_board_index][global_hand_index]

    //flop
    std::pair<std::vector<int>, int> hand_info = hand_indexer({flop});
    global_board_to_local[indexer({flop}, {3})] = local; 
    local_to_board.push_back(BoardInfo(local, hand_info.first, hand_info.second));
    last_cards.push_back(flop);
    hand_strengths.push_back(std::vector<int>(hand_info.second, -1));
    local++;
    int total = 0;
    //turn
    for (int c4=0; c4 < 52; c4++) {
        if ((flop >> c4) & 1) continue;
        uint64_t turn = (1ULL << c4);
        total++;

        int index = 1755 + indexer({flop, turn}, {3, 1}); //displacement of flop index to avoid collisions
        auto it = global_board_to_local.find(index);

        if (it != global_board_to_local.end()) {
            int local_index = global_board_to_local.at(index);
            assert(last_cards.size() > local_index);
            last_cards[local_index] |= (1ULL << c4);
            continue;
        }

        std::pair<std::vector<int>, int> hand_info = hand_indexer({flop, turn});
        global_board_to_local[index] = local;
        local_to_board.push_back(BoardInfo(local, hand_info.first, hand_info.second));
        last_cards.push_back(turn);
        hand_strengths.push_back(std::vector<int>(hand_info.second, -1));
        local++;
    }
    std::cout << local;
    int turn_local = local;
    //river
    for (int c4=0; c4 < 52; c4++) {
        if ((flop >> c4) & 1) continue;
        uint64_t turn = (1ULL << c4);
        for (int c5=0; c5 < 52; c5++) {
            if ((flop >> c5) & 1 || (turn >> c5) & 1) continue;
            uint64_t river = (1ULL << c5);

            int index = 0;
            try {
                index = 1755 + 63193 + indexer({flop, turn, river}, {3, 1, 1});
            } catch (const std::exception& e) {
                std::cout << "here1";
            }
            auto it = global_board_to_local.find(index);

            if (it != global_board_to_local.end()) {
                int local_index = global_board_to_local.at(index);
                assert(last_cards.size() > local_index);
                last_cards[local_index] |= (1ULL << c5);
                std::pair<std::vector<int>, std::vector<int>> hand_info;
                try {
                    hand_info = river_hand_indexer({flop, turn, river});
                } catch (const std::exception& e) {
                    std::cout << "here1";
                }

                //to account for different isomorphic hands when evaluing hand strength, we take the max for each hand of all isomorphic boards
                //in regards to some boards blocking hands from even being possible when evaluation
                for (int i = 0; i < hand_info.second.size(); i++) {
                    hand_strengths[local_index][i] = std::max(hand_strengths[local_index][i], hand_info.second[i]);
                }
                continue;
            }

            std::pair<std::vector<int>, std::vector<int>> hand_info;
            try {
                hand_info = river_hand_indexer({flop, turn, river});
            } catch (const std::exception& e) {
                std::cout << "here1";
            }
            global_board_to_local[index] = local;
            local_to_board.push_back(BoardInfo(local, hand_info.first, hand_info.second.size()));
            last_cards.push_back(river);
            hand_strengths.push_back(hand_info.second);
            local++;
        }
    }

    for (int i = turn_local; i < local; i++) {
        std::vector<std::pair<int, int>> hand_strength_sorted;
        for (int j = 0; j < hand_strengths[i].size(); j++) {
            hand_strength_sorted.push_back(std::make_pair(hand_strengths[i][j], j));
        }
        std::sort(hand_strength_sorted.begin(), hand_strength_sorted.end(), hand_str_comp);
        local_to_board[i].hand_strength_sorted = hand_strength_sorted;
        local_to_board[i].hand_strength = hand_strengths[i];
    }


    for (int i = 0; i < local; i++) {
        local_to_board[i].indexing_cards = last_cards[i];
    }

    std::cout << local << "\n";
    return std::make_pair(global_board_to_local, local_to_board);
}


void test_board_indexer() {
    std::unordered_map<int, int> map;
    for (int c1 = 0; c1 < 52; c1++) {
        for (int c2 = c1+1; c2 < 52; c2++) {
            for (int c3=c2+1; c3 < 52; c3++) {
                uint64_t board = 0;
                board |= (1ULL << c1);
                board |= (1ULL << c2);
                board |= (1ULL << c3);
                int index = index_board(board);
                if (map.find(index) != map.end()) continue;
                map[index] = 1;
            }
        }
    }   
}

void test_board_hand_indexer() {
    std::unordered_map<int, int> map;
    for (int c1 = 0; c1 < 52; c1++) {
        for (int c2 = c1+1; c2 < 52; c2++) {
            for (int c3=c2+1; c3 < 52; c3++) {
                uint64_t board = 0;
                board |= (1ULL << c1);
                board |= (1ULL << c2);
                board |= (1ULL << c3);
                int index = index_board(board);
                if (map.find(index) != map.end()) continue;
                map[index] = 1;
            }
        }
    }   
}
