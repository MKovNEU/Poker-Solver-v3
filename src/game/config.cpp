#include "include/game/config.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {

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

double parse_number(const std::string& key, const std::string& value) {
    try {
        size_t pos = 0;
        double d = std::stod(value, &pos);
        if (pos == value.size()) return d;
    } catch (const std::exception&) {
    }
    throw std::runtime_error("'" + key + "' expects a number, got '" + value + "'");
}

int parse_int(const std::string& key, const std::string& value) {
    double d = parse_number(key, value);
    if (d != std::floor(d)) throw std::runtime_error("'" + key + "' expects an integer, got '" + value + "'");
    return (int)d;
}

bool parse_bool(const std::string& key, const std::string& value) {
    std::string v = lower(value);
    if (v == "true" || v == "1" || v == "yes" || v == "on") return true;
    if (v == "false" || v == "0" || v == "no" || v == "off") return false;
    throw std::runtime_error("'" + key + "' expects true/false, got '" + value + "'");
}

const char* STREET_NAMES[3] = {"flop", "turn", "river"};

} // namespace

std::vector<BetSize> parse_sizes(const std::string& text, bool is_raise) {
    std::vector<BetSize> sizes;
    std::string token;
    auto flush = [&]() {
        if (token.empty()) return;
        std::string t = lower(token);
        token.clear();
        if (t == "a" || t == "allin" || t == "all-in") {
            sizes.push_back({SizeKind::ALLIN, 0.0});
            return;
        }
        char unit = t.back();
        std::string num = t.substr(0, t.size() - 1);
        double v = 0.0;
        try {
            size_t pos = 0;
            v = std::stod(num, &pos);
            if (pos != num.size()) throw std::invalid_argument("");
        } catch (const std::exception&) {
            unit = '?';
        }
        if (unit == '%' && v > 0) sizes.push_back({SizeKind::POT, v / 100.0});
        else if (unit == 'x' && is_raise && v > 1) sizes.push_back({SizeKind::MULT, v});
        else if (unit == 'c' && v > 0) sizes.push_back({SizeKind::CHIPS, v});
        else if (unit == 'x' && !is_raise) throw std::runtime_error("'" + t + "': multipliers (x) only make sense for raises");
        else
            throw std::runtime_error("bad bet size '" + t + "', use 75% (of pot), 3x (raise multiplier), 10c (chips) or a (all-in)");
    };
    for (char ch : text) {
        if (ch == ',' || std::isspace((unsigned char)ch)) flush();
        else token += ch;
    }
    flush();
    return sizes;
}

std::string format_chips(double chips) {
    char buf[64];
    if (std::fabs(chips - std::round(chips)) < 1e-6) std::snprintf(buf, sizeof buf, "%.0f", chips);
    else std::snprintf(buf, sizeof buf, "%.2f", chips);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s;
}

std::string size_str(const BetSize& size) {
    switch (size.kind) {
        case SizeKind::POT: return format_chips(size.value * 100.0) + "%";
        case SizeKind::MULT: return format_chips(size.value) + "x";
        case SizeKind::CHIPS: return format_chips(size.value) + "c";
        case SizeKind::ALLIN: return "a";
    }
    return "?";
}

GameConfig parse_config(const std::string& text) {
    GameConfig cfg;
    cfg.text = text;
    bool has_board = false, has_pot = false, has_stack = false;

    // shared sizes apply to both players, per-player keys (oop_flop_bet, ...) override them in any order
    bool shared_set[3][2] = {}, player_set[2][3][2] = {};
    std::vector<BetSize> shared[3][2], player[2][3][2];

    std::istringstream in(text);
    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        line_no++;
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        line = trim(line);
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) throw std::runtime_error("line " + std::to_string(line_no) + ": expected 'key = value'");
        std::string key = lower(trim(line.substr(0, eq)));
        std::string value = trim(line.substr(eq + 1));

        try {
            if (key == "board") {
                cfg.board = parse_cards(value);
                has_board = true;
            } else if (key == "pot") {
                cfg.pot = parse_number(key, value);
                has_pot = true;
            } else if (key == "stack" || key == "effective_stack") {
                cfg.stack = parse_number(key, value);
                has_stack = true;
            } else if (key == "oop_range") {
                cfg.range_text[0] = value;
            } else if (key == "ip_range") {
                cfg.range_text[1] = value;
            } else if (key == "max_raises") {
                cfg.max_raises = parse_int(key, value);
            } else if (key == "add_allin_threshold") {
                cfg.add_allin_threshold = parse_number(key, value);
            } else if (key == "force_allin_threshold") {
                cfg.force_allin_threshold = parse_number(key, value);
            } else if (key == "isomorphism") {
                cfg.isomorphism = parse_bool(key, value);
            } else if (key == "iterations") {
                cfg.iterations = parse_int(key, value);
            } else if (key == "target_exploitability") {
                cfg.target_exploitability = parse_number(key, value);
            } else if (key == "threads") {
                cfg.threads = parse_int(key, value);
            } else if (key == "check_every") {
                cfg.check_every = std::max(1, parse_int(key, value));
            } else if (key == "compress") {
                cfg.compress = parse_bool(key, value);
            } else {
                // [oop_|ip_]<street>_<bet|raise>
                std::string k = key;
                int who = -1;
                if (k.rfind("oop_", 0) == 0) {
                    who = 0;
                    k = k.substr(4);
                } else if (k.rfind("ip_", 0) == 0) {
                    who = 1;
                    k = k.substr(3);
                }
                int street = -1, kind = -1;
                for (int s = 0; s < 3; s++) {
                    std::string name = STREET_NAMES[s];
                    if (k == name + "_bet") street = s, kind = 0;
                    if (k == name + "_raise") street = s, kind = 1;
                }
                if (street < 0) throw std::runtime_error("unknown key '" + key + "'");
                std::vector<BetSize> sizes = parse_sizes(value, kind == 1);
                if (who < 0) {
                    shared[street][kind] = sizes;
                    shared_set[street][kind] = true;
                } else {
                    player[who][street][kind] = sizes;
                    player_set[who][street][kind] = true;
                }
            }
        } catch (const std::runtime_error& e) {
            throw std::runtime_error("line " + std::to_string(line_no) + ": " + e.what());
        }
    }

    if (!has_board || cfg.board.size() < 3 || cfg.board.size() > 5) throw std::runtime_error("'board' needs 3, 4 or 5 cards");
    if (!has_pot || cfg.pot <= 0) throw std::runtime_error("'pot' must be positive");
    if (!has_stack || cfg.stack < 0) throw std::runtime_error("'stack' must be given and non-negative");
    if (cfg.max_raises < 0) throw std::runtime_error("'max_raises' must be non-negative");
    if (cfg.iterations < 1) throw std::runtime_error("'iterations' must be at least 1");

    for (int p = 0; p < 2; p++) {
        const char* name = p == 0 ? "oop_range" : "ip_range";
        if (cfg.range_text[p].empty()) throw std::runtime_error(std::string("'") + name + "' is missing");
        try {
            cfg.range[p] = parse_range(cfg.range_text[p]);
        } catch (const std::runtime_error& e) {
            throw std::runtime_error(std::string(name) + ": " + e.what());
        }
    }

    for (int s = 0; s < 3; s++) {
        for (int k = 0; k < 2; k++) {
            for (int p = 0; p < 2; p++) {
                std::vector<BetSize>& dst = k == 0 ? cfg.streets[s].bet[p] : cfg.streets[s].raise[p];
                if (player_set[p][s][k]) dst = player[p][s][k];
                else if (shared_set[s][k]) dst = shared[s][k];
            }
        }
    }
    return cfg;
}

GameConfig load_config(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open config '" + path + "'");
    std::stringstream ss;
    ss << in.rdbuf();
    return parse_config(ss.str());
}
