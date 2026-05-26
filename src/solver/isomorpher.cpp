#include "include/solver/isomorpher.h"
#include "include/game/hand_eval.h"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <iostream>
#include <set>

std::unordered_map<int, int> board_config;
std::unordered_map<int, int> board_hand_config;


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


std::pair<std::vector<int>, int> hand_indexer(uint64_t board) {
    std::unordered_map<int, int> index_to_local;
    int local = 0;
    int global = 0;
    std::vector<int> global_to_local(1326);
    for (int card_one = 0; card_one < 51; card_one++) {
        for (int card_two = card_one+1; card_two < 52; card_two++) {
            if (((board >> card_one) & 1) || ((board >> card_two) & 1)) {
                global++;
                continue; //card in board
            }
            uint64_t hand = 0;
            hand |= (1ULL << card_one);
            hand |= (1ULL << card_two);
            int hand_index = hand_board_indexer(hand, board);
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

std::pair<std::vector<int>, std::vector<int>> river_hand_indexer(uint64_t board) {
    std::unordered_map<int, int> index_to_local;
    int local = 0;
    int global = 0;
    std::vector<int> global_to_local(1326);
    std::vector<int> hand_strength;
    for (int card_one = 0; card_one < 51; card_one++) {
        for (int card_two = card_one+1; card_two < 52; card_two++) {
            if (((board >> card_one) & 1) || ((board >> card_two) & 1)) {
                global++;
                continue; //card in board
            }
            uint64_t hand = 0;
            hand |= (1ULL << card_one);
            hand |= (1ULL << card_two);
            int hand_index = hand_board_indexer(hand, board);
            //std::cout << hand_index << "\n";
            if (index_to_local.find(hand_index) == index_to_local.end()) {
                global_to_local[global] = local;
                index_to_local[hand_index] = local;
                int hand_str = evaluateHand(hand, board);
                hand_strength.push_back(hand_str);
                local++;
            } else {
                int local_index = index_to_local[hand_index];
                global_to_local[global] = local_index;
            }
            global++;
        }
    }
    //std::cout << local << "\n";
    return std::make_pair(global_to_local, hand_strength);
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


//returns # of isomorphic boards + hands and global -> local board index mapping
std::pair<std::unordered_map<int, BoardInfo>, int> get_flop_info(uint64_t flop) {
    int total = 0;
    int local = 0;
    std::unordered_map<int, BoardInfo> global_board_to_local;

    //flop
    std::pair<std::vector<int>, int> hand_info = hand_indexer(flop);
    global_board_to_local[index_board(flop)] = BoardInfo(local, hand_info.first, hand_info.second);
    total += hand_indexer(flop).second;
    local++;

    //turn
    for (int c4=0; c4 < 52; c4++) {
        if ((flop >> c4) & 1) continue;
        flop |= (1ULL << c4);

        int index = 1755 + index_board(flop); //displacement of flop index to avoid collisions
        auto it = global_board_to_local.find(index);

        if (it != global_board_to_local.end()) {
            flop &= ~(1ULL << c4);
            continue;
        }

        std::pair<std::vector<int>, int> hand_info = hand_indexer(flop);
        global_board_to_local[index] = BoardInfo(local, hand_info.first, hand_info.second);
        total += hand_indexer(flop).second;
        local++;

        flop &= ~(1ULL << c4);
    }

    //river
    for (int c4=0; c4 < 52; c4++) {
        if ((flop >> c4) & 1) continue;
        flop |= (1ULL << c4);
        for (int c5=c4+1; c5 < 52; c5++) {
            if ((flop >> c5) & 1) continue;
            flop |= (1ULL << c5);

            int index = 1755 + 16432 + index_board(flop);
            auto it = global_board_to_local.find(index);

            if (it != global_board_to_local.end()) {
                flop &= ~(1ULL << c5);
                continue;
            }
            std::vector<int> full_hand_eval(flop);
            std::pair<std::vector<int>, std::vector<int>> hand_info = river_hand_indexer(flop);
            global_board_to_local[index] = BoardInfo(local, hand_info.first, hand_info.second, hand_info.second.size());
            total += hand_indexer(flop).second;
            local++;

            flop  &= ~(1ULL << c5);
        }
        flop &= ~(1ULL << c4);
    }

    return std::make_pair(global_board_to_local, total);
}
