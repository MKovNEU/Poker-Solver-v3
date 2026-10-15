#include <omp.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "include/game/config.h"
#include "include/preflop/explorer.h"
#include "include/preflop/protocol.h"
#include "include/solver/explorer.h"
#include "include/solver/protocol.h"
#include "include/solver/solver.h"

namespace {

void usage() {
    std::printf(
        "usage: solver <config> [options]      build, solve and explore a spot\n"
        "       solver --load <file> [options]  explore a saved solution\n"
        "       solver <config> --preflop       the config is a preflop game (examples/preflop_6max.txt)\n"
        "options:\n"
        "  --threads N       worker threads (default: all)\n"
        "  --iters N         iteration limit\n"
        "  --target X        stop at X%% of pot exploitability\n"
        "  --no-iso          disable suit isomorphism\n"
        "  --no-compress     keep regrets and strategy in 32 bit floats (twice the memory)\n"
        "  --save FILE       save the solution after solving\n"
        "  --dry-run         build the tree, print its size and exit\n"
        "  --no-explore      don't start the interactive explorer\n"
        "  --serve           JSON commands on stdin / answers on stdout, for the web UI (ui/server.py)\n");
}

std::string megabytes(uint64_t bytes) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f MB", bytes / (1024.0 * 1024.0));
    return buf;
}

void print_tree_info(const Solver& solver, double seconds) {
    const Tree& t = solver.tree();
    std::printf("hands: OOP %d, IP %d   suit swaps usable:", solver.hands(0).size, solver.hands(1).size);
    int swaps = 0;
    for (int s = 0; s < NUM_SUIT_SWAPS; s++) swaps += solver.swap_ok(s);
    std::printf(" %d/6\n", swaps);
    std::printf("tree: %zu nodes (%llu action, %llu chance, %llu terminal), %zu isomorphic cards skipped, %zu river boards\n",
                t.nodes.size(), (unsigned long long)t.num_action_nodes, (unsigned long long)t.num_chance_nodes,
                (unsigned long long)t.num_terminal_nodes, t.iso.size(), t.boards.size());
    std::printf("memory: %s for regrets + strategy (%s), built in %.2fs, %d threads\n", megabytes(solver.storage_bytes()).c_str(),
                solver.compressed() ? "16 bit" : "32 bit", seconds, solver.threads());
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open '" + path + "'");
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int run_preflop(const std::string& config_path, const std::string& load_path, const std::string& save_path,
                const std::string& overrides, bool explore, bool dry_run, bool serve_mode) {
    using clock = std::chrono::steady_clock;
    PreflopConfig config;
    if (load_path.empty()) config = parse_preflop_config(read_file(config_path) + overrides);

    PreflopEquity equity;
    if (!PreflopEquity::load(config.equity_file, equity)) {
        if (serve_mode) {
            std::cout << "{\"type\":\"status\",\"message\":\"computing the preflop equity table (first run only, under a minute)\"}\n"
                      << std::flush;
        }
        equity = PreflopEquity::compute(config.threads > 0 ? config.threads : omp_get_max_threads(), 0, !serve_mode);
        equity.save(config.equity_file);
    }

    const auto t0 = clock::now();
    std::unique_ptr<PreflopSolver> solver =
        load_path.empty() ? std::make_unique<PreflopSolver>(config, equity) : PreflopSolver::load(load_path, equity);
    const double seconds = std::chrono::duration<double>(clock::now() - t0).count();
    if (serve_mode) {
        write_preflop_tree_info(*solver, seconds, std::cout);
        serve_preflop(*solver, std::cin, std::cout);
        return 0;
    }

    const PreflopTree& t = solver->tree();
    const std::vector<std::string> names = position_names(solver->config().players);
    std::printf("preflop:");
    for (const std::string& name : names) std::printf(" %s", name.c_str());
    std::printf(", %s bb deep\ntree: %zu nodes (%u action, %zu terminal), %s, built in %.2fs, %d threads\n",
                format_chips(solver->config().stack).c_str(), t.nodes.size(), t.num_action_nodes, t.terminals.size(),
                megabytes(solver->storage_bytes()).c_str(), seconds, solver->threads());
    if (dry_run) return 0;

    if (load_path.empty()) {
        solver->solve(true);
        const std::vector<double> ev = solver->expected_values();
        std::printf("EV (bb per hand):");
        for (size_t p = 0; p < ev.size(); p++) std::printf(" %s %+.4f", names[p].c_str(), ev[p]);
        std::printf("\n");
        if (!save_path.empty()) {
            solver->save(save_path);
            std::printf("saved %s\n", save_path.c_str());
        }
    } else {
        std::printf("loaded %s (%d iterations)\n", load_path.c_str(), solver->iterations_done());
    }
    PreflopExplorer explorer(*solver);
    if (explore) explorer.run(std::cin, std::cout);
    else explorer.print_node(std::cout);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string config_path, load_path, save_path, overrides;
    bool explore = true, dry_run = false, serve_mode = false, preflop = false;
    try {
        for (int i = 1; i < argc; i++) {
            const std::string a = argv[i];
            auto next = [&]() -> std::string {
                if (i + 1 >= argc) throw std::runtime_error(a + " needs a value");
                return argv[++i];
            };
            if (a == "--load") load_path = next();
            else if (a == "--save") save_path = next();
            else if (a == "--threads") overrides += "\nthreads = " + next();
            else if (a == "--iters" || a == "--iterations") overrides += "\niterations = " + next();
            else if (a == "--target") overrides += "\ntarget_exploitability = " + next();
            else if (a == "--no-iso") overrides += "\nisomorphism = false";
            else if (a == "--no-compress") overrides += "\ncompress = false";
            else if (a == "--no-explore") explore = false;
            else if (a == "--dry-run") dry_run = true;
            else if (a == "--serve") serve_mode = true;
            else if (a == "--preflop") preflop = true;
            else if (a == "-h" || a == "--help") {
                usage();
                return 0;
            } else if (!a.empty() && a[0] == '-') {
                throw std::runtime_error("unknown option " + a);
            } else {
                config_path = a;
            }
        }
        if (config_path.empty() == load_path.empty()) {
            usage();
            return 1;
        }
        if (preflop) return run_preflop(config_path, load_path, save_path, overrides, explore, dry_run, serve_mode);

        using clock = std::chrono::steady_clock;
        std::unique_ptr<Solver> solver;
        if (!load_path.empty()) {
            const auto t0 = clock::now();
            solver = Solver::load(load_path);
            const double seconds = std::chrono::duration<double>(clock::now() - t0).count();
            if (serve_mode) {
                write_tree_info(*solver, seconds, std::cout);
                serve(*solver, std::cin, std::cout);
                return 0;
            }
            print_tree_info(*solver, seconds);
            std::printf("loaded %s (%d iterations)\n", load_path.c_str(), solver->iterations_done());
        } else {
            std::ifstream in(config_path, std::ios::binary);
            if (!in) throw std::runtime_error("cannot open '" + config_path + "'");
            std::stringstream ss;
            ss << in.rdbuf();
            const GameConfig config = parse_config(ss.str() + overrides);

            const auto t0 = clock::now();
            solver = std::make_unique<Solver>(config);
            const double seconds = std::chrono::duration<double>(clock::now() - t0).count();
            if (serve_mode) {
                write_tree_info(*solver, seconds, std::cout);
                serve(*solver, std::cin, std::cout);
                return 0;
            }
            print_tree_info(*solver, seconds);
            if (dry_run) return 0;

            solver->solve(true);
            const auto ev = solver->expected_values();
            std::printf("EV (chips of the %s pot): OOP %.4f, IP %.4f\n", format_chips(config.pot).c_str(), ev[0], ev[1]);
            if (!save_path.empty()) {
                solver->save(save_path);
                std::printf("saved %s\n", save_path.c_str());
            }
        }

        Explorer explorer(*solver);
        if (explore) explorer.run(std::cin, std::cout);
        else explorer.print_node(std::cout);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
