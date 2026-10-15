#include "include/solver/protocol.h"

#include <chrono>
#include <istream>
#include <ostream>
#include <sstream>
#include <stdexcept>

#include "include/core/commands.h"
#include "include/core/json.h"
#include "include/solver/explorer.h"

namespace {

// runs DCFR like Solver::solve but reports every exploitability check, and a heartbeat in between, as JSON lines
void solve(Solver& solver, Commands& commands, int max_iterations, double target, double& total_seconds, std::ostream& out) {
    using clock = std::chrono::steady_clock;
    const GameConfig& cfg = solver.config();
    const auto start = clock::now();
    const double before = total_seconds;
    auto elapsed = [&] { return before + std::chrono::duration<double>(clock::now() - start).count(); };

    commands.clear_stop();
    double pct = -1.0, last_beat = before;
    bool stopped = false;
    for (int t = solver.iterations_done(); t < max_iterations; t++) {
        solver.iterate(t);
        const int done = t + 1;
        stopped = commands.stop_requested();
        if (done % cfg.check_every == 0 || done == max_iterations || stopped) {
            pct = 100.0 * solver.exploitability() / cfg.pot;
            out << "{\"type\":\"progress\",\"iteration\":" << done << ",\"exploitability\":" << json_number(pct)
                << ",\"seconds\":" << json_number(elapsed()) << "}\n" << std::flush;
            if (pct <= target) break;
        } else if (elapsed() - last_beat >= 0.25) {
            last_beat = elapsed();
            out << "{\"type\":\"iteration\",\"iteration\":" << done << ",\"seconds\":" << json_number(last_beat) << "}\n"
                << std::flush;
        }
        if (stopped) break;
    }
    if (pct < 0.0) pct = 100.0 * solver.exploitability() / cfg.pot; // nothing ran, the limit was already reached
    total_seconds = elapsed();
    const std::array<double, 2> ev = solver.expected_values();
    out << "{\"type\":\"solved\",\"iterations\":" << solver.iterations_done() << ",\"exploitability\":" << json_number(pct)
        << ",\"seconds\":" << json_number(total_seconds) << ",\"stopped\":" << (stopped ? "true" : "false") << ",\"ev\":["
        << json_number(ev[0]) << "," << json_number(ev[1]) << "]}\n"
        << std::flush;
}

} // namespace

void write_tree_info(const Solver& solver, double build_seconds, std::ostream& out) {
    const Tree& t = solver.tree();
    const GameConfig& cfg = solver.config();
    out << "{\"type\":\"tree\",\"nodes\":" << t.nodes.size() << ",\"action_nodes\":" << t.num_action_nodes
        << ",\"chance_nodes\":" << t.num_chance_nodes << ",\"terminal_nodes\":" << t.num_terminal_nodes
        << ",\"iso_cards\":" << t.iso.size() << ",\"hands\":[" << solver.hands(0).size << "," << solver.hands(1).size
        << "],\"memory_mb\":" << json_number(solver.storage_bytes() / 1048576.0)
        << ",\"compressed\":" << (solver.compressed() ? "true" : "false") << ",\"threads\":" << solver.threads()
        << ",\"build_seconds\":" << json_number(build_seconds) << ",\"pot\":" << json_number(cfg.pot)
        << ",\"stack\":" << json_number(cfg.stack) << ",\"iterations\":" << cfg.iterations
        << ",\"target\":" << json_number(cfg.target_exploitability) << ",\"iterations_done\":" << solver.iterations_done()
        << ",\"board\":[";
    for (size_t i = 0; i < cfg.board.size(); i++) out << (i ? "," : "") << json_string(card_str(cfg.board[i]));
    out << "]}\n" << std::flush;
}

void serve(Solver& solver, std::istream& in, std::ostream& out) {
    Commands commands(in);
    Explorer explorer(solver);
    double total_seconds = 0.0;
    std::string line;
    while (commands.next(line)) {
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;
        if (cmd.empty()) continue;
        if (cmd == "quit") break;
        try {
            if (cmd == "solve") {
                int iterations = solver.config().iterations;
                double target = solver.config().target_exploitability;
                std::string a, b;
                ss >> a >> b;
                if (!a.empty()) iterations = std::stoi(a);
                if (!b.empty()) target = std::stod(b);
                solve(solver, commands, iterations, target, total_seconds, out);
            } else if (cmd == "node" || cmd == "ev") {
                explorer.reset();
                std::string token, error;
                while (ss >> token) {
                    if (!explorer.take(token, error)) throw std::runtime_error(error);
                }
                if (cmd == "node") explorer.write_node(out);
                else explorer.write_ev(out);
                out << "\n" << std::flush;
            } else {
                throw std::runtime_error("unknown command '" + cmd + "'");
            }
        } catch (const std::exception& e) {
            out << "{\"type\":\"error\",\"message\":" << json_string(e.what()) << "}\n" << std::flush;
        }
    }
}
