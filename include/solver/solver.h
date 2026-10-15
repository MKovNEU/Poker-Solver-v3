#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "include/core/arena.h"
#include "include/core/cards.h"
#include "include/game/config.h"
#include "include/game/tree.h"

// hands of one player that are in the range and don't touch the starting board, in combo order,
// every vector the solver touches is indexed by position in this list (not by the 1326 combo index)
struct HandList {
    int size = 0;
    std::vector<uint16_t> combo;
    std::vector<uint8_t> c1, c2; // c1 > c2
    std::vector<uint64_t> mask;
    std::vector<float> weight;   // range weight
    std::vector<int32_t> same;   // index of the same combo in the opponent's list, -1 if absent
    std::array<std::vector<uint16_t>, NUM_SUIT_SWAPS> perm; // index of the hand after a suit swap (valid swaps only)
    std::array<std::vector<uint16_t>, NUM_CARDS> with_card; // hands holding a card
    std::array<int16_t, NUM_COMBOS> index;                  // combo -> index, -1 if absent
};

// DCFR discounting of one iteration
struct Discount {
    float alpha; // positive regrets
    float beta;  // negative regrets
    float gamma; // cumulative strategy
    uint32_t iteration;
    static Discount at(int iteration);
};

// one entry of a river board's hand ranking
struct RankedHand {
    uint32_t strength;
    uint16_t hand;
    uint8_t c1, c2;
};

// a run of tied hands in one player's ranking: entries [previous end, end) share a strength, the opponent's
// ranking entries [0, lt) are weaker and [0, le) are not stronger
struct ShowdownGroup {
    uint16_t end, lt, le;
};

class Solver {
public:
    explicit Solver(const GameConfig& config);

    // runs DCFR until the exploitability target or the iteration limit, returns the last exploitability (% of pot)
    double solve(bool verbose = true);
    void iterate(int t); // one DCFR iteration, both players update once (alternating)
    double exploitability(); // chips, mean best-response gain of the two players
    std::array<double, 2> expected_values(); // chips each player wins of the starting pot under the average strategies

    const GameConfig& config() const { return config_; }
    const Tree& tree() const { return tree_; }
    const HandList& hands(int p) const { return hands_[p]; }
    int threads() const { return threads_; }
    int iterations_done() const { return iterations_; }
    bool swap_ok(int swap) const { return swap_ok_[swap]; }
    bool compressed() const { return compress_; }

    // average strategy of an action node, layout [action][hand]
    void average_strategy(uint32_t node, float* out) const;
    // counterfactual values of player p's hands at `node` under the average strategies, the opponent reaching it with opp_reach
    void node_values(uint32_t node, int p, const float* opp_reach, float* out);
    // total opponent reach that shares no card with each of player p's hands or the board
    void compatible_mass(int p, const float* opp_reach, uint64_t board, float* out) const;

    // bytes for regrets + cumulative strategy
    uint64_t storage_bytes() const { return tree_.storage_size * 2 * (compress_ ? sizeof(uint16_t) : sizeof(float)); }

    // saves the average strategy (16 bit), the tree is rebuilt from the stored config on load
    void save(const std::string& path) const;
    static std::unique_ptr<Solver> load(const std::string& path);

private:
    enum class Mode { CFR, BEST_RESPONSE, AVERAGE };

    GameConfig config_;
    int threads_ = 1;
    int iterations_ = 0;
    bool compress_ = false;
    HandList hands_[2];
    bool swap_ok_[NUM_SUIT_SWAPS] = {};
    Tree tree_;
    double norm_ = 0.0; // total weight of compatible (oop, ip) hand pairs

    // river hand rankings (ascending strength, hands touching the board left out) and their tie groups,
    // both indexed [ranking * 2 + player]
    std::vector<int32_t> ranking_of_board_; // board id -> ranking, -1 for flop / turn boards
    std::vector<uint64_t> ranking_offset_;
    std::vector<uint32_t> ranking_size_;
    std::vector<RankedHand> rankings_;
    std::vector<uint64_t> group_offset_;
    std::vector<uint32_t> group_count_;
    std::vector<ShowdownGroup> groups_;

    // hand orbits under each of the tree's suit symmetries, [symmetry * 2 + player], orbits of one hand left out
    struct Orbits {
        std::vector<uint32_t> begin; // orbit i is members[begin[i] .. begin[i + 1])
        std::vector<uint16_t> members;
    };
    std::vector<Orbits> orbits_;

    // per action node [action][hand] blocks at Node::offset, either 32 bit floats or fp16 scaled per node
    std::vector<float> regret_, cum_strategy_;
    std::vector<uint16_t> regret16_, cum16_;
    std::vector<float> regret_scale_, cum_scale_; // indexed by node id

    std::vector<PaddedArena> arenas_;

    void init_hands();
    void build_rankings();
    void build_orbits();
    void ensure_storage(bool with_regrets);
    void symmetrize(uint32_t symmetry, int p, float* values) const;
    void average_into(const Node& n, float* sigma, float* sum) const;
    uint32_t node_id(const Node& n) const { return (uint32_t)(&n - tree_.nodes.data()); }
    // rounding noise seed of one fp16 block update
    uint32_t seed(const Node& n, const Discount& d, uint32_t stream) const {
        return node_id(n) * 0x9E3779B9u ^ d.iteration * 0x85EBCA6Bu ^ stream * 0xC2B2AE35u;
    }
    Arena& arena();

    template <Mode M> void run(uint32_t root, int p, const float* opp_reach, float* out, const Discount& d);
    template <Mode M> void traverse(uint32_t id, int p, const float* opp_reach, float* out, const Discount& d);
    template <Mode M> void own_node(const Node& n, int p, const float* opp_reach, float* out, const Discount& d);
    template <Mode M> void opp_node(const Node& n, int p, const float* opp_reach, float* out, const Discount& d);
    template <Mode M> void chance_node(const Node& n, int p, const float* opp_reach, float* out, const Discount& d);
    void fold_node(const Node& n, int p, const float* opp_reach, float* out) const;
    // fold_out / fold: optionally also evaluates player p folding against the same reach (shares the card sums)
    void showdown_node(const Node& n, int p, const float* opp_reach, float* out, float* fold_out = nullptr,
                       const Node* fold = nullptr);
};
