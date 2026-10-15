#pragma once

#include <array>
#include <iosfwd>
#include <string>
#include <vector>
#include "include/preflop/solver.h"

// walks a solved preflop tree: strategies and ranges of every seat along a line, and at a heads-up flop the postflop
// solver's input (pot, stack and both ranges)
class PreflopExplorer {
public:
    explicit PreflopExplorer(PreflopSolver& solver);

    void run(std::istream& in, std::ostream& out); // interactive loop
    void reset();
    void back();
    bool take(const std::string& token, std::string& error); // action by index or name

    void print_node(std::ostream& out) const;
    void print_grid(std::ostream& out) const;          // the acting seat's strategy, 13x13 per action
    void print_range(std::ostream& out, int seat) const;
    std::string postflop_config() const;               // empty unless the line reaches a flop with chips behind
    std::string range_text(int seat) const;            // PioSolver syntax, class weights = reach

    // one line of JSON for the web UI
    void write_node(std::ostream& out) const;

    uint32_t node() const { return path_.back().node; }

private:
    struct Frame {
        uint32_t node;
        std::array<std::vector<float>, MAX_PLAYERS> reach; // per class, the chance each combo plays to here
        std::vector<std::string> line;
    };

    PreflopSolver& solver_;
    std::vector<std::string> names_;
    std::vector<Frame> path_;

    std::vector<float> strategy() const;
    double frequency(const std::vector<float>& sigma, int action) const; // over the acting seat's range
    int seat_of(const std::string& name) const;
};
