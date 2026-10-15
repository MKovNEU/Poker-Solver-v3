#pragma once

#include <array>
#include <iosfwd>
#include <string>
#include <vector>
#include "include/solver/solver.h"

// walks a solved tree and shows the strategy, ranges and EVs of any spot in it, turn and river cards are entered as
// real cards, isomorphic ones are mapped onto the stored representative (hands are translated back for display)
class Explorer {
public:
    explicit Explorer(Solver& solver);

    void run(std::istream& in, std::ostream& out); // interactive loop
    void reset();
    void back();
    bool take(const std::string& token, std::string& error); // action (index or name) or dealt card

    void print_node(std::ostream& out) const;
    void print_hands(std::ostream& out, bool with_ev);
    void print_grid(std::ostream& out, int action) const;
    void print_range(std::ostream& out, int player) const;
    void print_ev(std::ostream& out);
    void write_json(const std::string& path);

    // one line of JSON for the web UI: the spot, its actions and every combo still in each range,
    // the acting player's combos with their strategy
    void write_node(std::ostream& out) const;
    // one line of JSON: EV of the combos write_node lists, in the same order
    void write_ev(std::ostream& out);

    uint32_t node() const { return path_.back().node; }

private:
    struct Frame {
        uint32_t node;
        std::array<int, 4> perm;      // real suit -> stored suit
        std::vector<float> reach[2];  // in stored hand order
        std::vector<int> board;       // real cards
        std::vector<std::string> line;
    };

    Solver& solver_;
    std::vector<Frame> path_;

    int real_card(int stored) const;
    int real_combo(int p, int h) const;
    std::vector<float> strategy() const;
    std::vector<float> hand_ev(int p); // EV of each hand at the current node, pot share included, NaN if impossible
    double frequency(const std::vector<float>& sigma, int action) const;
    std::vector<int> listed_hands(int p) const; // hands still in p's range, highest combo first
};
