#include "include/solver/explorer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

#include "include/core/json.h"

namespace {

const char* PLAYER_NAMES[2] = {"OOP", "IP"};
const char* STREET_NAMES[3] = {"flop", "turn", "river"};

std::string lower(std::string s) {
    for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
    return s;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string fmt(const char* format, double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, format, v);
    return buf;
}

void print_help(std::ostream& out) {
    out << "commands:\n"
           "  <n> | <action>     take an action by index or name (check, call, fold, bet 3.3, allin, ...)\n"
           "  <card>             deal a turn / river card at a chance node, e.g. Kd\n"
           "  back | ..          undo the last step\n"
           "  root               go back to the root\n"
           "  hands [ev]         strategy of every combo of the player to act (and its EV)\n"
           "  grid [n]           13x13 grid of action frequencies (all actions, or action n)\n"
           "  range oop|ip       13x13 grid of a player's range at this spot\n"
           "  ev                 average EV of both ranges at this spot\n"
           "  json <file>        export this spot (strategy + EV per combo)\n"
           "  quit\n";
}

} // namespace

Explorer::Explorer(Solver& solver) : solver_(solver) { reset(); }

void Explorer::reset() {
    path_.clear();
    Frame root;
    root.node = 0;
    root.perm = {0, 1, 2, 3};
    for (int p = 0; p < 2; p++) root.reach[p] = solver_.hands(p).weight;
    root.board = solver_.config().board;
    path_.push_back(root);
}

void Explorer::back() {
    if (path_.size() > 1) path_.pop_back();
}

int Explorer::real_card(int stored) const {
    const Frame& f = path_.back();
    for (int s = 0; s < 4; s++) {
        if (f.perm[s] == card_suit(stored)) return card_rank(stored) * 4 + s;
    }
    return stored;
}

int Explorer::real_combo(int p, int h) const {
    const HandList& hl = solver_.hands(p);
    return combo_index(real_card(hl.c1[h]), real_card(hl.c2[h]));
}

std::vector<float> Explorer::strategy() const {
    const Node& n = solver_.tree().nodes[node()];
    std::vector<float> sigma((size_t)n.num_children * solver_.hands(n.player).size);
    solver_.average_strategy(node(), sigma.data());
    return sigma;
}

double Explorer::frequency(const std::vector<float>& sigma, int action) const {
    const Node& n = solver_.tree().nodes[node()];
    const std::vector<float>& reach = path_.back().reach[n.player];
    const int N = (int)reach.size();
    double num = 0.0, den = 0.0;
    for (int h = 0; h < N; h++) {
        num += (double)reach[h] * sigma[(size_t)action * N + h];
        den += reach[h];
    }
    return den > 0.0 ? num / den : 0.0;
}

bool Explorer::take(const std::string& token_in, std::string& error) {
    const Tree& tree = solver_.tree();
    const Frame& f = path_.back();
    const Node& n = tree.nodes[f.node];
    const std::string token = lower(trim(token_in));

    if (n.type == NodeType::ACTION) {
        const int A = n.num_children;
        int choice = -1;
        if (!token.empty() && std::all_of(token.begin(), token.end(), [](char c) { return std::isdigit((unsigned char)c); })) {
            int idx = std::stoi(token);
            if (idx >= 0 && idx < A) choice = idx;
        } else {
            int prefix_hits = 0, prefix_choice = -1;
            for (int a = 0; a < A; a++) {
                std::string name = action_str(tree.meta[n.first_child + a]);
                if (name == token) choice = a;
                if (name.rfind(token, 0) == 0) {
                    prefix_hits++;
                    prefix_choice = a;
                }
            }
            if (choice < 0 && prefix_hits == 1) choice = prefix_choice;
        }
        if (choice < 0) {
            error = "no such action '" + token_in + "'";
            return false;
        }
        const std::vector<float> sigma = strategy();
        Frame next = f;
        next.node = n.first_child + choice;
        next.line.push_back(action_str(tree.meta[next.node]));
        std::vector<float>& reach = next.reach[n.player];
        const int N = (int)reach.size();
        for (int h = 0; h < N; h++) reach[h] *= sigma[(size_t)choice * N + h];
        path_.push_back(std::move(next));
        return true;
    }

    if (n.type == NodeType::CHANCE) {
        const int card = parse_card(token);
        if (card < 0) {
            error = "deal a card, e.g. Kd";
            return false;
        }
        if (std::find(f.board.begin(), f.board.end(), card) != f.board.end()) {
            error = card_str(card) + " is already on the board";
            return false;
        }
        const int stored = card_rank(card) * 4 + f.perm[card_suit(card)];
        uint32_t child = UINT32_MAX;
        int swap = -1;
        for (int k = 0; k < n.num_children; k++) {
            if (tree.meta[n.first_child + k].card == stored) child = n.first_child + k;
        }
        if (child == UINT32_MAX) {
            const ChanceData& cd = tree.chance[n.offset];
            for (uint32_t i = 0; i < cd.iso_count; i++) {
                const IsoCard& ic = tree.iso[cd.iso_begin + i];
                if (ic.card == stored) {
                    child = n.first_child + ic.child;
                    swap = ic.swap;
                }
            }
        }
        if (child == UINT32_MAX) {
            error = "card not found in the tree";
            return false;
        }
        Frame next = f;
        next.node = child;
        next.board.push_back(card);
        next.line.push_back(card_str(card));
        if (swap >= 0) {
            // move into the representative's suit frame
            for (int s = 0; s < 4; s++) next.perm[s] = swap_suit(f.perm[s], swap);
            for (int p = 0; p < 2; p++) {
                const std::vector<uint16_t>& perm = solver_.hands(p).perm[swap];
                for (size_t h = 0; h < perm.size(); h++) next.reach[p][h] = f.reach[p][perm[h]];
            }
        }
        const int rep = tree.meta[child].card;
        for (int p = 0; p < 2; p++) {
            for (uint16_t h : solver_.hands(p).with_card[rep]) next.reach[p][h] = 0.0f;
        }
        path_.push_back(std::move(next));
        return true;
    }

    error = "this is a terminal node, go 'back'";
    return false;
}

void Explorer::print_node(std::ostream& out) const {
    const Tree& tree = solver_.tree();
    const Frame& f = path_.back();
    const Node& n = tree.nodes[f.node];
    const NodeMeta& m = tree.meta[f.node];

    out << "\nboard:";
    for (int c : f.board) out << " " << card_str(c);
    out << "   pot: " << format_chips(m.pot) << "   stacks: OOP " << format_chips(m.stack[0]) << " / IP "
        << format_chips(m.stack[1]) << "\nline: ";
    if (f.line.empty()) out << "(root)";
    for (size_t i = 0; i < f.line.size(); i++) out << (i ? ", " : "") << f.line[i];
    out << "\n";

    switch (n.type) {
        case NodeType::ACTION: {
            double combos = 0.0;
            for (float r : f.reach[n.player]) combos += r;
            out << PLAYER_NAMES[n.player] << " to act on the " << STREET_NAMES[m.street] << " (" << fmt("%.1f", combos)
                << " combos)\n";
            const std::vector<float> sigma = strategy();
            for (int a = 0; a < n.num_children; a++) {
                char buf[128];
                std::snprintf(buf, sizeof buf, "  [%d] %-16s %6.2f%%\n", a, action_str(tree.meta[n.first_child + a]).c_str(),
                              100.0 * frequency(sigma, a));
                out << buf;
            }
            break;
        }
        case NodeType::CHANCE:
            out << "chance: deal the " << STREET_NAMES[std::min(m.street + 1, 2)] << " (type a card)\n";
            break;
        case NodeType::FOLD:
            out << PLAYER_NAMES[n.player] << " folds, " << PLAYER_NAMES[n.player ^ 1] << " wins the pot\n";
            break;
        case NodeType::SHOWDOWN:
            out << "showdown\n";
            break;
    }
}

std::vector<float> Explorer::hand_ev(int p) {
    const Tree& tree = solver_.tree();
    const Frame& f = path_.back();
    const int N = solver_.hands(p).size;
    std::vector<float> cfv(N), mass(N), ev(N);
    solver_.node_values(f.node, p, f.reach[p ^ 1].data(), cfv.data());
    solver_.compatible_mass(p, f.reach[p ^ 1].data(), tree.boards[tree.nodes[f.node].board].mask, mass.data());
    const GameConfig& cfg = solver_.config();
    const double pot_share = cfg.pot / 2.0 + (cfg.stack - tree.meta[f.node].stack[p]);
    for (int h = 0; h < N; h++) {
        ev[h] = mass[h] > 1e-12f ? float(cfv[h] / mass[h] + pot_share) : std::numeric_limits<float>::quiet_NaN();
    }
    return ev;
}

void Explorer::print_hands(std::ostream& out, bool with_ev) {
    const Tree& tree = solver_.tree();
    const Frame& f = path_.back();
    const Node& n = tree.nodes[f.node];
    if (n.type != NodeType::ACTION) {
        out << "no player acts here\n";
        return;
    }
    const int p = n.player, A = n.num_children, N = solver_.hands(p).size;
    const std::vector<float> sigma = strategy();
    std::vector<float> ev;
    if (with_ev) ev = hand_ev(p);

    std::vector<int> order;
    for (int h = 0; h < N; h++) {
        if (f.reach[p][h] > 1e-7f) order.push_back(h);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) { return real_combo(p, a) > real_combo(p, b); });

    char buf[64];
    out << "hand   reach ";
    for (int a = 0; a < A; a++) {
        std::snprintf(buf, sizeof buf, " %12.12s", action_str(tree.meta[n.first_child + a]).c_str());
        out << buf;
    }
    if (with_ev) out << "        EV";
    out << "\n";
    for (int h : order) {
        std::snprintf(buf, sizeof buf, "%s  %5.3f ", combo_str(real_combo(p, h)).c_str(), f.reach[p][h]);
        out << buf;
        for (int a = 0; a < A; a++) {
            std::snprintf(buf, sizeof buf, " %11.1f%%", 100.0 * sigma[(size_t)a * N + h]);
            out << buf;
        }
        if (with_ev) {
            std::snprintf(buf, sizeof buf, " %9.3f", ev[h]);
            out << buf;
        }
        out << "\n";
    }
}

void Explorer::print_grid(std::ostream& out, int action) const {
    const Tree& tree = solver_.tree();
    const Frame& f = path_.back();
    const Node& n = tree.nodes[f.node];
    if (n.type != NodeType::ACTION) {
        out << "no player acts here\n";
        return;
    }
    const int p = n.player, N = solver_.hands(p).size;
    const std::vector<float> sigma = strategy();
    for (int a = 0; a < n.num_children; a++) {
        if (action >= 0 && a != action) continue;
        double num[13][13] = {}, den[13][13] = {};
        for (int h = 0; h < N; h++) {
            int row, col;
            combo_grid_pos(real_combo(p, h), row, col);
            num[row][col] += (double)f.reach[p][h] * sigma[(size_t)a * N + h];
            den[row][col] += f.reach[p][h];
        }
        out << "[" << a << "] " << action_str(tree.meta[n.first_child + a]) << " (" << fmt("%.1f", 100.0 * frequency(sigma, a))
            << "%)\n";
        for (int row = 0; row < 13; row++) {
            for (int col = 0; col < 13; col++) {
                char buf[32];
                if (den[row][col] > 1e-7) std::snprintf(buf, sizeof buf, " %3s%4.0f", grid_label(row, col).c_str(), 100.0 * num[row][col] / den[row][col]);
                else std::snprintf(buf, sizeof buf, " %3s   .", grid_label(row, col).c_str());
                out << buf;
            }
            out << "\n";
        }
    }
}

void Explorer::print_range(std::ostream& out, int player) const {
    const Frame& f = path_.back();
    const HandList& hl = solver_.hands(player);
    double num[13][13] = {}, den[13][13] = {};
    uint64_t board = cards_mask(f.board);
    for (int combo = 0; combo < NUM_COMBOS; combo++) {
        if (combo_mask(combo) & board) continue;
        int row, col;
        combo_grid_pos(combo, row, col);
        den[row][col] += 1.0;
    }
    double total = 0.0;
    for (int h = 0; h < hl.size; h++) {
        int row, col;
        combo_grid_pos(real_combo(player, h), row, col);
        num[row][col] += f.reach[player][h];
        total += f.reach[player][h];
    }
    out << PLAYER_NAMES[player] << " range, % of each hand class still in (" << fmt("%.1f", total) << " combos)\n";
    for (int row = 0; row < 13; row++) {
        for (int col = 0; col < 13; col++) {
            char buf[32];
            if (num[row][col] > 1e-7) std::snprintf(buf, sizeof buf, " %3s%4.0f", grid_label(row, col).c_str(), 100.0 * num[row][col] / den[row][col]);
            else std::snprintf(buf, sizeof buf, " %3s   .", grid_label(row, col).c_str());
            out << buf;
        }
        out << "\n";
    }
}

void Explorer::print_ev(std::ostream& out) {
    const Frame& f = path_.back();
    for (int p = 0; p < 2; p++) {
        const std::vector<float> ev = hand_ev(p);
        double num = 0.0, den = 0.0;
        for (size_t h = 0; h < ev.size(); h++) {
            if (std::isnan(ev[h]) || f.reach[p][h] <= 0.0f) continue;
            num += (double)f.reach[p][h] * ev[h];
            den += f.reach[p][h];
        }
        out << PLAYER_NAMES[p] << " EV: " << (den > 0.0 ? fmt("%.4f", num / den) : std::string("-")) << " chips\n";
    }
}

void Explorer::write_json(const std::string& path) {
    const Tree& tree = solver_.tree();
    const Frame& f = path_.back();
    const Node& n = tree.nodes[f.node];
    const NodeMeta& m = tree.meta[f.node];
    std::ofstream js(path);
    if (!js) throw std::runtime_error("cannot write '" + path + "'");

    js << "{\n  \"board\": [";
    for (size_t i = 0; i < f.board.size(); i++) js << (i ? ", " : "") << "\"" << card_str(f.board[i]) << "\"";
    js << "],\n  \"line\": [";
    for (size_t i = 0; i < f.line.size(); i++) js << (i ? ", " : "") << "\"" << f.line[i] << "\"";
    js << "],\n  \"pot\": " << m.pot << ",\n  \"stacks\": [" << m.stack[0] << ", " << m.stack[1] << "],\n";
    const char* type = n.type == NodeType::ACTION ? "action" : n.type == NodeType::CHANCE ? "chance" : n.type == NodeType::FOLD ? "fold" : "showdown";
    js << "  \"type\": \"" << type << "\"";

    std::vector<float> ev[2] = {hand_ev(0), hand_ev(1)};
    std::vector<float> sigma;
    if (n.type == NodeType::ACTION) {
        sigma = strategy();
        js << ",\n  \"player\": \"" << PLAYER_NAMES[n.player] << "\",\n  \"actions\": [";
        for (int a = 0; a < n.num_children; a++) js << (a ? ", " : "") << "\"" << action_str(tree.meta[n.first_child + a]) << "\"";
        js << "],\n  \"frequencies\": [";
        for (int a = 0; a < n.num_children; a++) js << (a ? ", " : "") << frequency(sigma, a);
        js << "]";
    }
    for (int p = 0; p < 2; p++) {
        const int N = solver_.hands(p).size;
        js << ",\n  \"" << (p == 0 ? "oop" : "ip") << "\": [";
        bool first = true;
        for (int h = 0; h < N; h++) {
            if (f.reach[p][h] <= 1e-7f) continue;
            js << (first ? "\n" : ",\n") << "    {\"hand\": \"" << combo_str(real_combo(p, h)) << "\", \"reach\": " << f.reach[p][h];
            if (!std::isnan(ev[p][h])) js << ", \"ev\": " << ev[p][h];
            if (n.type == NodeType::ACTION && n.player == p) {
                js << ", \"strategy\": [";
                for (int a = 0; a < n.num_children; a++) js << (a ? ", " : "") << sigma[(size_t)a * N + h];
                js << "]";
            }
            js << "}";
            first = false;
        }
        js << "\n  ]";
    }
    js << "\n}\n";
}

std::vector<int> Explorer::listed_hands(int p) const {
    const std::vector<float>& reach = path_.back().reach[p];
    std::vector<int> hands;
    for (int h = 0; h < (int)reach.size(); h++) {
        if (reach[h] > 1e-7f) hands.push_back(h);
    }
    std::sort(hands.begin(), hands.end(), [&](int a, int b) { return real_combo(p, a) > real_combo(p, b); });
    return hands;
}

// {"type":"node","kind":"action","street":0,"board":["Qs","Jh","2c"],"line":["check"],"pot":5.5,"stacks":[97.5,97.5],
//  "player":1,"actions":[{"label":"bet 1.82","kind":"bet","amount":1.82,"freq":0.4}, ...],"hands":[oop, ip]}
// a hand is ["AsKs", reach], the acting player's are ["AsKs", reach, [probability of each action]]
void Explorer::write_node(std::ostream& out) const {
    static const char* KINDS[] = {"action", "chance", "fold", "showdown"};
    static const char* ACTIONS[] = {"root", "fold", "check", "call", "bet", "raise", "allin", "deal"};
    const Tree& tree = solver_.tree();
    const Frame& f = path_.back();
    const Node& n = tree.nodes[f.node];
    const NodeMeta& m = tree.meta[f.node];
    const bool acting = n.type == NodeType::ACTION;

    out << "{\"type\":\"node\",\"kind\":\"" << KINDS[(int)n.type] << "\",\"street\":" << (int)m.street << ",\"board\":[";
    for (size_t i = 0; i < f.board.size(); i++) out << (i ? "," : "") << json_string(card_str(f.board[i]));
    out << "],\"line\":[";
    for (size_t i = 0; i < f.line.size(); i++) out << (i ? "," : "") << json_string(f.line[i]);
    out << "],\"pot\":" << json_number(m.pot) << ",\"stacks\":[" << json_number(m.stack[0]) << "," << json_number(m.stack[1])
        << "]";
    if (n.type == NodeType::ACTION || n.type == NodeType::FOLD) out << ",\"player\":" << (int)n.player;

    std::vector<float> sigma;
    if (acting) {
        sigma = strategy();
        out << ",\"actions\":[";
        for (int a = 0; a < n.num_children; a++) {
            const NodeMeta& c = tree.meta[n.first_child + a];
            out << (a ? "," : "") << "{\"label\":" << json_string(action_str(c)) << ",\"kind\":\"" << ACTIONS[(int)c.action]
                << "\",\"amount\":" << json_number(c.amount) << ",\"freq\":" << json_number(frequency(sigma, a)) << "}";
        }
        out << "]";
    }

    out << ",\"hands\":[";
    for (int p = 0; p < 2; p++) {
        const int N = solver_.hands(p).size;
        const std::vector<int> hands = listed_hands(p);
        out << (p ? ",[" : "[");
        for (size_t i = 0; i < hands.size(); i++) {
            const int h = hands[i];
            out << (i ? "," : "") << "[\"" << combo_str(real_combo(p, h)) << "\"," << json_number(f.reach[p][h]);
            if (acting && p == n.player) {
                out << ",[";
                for (int a = 0; a < n.num_children; a++) out << (a ? "," : "") << json_number(sigma[(size_t)a * N + h]);
                out << "]";
            }
            out << "]";
        }
        out << "]";
    }
    out << "]}";
}

// {"type":"ev","ev":[oop, ip]}, null where a hand can't reach this spot
void Explorer::write_ev(std::ostream& out) {
    out << "{\"type\":\"ev\",\"ev\":[";
    for (int p = 0; p < 2; p++) {
        const std::vector<float> ev = hand_ev(p);
        const std::vector<int> hands = listed_hands(p);
        out << (p ? ",[" : "[");
        for (size_t i = 0; i < hands.size(); i++) out << (i ? "," : "") << json_number(ev[hands[i]]);
        out << "]";
    }
    out << "]}";
}

void Explorer::run(std::istream& in, std::ostream& out) {
    out << "\nexplorer, type 'help' for commands\n";
    print_node(out);
    std::string line;
    while (true) {
        out << "> " << std::flush;
        if (!std::getline(in, line)) break;
        line = trim(line);
        std::istringstream ss(line);
        std::string cmd, arg;
        ss >> cmd >> arg;
        cmd = lower(cmd);
        try {
            if (cmd.empty()) {
                print_node(out);
            } else if (cmd == "quit" || cmd == "exit" || cmd == "q") {
                break;
            } else if (cmd == "help" || cmd == "?") {
                print_help(out);
            } else if (cmd == "back" || cmd == "..") {
                back();
                print_node(out);
            } else if (cmd == "root") {
                reset();
                print_node(out);
            } else if (cmd == "hands") {
                print_hands(out, lower(arg) == "ev");
            } else if (cmd == "grid") {
                print_grid(out, arg.empty() ? -1 : std::stoi(arg));
            } else if (cmd == "range") {
                print_range(out, lower(arg) == "ip" ? 1 : 0);
            } else if (cmd == "ev") {
                print_ev(out);
            } else if (cmd == "json") {
                if (arg.empty()) throw std::runtime_error("json needs a file name");
                write_json(arg);
                out << "wrote " << arg << "\n";
            } else {
                std::string error;
                if (take(line, error)) print_node(out);
                else out << error << "\n";
            }
        } catch (const std::exception& e) {
            out << "error: " << e.what() << "\n";
        }
    }
}
