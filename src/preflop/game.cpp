#include "include/preflop/game.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>

#include "include/core/cards.h"
#include "include/game/config.h"

// all the preflop game logic: it runs once while building the tree, the solver only walks the arrays

namespace {

constexpr double EPS = 1e-6;

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) b++;
    while (e > b && std::isspace((unsigned char)s[e - 1])) e--;
    return s.substr(b, e - b);
}

std::string lower(std::string s) {
    for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
    return s;
}

// "2.5", "2.5bb", "3x", "40%" (a share)
double parse_number(const std::string& key, std::string value) {
    double scale = 1.0;
    value = lower(value);
    if (value.size() > 2 && value.compare(value.size() - 2, 2, "bb") == 0) value.resize(value.size() - 2);
    else if (!value.empty() && value.back() == 'x') value.pop_back();
    else if (!value.empty() && value.back() == '%') {
        value.pop_back();
        scale = 0.01;
    }
    try {
        size_t pos = 0;
        const double d = std::stod(value, &pos);
        if (pos == value.size()) return d * scale;
    } catch (const std::exception&) {
    }
    throw std::runtime_error("'" + key + "' expects a number, got '" + value + "'");
}

int parse_int(const std::string& key, const std::string& value) {
    const double d = parse_number(key, value);
    if (d != std::floor(d)) throw std::runtime_error("'" + key + "' expects an integer, got '" + value + "'");
    return (int)d;
}

struct State {
    std::array<double, MAX_PLAYERS> committed{}, behind{};
    unsigned folded = 0, allin = 0, pending = 0;
    double bet = 1.0;
    int raises = 0, callers = 0, limpers = 0, last_raiser = -1;
};

struct Choice {
    PAction kind;
    double to;
    bool limp;
};

class Builder {
public:
    Builder(PreflopTree& tree, const PreflopConfig& cfg)
        : tree_(tree), cfg_(cfg), n_(cfg.players), sb_(small_blind_seat(cfg.players)), bb_(big_blind_seat(cfg.players)) {}

    void run() {
        tree_ = PreflopTree{};
        State s;
        for (int p = 0; p < n_; p++) s.behind[p] = cfg_.stack - cfg_.ante;
        s.committed[sb_] = cfg_.sb;
        s.behind[sb_] -= cfg_.sb;
        s.committed[bb_] = 1.0;
        s.behind[bb_] -= 1.0;
        s.pending = (1u << n_) - 1;
        alloc(1);
        tree_.meta[0] = {0, PAction::ROOT, 0, false, 0.0f, (float)pot(s)};
        build(0, s, 0);
    }

private:
    PreflopTree& tree_;
    const PreflopConfig& cfg_;
    const int n_, sb_, bb_;

    uint32_t alloc(int count) {
        const uint32_t first = (uint32_t)tree_.nodes.size();
        tree_.nodes.resize(first + count);
        tree_.meta.resize(first + count);
        return first;
    }

    double pot(const State& s) const {
        double p = n_ * cfg_.ante;
        for (int q = 0; q < n_; q++) p += s.committed[q];
        return p;
    }

    bool in_hand(const State& s, int p) const { return !(s.folded >> p & 1); }

    int next_seat(const State& s, int from) const {
        for (int k = 1; k <= n_; k++) {
            const int q = (from + k) % n_;
            if (s.pending >> q & 1) return q;
        }
        return -1;
    }

    std::vector<Choice> choices(const State& s, int p) const {
        const double to_call = s.bet - s.committed[p];
        const bool facing = to_call > EPS;
        std::vector<Choice> out;
        if (facing) out.push_back({PAction::FOLD, 0.0, false});
        else out.push_back({PAction::CHECK, s.committed[p], false});

        if (facing) {
            const bool limp = s.raises == 0;
            bool ok = !limp || cfg_.limp == 2 || (cfg_.limp == 1 && p == sb_ && s.limpers == 0);
            int matched = 0; // players already in for the bet, a call must not make the flop multiway
            for (int q = 0; q < n_; q++) matched += in_hand(s, q) && s.committed[q] >= s.bet - EPS;
            if (matched + 1 > 2) ok = false;
            if (ok) out.push_back({PAction::CALL, std::min(s.bet, s.committed[p] + s.behind[p]), limp});
        }

        bool someone_left = false;
        for (int q = 0; q < n_; q++) someone_left |= q != p && in_hand(s, q) && !(s.allin >> q & 1);
        if (s.raises < cfg_.allin_raise && s.behind[p] > to_call + EPS && someone_left) {
            const int level = s.raises + 1;
            const double total = s.committed[p] + s.behind[p];
            double to;
            if (level >= cfg_.allin_raise) {
                to = total;
            } else if (s.raises == 0) {
                to = s.limpers == 0 ? (p == sb_ ? cfg_.sb_open : cfg_.open) : cfg_.iso + (s.limpers - 1);
            } else {
                const bool ip = postflop_order(n_, p) > postflop_order(n_, s.last_raiser);
                const double f = level == 2 ? (ip ? cfg_.threebet_ip : cfg_.threebet_oop) : (ip ? cfg_.fourbet_ip : cfg_.fourbet_oop);
                to = f * s.bet;
                if (level == 2) to += cfg_.squeeze * s.bet * s.callers;
            }
            if (to >= total - EPS || to > cfg_.allin_threshold * cfg_.stack + EPS) to = total;
            if (to > s.bet + EPS) out.push_back({to >= total - EPS ? PAction::ALLIN : PAction::RAISE, to, false});
        }
        return out;
    }

    void build(uint32_t id, const State& s, int p) {
        const std::vector<Choice> acts = choices(s, p);
        const int count = (int)acts.size();
        const uint32_t first = alloc(count);
        PNode& n = tree_.nodes[id];
        n = PNode{};
        n.type = PNodeType::ACTION;
        n.player = (uint8_t)p;
        n.num_children = (uint8_t)count;
        n.first_child = first;
        n.data = (uint32_t)tree_.storage;
        n.invested = (float)(s.committed[p] + cfg_.ante);
        tree_.storage += (uint64_t)count * NUM_CLASSES;
        tree_.num_action_nodes++;
        if (tree_.storage > 0xFFFFFFF0ULL) throw std::runtime_error("preflop tree too large");

        for (int k = 0; k < count; k++) {
            const Choice& a = acts[k];
            State c = s;
            const unsigned bit = 1u << p;
            switch (a.kind) {
                case PAction::FOLD:
                    c.folded |= bit;
                    c.pending &= ~bit;
                    break;
                case PAction::CHECK:
                    c.pending &= ~bit;
                    break;
                case PAction::CALL: {
                    const double put = std::min(s.bet - s.committed[p], s.behind[p]);
                    c.committed[p] += put;
                    c.behind[p] -= put;
                    if (c.behind[p] <= EPS) {
                        c.behind[p] = 0.0;
                        c.allin |= bit;
                    }
                    c.pending &= ~bit;
                    if (s.raises > 0) c.callers++;
                    else c.limpers++;
                    break;
                }
                default: { // RAISE, ALLIN
                    c.behind[p] -= a.to - s.committed[p];
                    c.committed[p] = a.to;
                    if (c.behind[p] <= EPS) {
                        c.behind[p] = 0.0;
                        c.allin |= bit;
                    }
                    c.bet = a.to;
                    c.raises++;
                    c.last_raiser = p;
                    c.callers = 0;
                    c.pending = ((1u << n_) - 1) & ~c.folded & ~c.allin & ~bit;
                    break;
                }
            }
            const uint32_t child = first + k;
            const double amount = a.kind == PAction::CALL ? c.committed[p] : a.to;
            tree_.meta[child] = {id, a.kind, (uint8_t)p, a.limp, (float)amount, (float)pot(c)};
            finish(child, c, p);
        }
    }

    void finish(uint32_t id, const State& s, int last) {
        const int left = n_ - std::popcount(s.folded);
        const int next = next_seat(s, last);
        if (left > 1 && next >= 0) {
            build(id, s, next);
            return;
        }
        PTerminal t;
        for (int q = 0; q < n_; q++) t.invested[q] = (float)(s.committed[q] + cfg_.ante);
        t.pot = (float)pot(s);
        PNode& n = tree_.nodes[id];
        n = PNode{};
        n.data = (uint32_t)tree_.terminals.size();
        if (left == 1) {
            n.type = PNodeType::FOLD;
            for (int q = 0; q < n_; q++)
                if (in_hand(s, q)) n.player = (uint8_t)q;
        } else {
            if (left != 2) throw std::runtime_error("internal error: multiway flop in the preflop tree");
            int a = -1, b = -1;
            for (int q = 0; q < n_; q++)
                if (in_hand(s, q)) (a < 0 ? a : b) = q;
            if (postflop_order(n_, a) > postflop_order(n_, b)) std::swap(a, b);
            n.type = PNodeType::SHOWDOWN;
            t.oop = (uint8_t)a;
            t.ip = (uint8_t)b;
            t.allin = ((s.allin >> a) & 1) || ((s.allin >> b) & 1);
            t.behind = (float)std::min(s.behind[a], s.behind[b]);
            const double spr = t.behind / t.pot;
            const double depth = t.allin ? 0.0 : std::clamp((spr - 1.0) / (cfg_.realization_spr - 1.0), 0.0, 1.0);
            t.depth = (float)(std::round(depth * 100.0) / 100.0);
        }
        tree_.terminals.push_back(t);
    }
};

} // namespace

PreflopConfig parse_preflop_config(const std::string& text) {
    PreflopConfig cfg;
    cfg.text = text;
    std::istringstream in(text);
    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        line_no++;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        line = trim(line);
        if (line.empty()) continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) throw std::runtime_error("line " + std::to_string(line_no) + ": expected 'key = value'");
        const std::string key = lower(trim(line.substr(0, eq)));
        const std::string value = trim(line.substr(eq + 1));
        try {
            if (key == "players") cfg.players = parse_int(key, value);
            else if (key == "stack") cfg.stack = parse_number(key, value);
            else if (key == "sb") cfg.sb = parse_number(key, value);
            else if (key == "ante") cfg.ante = parse_number(key, value);
            else if (key == "open") cfg.open = parse_number(key, value);
            else if (key == "sb_open") cfg.sb_open = parse_number(key, value);
            else if (key == "iso") cfg.iso = parse_number(key, value);
            else if (key == "threebet_ip") cfg.threebet_ip = parse_number(key, value);
            else if (key == "threebet_oop") cfg.threebet_oop = parse_number(key, value);
            else if (key == "squeeze") cfg.squeeze = parse_number(key, value);
            else if (key == "fourbet_ip") cfg.fourbet_ip = parse_number(key, value);
            else if (key == "fourbet_oop") cfg.fourbet_oop = parse_number(key, value);
            else if (key == "allin_raise") cfg.allin_raise = parse_int(key, value);
            else if (key == "allin_threshold") cfg.allin_threshold = parse_number(key, value);
            else if (key == "realization") cfg.realization = parse_number(key, value);
            else if (key == "realization_spr") cfg.realization_spr = parse_number(key, value);
            else if (key == "playability") cfg.playability = parse_number(key, value);
            else if (key == "iterations") cfg.iterations = parse_int(key, value);
            else if (key == "target_exploitability" || key == "target") cfg.target = parse_number(key, value);
            else if (key == "threads") cfg.threads = parse_int(key, value);
            else if (key == "check_every") cfg.check_every = std::max(1, parse_int(key, value));
            else if (key == "equity_file") cfg.equity_file = value;
            else if (key == "limp") {
                const std::string v = lower(value);
                if (v == "none" || v == "no" || v == "false") cfg.limp = 0;
                else if (v == "sb") cfg.limp = 1;
                else if (v == "all" || v == "yes" || v == "true") cfg.limp = 2;
                else throw std::runtime_error("'limp' is none, sb or all");
            } else {
                throw std::runtime_error("unknown key '" + key + "'");
            }
        } catch (const std::runtime_error& e) {
            throw std::runtime_error("line " + std::to_string(line_no) + ": " + e.what());
        }
    }
    if (cfg.players < 2 || cfg.players > MAX_PLAYERS) throw std::runtime_error("'players' must be 2 to 6");
    if (cfg.stack <= 1.0 + cfg.ante) throw std::runtime_error("'stack' must be more than the big blind");
    if (cfg.sb <= 0.0 || cfg.sb > 1.0) throw std::runtime_error("'sb' must be in (0, 1]");
    if (cfg.open <= 1.0 || cfg.sb_open <= 1.0 || cfg.iso <= 1.0) throw std::runtime_error("open and iso sizes must be above 1 bb");
    for (double f : {cfg.threebet_ip, cfg.threebet_oop, cfg.fourbet_ip, cfg.fourbet_oop})
        if (f <= 1.0) throw std::runtime_error("re-raise multiples must be above 1");
    if (cfg.allin_raise < 1) throw std::runtime_error("'allin_raise' must be at least 1");
    if (cfg.realization <= 0.0 || cfg.realization > 1.0) throw std::runtime_error("'realization' must be in (0, 1]");
    if (cfg.realization_spr <= 1.0) throw std::runtime_error("'realization_spr' must be above 1");
    if (cfg.playability < 0.0 || cfg.playability > 3.0) throw std::runtime_error("'playability' must be in [0, 3]");
    if (cfg.iterations < 1) throw std::runtime_error("'iterations' must be at least 1");
    return cfg;
}

std::vector<std::string> position_names(int players) {
    static const char* NAMES[MAX_PLAYERS] = {"UTG", "HJ", "CO", "BTN", "SB", "BB"};
    return std::vector<std::string>(NAMES + MAX_PLAYERS - players, NAMES + MAX_PLAYERS);
}

int small_blind_seat(int players) { return players == 2 ? 0 : players - 2; }
int big_blind_seat(int players) { return players - 1; }

int postflop_order(int players, int seat) {
    if (players == 2) return seat == 1 ? 0 : 1; // heads-up the big blind acts first after the flop
    if (seat == players - 2) return 0;
    if (seat == players - 1) return 1;
    return seat + 2;
}

void build_preflop_tree(PreflopTree& tree, const PreflopConfig& cfg) {
    Builder builder(tree, cfg);
    builder.run();
}

std::string preflop_action_str(const PMeta& meta) {
    switch (meta.action) {
        case PAction::ROOT: return "root";
        case PAction::FOLD: return "fold";
        case PAction::CHECK: return "check";
        case PAction::CALL: return meta.limp ? "limp" : "call";
        case PAction::RAISE: return "raise " + format_chips(meta.amount);
        case PAction::ALLIN: return "allin " + format_chips(meta.amount);
    }
    return "?";
}

int combo_class(int combo) {
    int row, col;
    combo_grid_pos(combo, row, col);
    return row * 13 + col;
}

std::string class_name(int cls) { return grid_label(cls / 13, cls % 13); }

int class_combos(int cls) {
    const int row = cls / 13, col = cls % 13;
    return row == col ? 6 : row < col ? 4 : 12;
}

std::array<float, NUM_CLASSES> playability_factors(double strength) {
    std::array<double, NUM_CLASSES> f{};
    double mean = 0.0;
    for (int cls = 0; cls < NUM_CLASSES; cls++) {
        const int row = cls / 13, col = cls % 13;
        double t = 0.0;
        if (row != col) {
            const int hi = 12 - std::min(row, col), lo = 12 - std::max(row, col); // 12 = ace, 0 = deuce
            int gap = hi - lo - 1;
            if (hi == 12) gap = std::min(gap, lo); // ace-low straights, A2 is a connector
            t = (row < col ? 0.08 : -0.06) + (gap == 0 ? 0.06 : gap == 1 ? 0.04 : gap == 2 ? 0.02 : gap == 3 ? 0.0 : -0.03);
        }
        f[cls] = 1.0 + strength * t;
        mean += class_combos(cls) * f[cls];
    }
    mean /= NUM_COMBOS;
    std::array<float, NUM_CLASSES> out;
    for (int cls = 0; cls < NUM_CLASSES; cls++) out[cls] = (float)(f[cls] / mean);
    return out;
}

float preflop_realization(const PreflopConfig& cfg, const std::array<float, NUM_CLASSES>& playability, int cls, bool oop,
                          float depth) {
    const double position = oop ? 1.0 - (1.0 - cfg.realization) * depth : 1.0;
    return (float)(position * (1.0 + (playability[cls] - 1.0) * depth));
}
