#include "include/preflop/explorer.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <sstream>

#include "include/core/json.h"
#include "include/game/config.h"

namespace {

std::string lower(std::string s) {
    for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
    return s;
}

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

const char* KINDS[] = {"action", "fold", "showdown"};
const char* ACTIONS[] = {"root", "fold", "check", "call", "raise", "allin"};

} // namespace

PreflopExplorer::PreflopExplorer(PreflopSolver& solver) : solver_(solver), names_(position_names(solver.config().players)) {
    reset();
}

void PreflopExplorer::reset() {
    path_.clear();
    Frame root;
    root.node = 0;
    for (int p = 0; p < solver_.config().players; p++) root.reach[p].assign(NUM_CLASSES, 1.0f);
    path_.push_back(std::move(root));
}

void PreflopExplorer::back() {
    if (path_.size() > 1) path_.pop_back();
}

std::vector<float> PreflopExplorer::strategy() const {
    const PNode& n = solver_.tree().nodes[node()];
    std::vector<float> sigma((size_t)n.num_children * NUM_CLASSES);
    solver_.average_strategy(node(), sigma.data());
    return sigma;
}

double PreflopExplorer::frequency(const std::vector<float>& sigma, int action) const {
    const PNode& n = solver_.tree().nodes[node()];
    const std::vector<float>& reach = path_.back().reach[n.player];
    double num = 0.0, den = 0.0;
    for (int i = 0; i < NUM_CLASSES; i++) {
        const double w = (double)class_combos(i) * reach[i];
        num += w * sigma[(size_t)action * NUM_CLASSES + i];
        den += w;
    }
    return den > 0.0 ? num / den : 0.0;
}

int PreflopExplorer::seat_of(const std::string& name) const {
    for (size_t p = 0; p < names_.size(); p++)
        if (lower(names_[p]) == lower(name)) return (int)p;
    return -1;
}

bool PreflopExplorer::take(const std::string& token_in, std::string& error) {
    const PreflopTree& tree = solver_.tree();
    const Frame& f = path_.back();
    const PNode& n = tree.nodes[f.node];
    if (n.type != PNodeType::ACTION) {
        error = "the hand is over here, go 'back'";
        return false;
    }
    const std::string token = lower(trim(token_in));
    int choice = -1;
    if (!token.empty() && std::all_of(token.begin(), token.end(), [](char c) { return std::isdigit((unsigned char)c); })) {
        const int idx = std::stoi(token);
        if (idx >= 0 && idx < n.num_children) choice = idx;
    } else {
        int hits = 0, last = -1;
        for (int a = 0; a < n.num_children; a++) {
            const std::string name = preflop_action_str(tree.meta[n.first_child + a]);
            if (name == token) choice = a;
            if (name.rfind(token, 0) == 0) {
                hits++;
                last = a;
            }
        }
        if (choice < 0 && hits == 1) choice = last;
    }
    if (choice < 0) {
        error = "no such action '" + token_in + "'";
        return false;
    }
    const std::vector<float> sigma = strategy();
    Frame next = f;
    next.node = n.first_child + choice;
    next.line.push_back(names_[n.player] + " " + preflop_action_str(tree.meta[next.node]));
    for (int i = 0; i < NUM_CLASSES; i++) next.reach[n.player][i] *= sigma[(size_t)choice * NUM_CLASSES + i];
    path_.push_back(std::move(next));
    return true;
}

void PreflopExplorer::print_node(std::ostream& out) const {
    const PreflopTree& tree = solver_.tree();
    const Frame& f = path_.back();
    const PNode& n = tree.nodes[f.node];
    out << "\nline: ";
    if (f.line.empty()) out << "(start)";
    for (size_t i = 0; i < f.line.size(); i++) out << (i ? ", " : "") << f.line[i];
    out << "\npot: " << format_chips(tree.meta[f.node].pot) << "\n";
    switch (n.type) {
        case PNodeType::ACTION: {
            out << names_[n.player] << " to act\n";
            const std::vector<float> sigma = strategy();
            for (int a = 0; a < n.num_children; a++) {
                char buf[128];
                std::snprintf(buf, sizeof buf, "  [%d] %-14s %6.2f%%\n", a, preflop_action_str(tree.meta[n.first_child + a]).c_str(),
                              100.0 * frequency(sigma, a));
                out << buf;
            }
            break;
        }
        case PNodeType::FOLD:
            out << names_[n.player] << " wins the pot\n";
            break;
        case PNodeType::SHOWDOWN: {
            const PTerminal& t = tree.terminals[n.data];
            out << (t.allin ? "all-in: " : "flop: ") << names_[t.oop] << " (OOP) vs " << names_[t.ip] << " (IP)";
            if (!t.allin) out << ", " << format_chips(t.behind) << " behind, type 'export' for the postflop config";
            out << "\n";
            break;
        }
    }
}

void PreflopExplorer::print_grid(std::ostream& out) const {
    const PreflopTree& tree = solver_.tree();
    const Frame& f = path_.back();
    const PNode& n = tree.nodes[f.node];
    if (n.type != PNodeType::ACTION) {
        out << "nobody acts here\n";
        return;
    }
    const std::vector<float> sigma = strategy();
    const std::vector<float>& reach = f.reach[n.player];
    for (int a = 0; a < n.num_children; a++) {
        out << "[" << a << "] " << preflop_action_str(tree.meta[n.first_child + a]) << "\n";
        for (int row = 0; row < 13; row++) {
            for (int col = 0; col < 13; col++) {
                const int cls = row * 13 + col;
                char buf[32];
                if (reach[cls] > 1e-6f) std::snprintf(buf, sizeof buf, " %3s%4.0f", class_name(cls).c_str(), 100.0 * sigma[(size_t)a * NUM_CLASSES + cls]);
                else std::snprintf(buf, sizeof buf, " %3s   .", class_name(cls).c_str());
                out << buf;
            }
            out << "\n";
        }
    }
}

void PreflopExplorer::print_range(std::ostream& out, int seat) const {
    const std::vector<float>& reach = path_.back().reach[seat];
    double combos = 0.0;
    for (int i = 0; i < NUM_CLASSES; i++) combos += class_combos(i) * reach[i];
    char head[96];
    std::snprintf(head, sizeof head, "%s range, %% of each hand (%.1f combos, %.1f%% of hands)\n", names_[seat].c_str(), combos,
                  100.0 * combos / NUM_COMBOS);
    out << head;
    for (int row = 0; row < 13; row++) {
        for (int col = 0; col < 13; col++) {
            const int cls = row * 13 + col;
            char buf[32];
            if (reach[cls] > 1e-6f) std::snprintf(buf, sizeof buf, " %3s%4.0f", class_name(cls).c_str(), 100.0 * reach[cls]);
            else std::snprintf(buf, sizeof buf, " %3s   .", class_name(cls).c_str());
            out << buf;
        }
        out << "\n";
    }
}

std::string PreflopExplorer::range_text(int seat) const {
    const std::vector<float>& reach = path_.back().reach[seat];
    std::string text;
    for (int i = 0; i < NUM_CLASSES; i++) {
        if (reach[i] < 0.0005f) continue;
        if (!text.empty()) text += ",";
        text += class_name(i);
        if (reach[i] < 0.9995f) {
            char buf[16];
            std::snprintf(buf, sizeof buf, ":%.3f", reach[i]);
            text += buf;
        }
    }
    return text;
}

std::string PreflopExplorer::postflop_config() const {
    const PreflopTree& tree = solver_.tree();
    const Frame& f = path_.back();
    const PNode& n = tree.nodes[f.node];
    if (n.type != PNodeType::SHOWDOWN) return "";
    const PTerminal& t = tree.terminals[n.data];
    if (t.allin) return "";
    std::string s = "# preflop: ";
    for (size_t i = 0; i < f.line.size(); i++) s += (i ? ", " : "") + f.line[i];
    s += "\n# " + names_[t.oop] + " is OOP, " + names_[t.ip] + " is IP\n";
    s += "pot = " + format_chips(t.pot) + "\n";
    s += "stack = " + format_chips(t.behind) + "\n";
    s += "oop_range = " + range_text(t.oop) + "\n";
    s += "ip_range = " + range_text(t.ip) + "\n";
    return s;
}

// {"type":"node","kind":"action","positions":[...],"line":[...],"pot":4,"invested":[...],"folded":[...],"player":2,
//  "actions":[{"label":"raise 2.5","kind":"raise","amount":2.5,"freq":0.3}, ...],"strategy":[[p0, p1, ...] x 169],
//  "reach":[[169 floats] x seats]}, a showdown also has "oop","ip","allin","behind","realization" and both ranges
void PreflopExplorer::write_node(std::ostream& out) const {
    const PreflopTree& tree = solver_.tree();
    const Frame& f = path_.back();
    const PNode& n = tree.nodes[f.node];
    const int players = solver_.config().players;

    // what every seat has in after the line: the closest terminal below has it, walk the path instead
    std::array<double, MAX_PLAYERS> invested{};
    std::array<bool, MAX_PLAYERS> folded{};
    for (int p = 0; p < players; p++) invested[p] = solver_.config().ante;
    invested[small_blind_seat(players)] += solver_.config().sb;
    invested[big_blind_seat(players)] += 1.0;
    for (size_t k = 1; k < path_.size(); k++) {
        const PMeta& m = tree.meta[path_[k].node];
        if (m.action == PAction::FOLD) folded[m.actor] = true;
        else if (m.action != PAction::CHECK) invested[m.actor] = solver_.config().ante + m.amount;
    }

    out << "{\"type\":\"node\",\"kind\":\"" << KINDS[(int)n.type] << "\",\"positions\":[";
    for (int p = 0; p < players; p++) out << (p ? "," : "") << json_string(names_[p]);
    out << "],\"line\":[";
    for (size_t i = 0; i < f.line.size(); i++) out << (i ? "," : "") << json_string(f.line[i]);
    out << "],\"pot\":" << json_number(tree.meta[f.node].pot) << ",\"invested\":[";
    for (int p = 0; p < players; p++) out << (p ? "," : "") << json_number(invested[p]);
    out << "],\"folded\":[";
    for (int p = 0; p < players; p++) out << (p ? "," : "") << (folded[p] ? "true" : "false");
    out << "]";

    if (n.type == PNodeType::ACTION) {
        const std::vector<float> sigma = strategy();
        out << ",\"player\":" << (int)n.player << ",\"actions\":[";
        for (int a = 0; a < n.num_children; a++) {
            const PMeta& c = tree.meta[n.first_child + a];
            out << (a ? "," : "") << "{\"label\":" << json_string(preflop_action_str(c)) << ",\"kind\":\""
                << (c.action == PAction::CALL && c.limp ? "limp" : ACTIONS[(int)c.action]) << "\",\"amount\":" << json_number(c.amount)
                << ",\"freq\":" << json_number(frequency(sigma, a)) << "}";
        }
        out << "],\"strategy\":[";
        for (int i = 0; i < NUM_CLASSES; i++) {
            out << (i ? ",[" : "[");
            for (int a = 0; a < n.num_children; a++) out << (a ? "," : "") << json_number(sigma[(size_t)a * NUM_CLASSES + i]);
            out << "]";
        }
        out << "]";
    } else if (n.type == PNodeType::FOLD) {
        out << ",\"winner\":" << (int)n.player;
    } else {
        const PTerminal& t = tree.terminals[n.data];
        out << ",\"oop\":" << (int)t.oop << ",\"ip\":" << (int)t.ip << ",\"allin\":" << (t.allin ? "true" : "false")
            << ",\"behind\":" << json_number(t.behind) << ",\"depth\":" << json_number(t.depth)
            << ",\"oop_range\":" << json_string(range_text(t.oop)) << ",\"ip_range\":" << json_string(range_text(t.ip));
    }

    out << ",\"reach\":[";
    for (int p = 0; p < players; p++) {
        out << (p ? ",[" : "[");
        for (int i = 0; i < NUM_CLASSES; i++) out << (i ? "," : "") << json_number(f.reach[p][i]);
        out << "]";
    }
    out << "]}";
}

void PreflopExplorer::run(std::istream& in, std::ostream& out) {
    out << "\npreflop explorer, type 'help' for commands\n";
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
        if (cmd.empty()) {
            print_node(out);
        } else if (cmd == "quit" || cmd == "exit" || cmd == "q") {
            break;
        } else if (cmd == "help" || cmd == "?") {
            out << "  <n> | <action>   take an action by index or name (fold, call, limp, raise, allin)\n"
                   "  back | .. | root navigate\n"
                   "  grid             13x13 strategy of the seat to act\n"
                   "  range <seat>     13x13 range of a seat here (e.g. range btn)\n"
                   "  export           postflop config (pot, stack, ranges) at a heads-up flop\n"
                   "  quit\n";
        } else if (cmd == "back" || cmd == "..") {
            back();
            print_node(out);
        } else if (cmd == "root") {
            reset();
            print_node(out);
        } else if (cmd == "grid") {
            print_grid(out);
        } else if (cmd == "range") {
            const int seat = seat_of(arg);
            if (seat >= 0) {
                print_range(out, seat);
            } else {
                out << "which seat? one of";
                for (const std::string& name : names_) out << " " << name;
                out << "\n";
            }
        } else if (cmd == "export") {
            const std::string cfg = postflop_config();
            out << (cfg.empty() ? "only at a flop with chips behind\n" : cfg);
        } else {
            std::string error;
            if (take(line, error)) print_node(out);
            else out << error << "\n";
        }
    }
}
