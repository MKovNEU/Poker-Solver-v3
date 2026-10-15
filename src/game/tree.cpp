#include "include/game/tree.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

// all the game logic lives here, it runs once, the solver only walks the finished arrays

namespace {

constexpr double EPS = 1e-6;

struct BuildState {
    int street;
    int player;
    double pot;            // starting pot + every bet so far, current street included
    double stack[2];
    double bet[2];         // chips put in on the current street
    double last_raise;     // size of the last bet / raise increment (min-raise rule)
    int aggressions;       // bets + raises on the current street
    uint64_t board;
    uint32_t board_id;
    uint64_t suit_sig[4];  // ranks of each suit, one 13 bit group per deal (initial board, turn, river)
    int groups;
};

struct Candidate {
    ActionKind kind;
    double to; // total street bet after the action
};

class Builder {
public:
    Builder(Tree& tree, const GameConfig& cfg, const int num_hands[2], const bool swap_ok[NUM_SUIT_SWAPS])
        : tree_(tree), cfg_(cfg) {
        num_hands_[0] = num_hands[0];
        num_hands_[1] = num_hands[1];
        for (int s = 0; s < NUM_SUIT_SWAPS; s++) swap_ok_[s] = swap_ok[s] && cfg.isomorphism;
    }

    void run() {
        tree_ = Tree{};
        BuildState s{};
        s.street = (int)cfg_.board.size() - 3;
        s.player = 0;
        s.pot = cfg_.pot;
        s.stack[0] = s.stack[1] = cfg_.stack;
        s.board = cards_mask(cfg_.board);
        s.board_id = board_id(s.board);
        for (int c : cfg_.board) s.suit_sig[card_suit(c)] |= 1ULL << card_rank(c);
        s.groups = 1;

        tree_.symmetries.push_back({0, 1, 2, 3});
        alloc_nodes(1);
        set_meta(0, 0, ActionKind::ROOT, 0.0, 0, s);
        if (s.stack[0] <= EPS) build_street_end(0, s);
        else build_action(0, s);
    }

private:
    Tree& tree_;
    const GameConfig& cfg_;
    int num_hands_[2];
    bool swap_ok_[NUM_SUIT_SWAPS];

    uint32_t alloc_nodes(int count) {
        uint32_t first = (uint32_t)tree_.nodes.size();
        if ((uint64_t)first + count > 0xFFFFFFF0ULL) throw std::runtime_error("tree too large");
        tree_.nodes.resize(first + count);
        tree_.meta.resize(first + count);
        return first;
    }

    uint32_t board_id(uint64_t mask) {
        auto it = tree_.board_ids.find(mask);
        if (it != tree_.board_ids.end()) return it->second;
        uint32_t id = (uint32_t)tree_.boards.size();
        tree_.boards.push_back({mask, std::popcount(mask)});
        tree_.board_ids.emplace(mask, id);
        return id;
    }

    // two suits are interchangeable when they've had identical ranks dealt on every street so far
    // and both ranges are symmetric under exchanging them
    SuitClasses suit_classes(const BuildState& s) const {
        SuitClasses classes = {0, 1, 2, 3};
        for (int suit = 1; suit < 4; suit++) {
            for (int t = 0; t < suit; t++) {
                if (s.suit_sig[t] == s.suit_sig[suit] && swap_ok_[swap_index(t, suit)]) {
                    classes[suit] = (uint8_t)t;
                    break;
                }
            }
        }
        return classes;
    }

    uint32_t symmetry_id(const BuildState& s) {
        const SuitClasses classes = suit_classes(s);
        for (uint32_t i = 0; i < tree_.symmetries.size(); i++) {
            if (tree_.symmetries[i] == classes) return i;
        }
        tree_.symmetries.push_back(classes);
        return (uint32_t)tree_.symmetries.size() - 1;
    }

    void set_meta(uint32_t id, uint32_t parent, ActionKind kind, double amount, int card, const BuildState& s) {
        NodeMeta& m = tree_.meta[id];
        m.parent = parent;
        m.action = kind;
        m.card = (uint8_t)card;
        m.street = (uint8_t)s.street;
        m.amount = (float)amount;
        m.pot = (float)s.pot;
        m.stack[0] = (float)s.stack[0];
        m.stack[1] = (float)s.stack[1];
    }

    // payoff of the winner: half the starting pot plus everything the loser put in after it
    double winner_gain(const BuildState& s, int loser) const { return cfg_.pot / 2.0 + (cfg_.stack - s.stack[loser]); }

    void make_terminal(uint32_t id, NodeType type, int player, double payoff, const BuildState& s) {
        Node& n = tree_.nodes[id];
        n = Node{};
        n.type = type;
        n.player = (uint8_t)player;
        n.board = s.board_id;
        n.payoff = (float)payoff;
        tree_.num_terminal_nodes++;
    }

    std::vector<Candidate> legal_actions(const BuildState& s) const {
        const int p = s.player, o = p ^ 1;
        const double to_call = s.bet[o] - s.bet[p];
        const bool facing = to_call > EPS;
        const double max_to = s.bet[p] + s.stack[p];
        const StreetSizes& sizes = cfg_.streets[s.street];

        std::vector<Candidate> actions;
        bool can_raise;
        if (facing) {
            actions.push_back({ActionKind::FOLD, 0.0});
            actions.push_back({ActionKind::CALL, s.bet[o]});
            can_raise = s.aggressions <= cfg_.max_raises && s.stack[p] > to_call + EPS && s.stack[o] > EPS;
        } else {
            actions.push_back({ActionKind::CHECK, s.bet[p]});
            can_raise = s.stack[p] > EPS && s.stack[o] > EPS;
        }
        if (!can_raise) return actions;

        const double pot_after_call = s.pot + to_call;
        std::vector<Candidate> aggressive;
        auto add = [&](double to) {
            if (facing) to = std::max(to, s.bet[o] + s.last_raise); // min-raise
            if (to <= s.bet[o] + EPS) return;
            if (to >= max_to - EPS) {
                aggressive.push_back({ActionKind::ALLIN, max_to});
                return;
            }
            double put = to - s.bet[p];
            double behind = s.stack[p] - put;
            double pot_if_called = s.pot + put + (to - s.bet[o]);
            if (behind <= cfg_.force_allin_threshold * pot_if_called) {
                aggressive.push_back({ActionKind::ALLIN, max_to});
                return;
            }
            aggressive.push_back({facing ? ActionKind::RAISE : ActionKind::BET, to});
        };

        for (const BetSize& b : facing ? sizes.raise[p] : sizes.bet[p]) {
            switch (b.kind) {
                case SizeKind::POT: add(s.bet[o] + b.value * pot_after_call); break;
                case SizeKind::MULT: add(b.value * s.bet[o]); break;
                case SizeKind::CHIPS: add(s.bet[o] + b.value); break;
                case SizeKind::ALLIN: add(max_to); break;
            }
        }
        if ((max_to - s.bet[o]) <= cfg_.add_allin_threshold * pot_after_call + EPS) add(max_to);

        std::sort(aggressive.begin(), aggressive.end(), [](const Candidate& a, const Candidate& b) {
            if (std::fabs(a.to - b.to) > 1e-9) return a.to < b.to;
            return a.kind == ActionKind::ALLIN && b.kind != ActionKind::ALLIN;
        });
        for (const Candidate& c : aggressive) {
            if (!actions.empty() && actions.back().kind >= ActionKind::BET && std::fabs(actions.back().to - c.to) < 1e-4)
                continue; // duplicate size
            actions.push_back(c);
        }
        return actions;
    }

    void build_action(uint32_t id, const BuildState& s) {
        const int p = s.player, o = p ^ 1;
        const std::vector<Candidate> actions = legal_actions(s);
        const int count = (int)actions.size();
        const uint32_t first = alloc_nodes(count);
        {
            Node& n = tree_.nodes[id];
            n = Node{};
            n.type = NodeType::ACTION;
            n.player = (uint8_t)p;
            n.num_children = (uint16_t)count;
            n.first_child = first;
            n.board = s.board_id;
            n.offset = tree_.storage_size;
        }
        tree_.storage_size += (uint64_t)count * num_hands_[p];
        tree_.num_action_nodes++;

        for (int i = 0; i < count; i++) {
            const Candidate& a = actions[i];
            const uint32_t child = first + i;
            BuildState c = s;
            switch (a.kind) {
                case ActionKind::FOLD:
                    set_meta(child, id, a.kind, 0.0, 0, c);
                    make_terminal(child, NodeType::FOLD, p, winner_gain(s, p), c);
                    // values of folds before the river feed straight into nodes above isomorphic chance nodes
                    if (s.street < 2) tree_.nodes[child].offset = symmetry_id(c);
                    break;
                case ActionKind::CHECK:
                    set_meta(child, id, a.kind, 0.0, 0, c);
                    if (p == 0) {
                        c.player = 1;
                        build_action(child, c);
                    } else {
                        build_street_end(child, c);
                    }
                    break;
                case ActionKind::CALL: {
                    double put = std::min(s.bet[o] - s.bet[p], s.stack[p]);
                    c.stack[p] = s.stack[p] - put;
                    if (c.stack[p] < EPS) c.stack[p] = 0.0;
                    c.pot += put;
                    c.bet[p] = s.bet[p] + put;
                    set_meta(child, id, a.kind, put, 0, c);
                    build_street_end(child, c);
                    break;
                }
                default: { // BET, RAISE, ALLIN
                    double put = a.to - s.bet[p];
                    c.stack[p] = a.kind == ActionKind::ALLIN ? 0.0 : s.stack[p] - put;
                    c.pot += put;
                    c.bet[p] = a.to;
                    c.last_raise = std::max(a.to - s.bet[o], s.last_raise);
                    c.aggressions++;
                    c.player = o;
                    set_meta(child, id, a.kind, a.to, 0, c);
                    build_action(child, c);
                    break;
                }
            }
        }
    }

    // bets are equal, the street is over
    void build_street_end(uint32_t id, const BuildState& s) {
        if (s.street == 2) {
            make_terminal(id, NodeType::SHOWDOWN, 0, winner_gain(s, 0), s);
            return;
        }
        build_chance(id, s, s.stack[0] <= EPS || s.stack[1] <= EPS);
    }

    void build_chance(uint32_t id, const BuildState& s, bool runout) {
        // a card is isomorphic to the same rank in the lowest interchangeable suit, its subtree is never built,
        // the solver recovers its values by permuting the hands of the representative's subtree
        const SuitClasses classes = suit_classes(s);
        std::vector<int> reps;
        std::vector<int> rep_child(NUM_CARDS, -1);
        std::vector<IsoCard> isos;
        for (int card = 0; card < NUM_CARDS; card++) {
            if (s.board & card_bit(card)) continue;
            int suit = card_suit(card);
            int rep_suit = classes[suit];
            int rep = card_rank(card) * 4 + rep_suit;
            if (rep_suit == suit || rep_child[rep] < 0) {
                rep_child[card] = (int)reps.size();
                reps.push_back(card);
            } else {
                isos.push_back({(uint16_t)rep_child[rep], (uint8_t)card, (uint8_t)swap_index(rep_suit, suit)});
            }
        }

        const int count = (int)reps.size();
        const uint32_t first = alloc_nodes(count);
        {
            Node& n = tree_.nodes[id];
            n = Node{};
            n.type = NodeType::CHANCE;
            n.num_children = (uint16_t)count;
            n.first_child = first;
            n.board = s.board_id;
            n.offset = tree_.chance.size();
        }
        tree_.chance.push_back({(uint32_t)tree_.iso.size(), (uint32_t)isos.size(), symmetry_id(s)});
        tree_.iso.insert(tree_.iso.end(), isos.begin(), isos.end());
        tree_.num_chance_nodes++;

        for (int k = 0; k < count; k++) {
            const int card = reps[k];
            BuildState c = s;
            c.street++;
            c.player = 0;
            c.bet[0] = c.bet[1] = 0.0;
            c.last_raise = 0.0;
            c.aggressions = 0;
            c.board |= card_bit(card);
            c.board_id = board_id(c.board);
            c.suit_sig[card_suit(card)] |= 1ULL << (13 * c.groups + card_rank(card));
            c.groups++;
            set_meta(first + k, id, ActionKind::DEAL, 0.0, card, c);
            if (runout) build_street_end(first + k, c);
            else build_action(first + k, c);
        }
    }
};

} // namespace

void build_tree(Tree& tree, const GameConfig& config, const int num_hands[2], const bool swap_ok[NUM_SUIT_SWAPS]) {
    Builder builder(tree, config, num_hands, swap_ok);
    builder.run();
}

std::string action_str(const NodeMeta& meta) {
    switch (meta.action) {
        case ActionKind::ROOT: return "root";
        case ActionKind::FOLD: return "fold";
        case ActionKind::CHECK: return "check";
        case ActionKind::CALL: return "call";
        case ActionKind::BET: return "bet " + format_chips(meta.amount);
        case ActionKind::RAISE: return "raise " + format_chips(meta.amount);
        case ActionKind::ALLIN: return "allin " + format_chips(meta.amount);
        case ActionKind::DEAL: return card_str(meta.card);
    }
    return "?";
}
