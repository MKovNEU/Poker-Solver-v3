#include "include/preflop/protocol.h"

#include <chrono>
#include <istream>
#include <ostream>
#include <sstream>
#include <stdexcept>

#include "include/core/commands.h"
#include "include/core/json.h"
#include "include/preflop/explorer.h"

namespace {

void solve(PreflopSolver& solver, Commands& commands, int max_iterations, double target, double& total_seconds, std::ostream& out) {
    using clock = std::chrono::steady_clock;
    const PreflopConfig& cfg = solver.config();
    const auto start = clock::now();
    const double before = total_seconds;
    auto elapsed = [&] { return before + std::chrono::duration<double>(clock::now() - start).count(); };

    commands.clear_stop();
    double e = -1.0, last_beat = before;
    bool stopped = false;
    for (int t = solver.iterations_done(); t < max_iterations; t++) {
        solver.iterate(t);
        const int done = t + 1;
        stopped = commands.stop_requested();
        if (done % cfg.check_every == 0 || done == max_iterations || stopped) {
            e = solver.exploitability();
            out << "{\"type\":\"progress\",\"iteration\":" << done << ",\"exploitability\":" << json_number(e)
                << ",\"seconds\":" << json_number(elapsed()) << "}\n" << std::flush;
            if (e <= target) break;
        } else if (elapsed() - last_beat >= 0.25) {
            last_beat = elapsed();
            out << "{\"type\":\"iteration\",\"iteration\":" << done << ",\"seconds\":" << json_number(last_beat) << "}\n"
                << std::flush;
        }
        if (stopped) break;
    }
    if (e < 0.0) e = solver.exploitability();
    total_seconds = elapsed();
    const std::vector<double> ev = solver.expected_values();
    out << "{\"type\":\"solved\",\"iterations\":" << solver.iterations_done() << ",\"exploitability\":" << json_number(e)
        << ",\"seconds\":" << json_number(total_seconds) << ",\"stopped\":" << (stopped ? "true" : "false") << ",\"ev\":[";
    for (size_t p = 0; p < ev.size(); p++) out << (p ? "," : "") << json_number(ev[p]);
    out << "]}\n" << std::flush;
}

} // namespace

void write_preflop_tree_info(const PreflopSolver& solver, double build_seconds, std::ostream& out) {
    const PreflopTree& t = solver.tree();
    const PreflopConfig& cfg = solver.config();
    const std::vector<std::string> names = position_names(cfg.players);
    out << "{\"type\":\"tree\",\"game\":\"preflop\",\"players\":" << cfg.players << ",\"positions\":[";
    for (size_t p = 0; p < names.size(); p++) out << (p ? "," : "") << json_string(names[p]);
    out << "],\"nodes\":" << t.nodes.size() << ",\"action_nodes\":" << t.num_action_nodes
        << ",\"terminal_nodes\":" << t.terminals.size() << ",\"memory_mb\":" << json_number(solver.storage_bytes() / 1048576.0)
        << ",\"threads\":" << solver.threads() << ",\"build_seconds\":" << json_number(build_seconds)
        << ",\"stack\":" << json_number(cfg.stack) << ",\"iterations\":" << cfg.iterations
        << ",\"target\":" << json_number(cfg.target) << ",\"iterations_done\":" << solver.iterations_done() << "}\n"
        << std::flush;
}

void serve_preflop(PreflopSolver& solver, std::istream& in, std::ostream& out) {
    Commands commands(in);
    PreflopExplorer explorer(solver);
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
                double target = solver.config().target;
                std::string a, b;
                ss >> a >> b;
                if (!a.empty()) iterations = std::stoi(a);
                if (!b.empty()) target = std::stod(b);
                solve(solver, commands, iterations, target, total_seconds, out);
            } else if (cmd == "node") {
                explorer.reset();
                std::string token, error;
                while (ss >> token) {
                    if (!explorer.take(token, error)) throw std::runtime_error(error);
                }
                explorer.write_node(out);
                out << "\n" << std::flush;
            } else if (cmd == "save") {
                std::string path;
                std::getline(ss >> std::ws, path);
                if (path.empty()) throw std::runtime_error("save needs a file name");
                solver.save(path);
                out << "{\"type\":\"saved\",\"path\":" << json_string(path) << "}\n" << std::flush;
            } else {
                throw std::runtime_error("unknown command '" + cmd + "'");
            }
        } catch (const std::exception& e) {
            out << "{\"type\":\"error\",\"message\":" << json_string(e.what()) << "}\n" << std::flush;
        }
    }
}
