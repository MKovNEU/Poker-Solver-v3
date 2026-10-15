#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>
#include "include/preflop/equity.h"
#include "include/preflop/game.h"
#include "include/solver/solver.h"

// Vector-form DCFR over the 169 hand classes: with no board yet the combos of a class are interchangeable, so one
// strategy per class is exact, and every traversal carries each seat's reach per class. Card removal between the
// traverser and each opponent is exact through 169 x 169 matrices of compatible combo pairs and their equity shares,
// opponents are independent of each other (the usual simplification, exact heads-up).
class PreflopSolver {
public:
    PreflopSolver(const PreflopConfig& config, const PreflopEquity& equity);

    // runs DCFR until the target or the iteration limit, returns the last exploitability (mbb per hand)
    double solve(bool verbose = true);
    void iterate(int t);                   // one DCFR iteration, every seat updates once in turn
    double exploitability();               // mbb per hand: best-response gains summed over seats, per seat
    std::vector<double> expected_values(); // bb per hand of every seat under the average strategies

    const PreflopConfig& config() const { return config_; }
    const PreflopTree& tree() const { return tree_; }
    int iterations_done() const { return iterations_; }
    int threads() const { return threads_; }
    uint64_t storage_bytes() const { return tree_.storage * 2 * sizeof(float); }

    // average strategy of an action node, layout [action][class]
    void average_strategy(uint32_t node, float* out) const;

    // saves the average strategy (16 bit), the tree is rebuilt from the stored config on load
    void save(const std::string& path) const;
    static std::unique_ptr<PreflopSolver> load(const std::string& path, const PreflopEquity& equity);

private:
    enum class Mode { CFR, BEST_RESPONSE, AVERAGE };
    using Reach = std::array<const float*, MAX_PLAYERS>;

    PreflopConfig config_;
    PreflopTree tree_;
    int threads_ = 1;
    int iterations_ = 0;

    // [i][j] for a hand of class i against class j, divided by 1225 * combos of i: compatible combo pairs, and the
    // equity share of the OOP / IP player summed over them, one share pair per realization level
    std::vector<float> compat_;
    std::vector<std::vector<float>> share_oop_, share_ip_;
    std::vector<uint16_t> terminal_level_;

    std::vector<float> regret_, cum_;

    void build_matrices(const PreflopEquity& equity);
    double root_value(Mode mode, int p);
    template <Mode M> void traverse(uint32_t id, int p, const Reach& reach, float* out, const Discount& d, int depth);
    void terminal(const PNode& n, int p, const Reach& reach, float* out) const;
    void fold_value(float lost, int p, const Reach& reach, float* out) const;
};
