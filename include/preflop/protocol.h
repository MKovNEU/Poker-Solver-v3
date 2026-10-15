#pragma once

#include <iosfwd>
#include "include/preflop/solver.h"

// the preflop counterpart of include/solver/protocol.h: `solver <config> --preflop --serve` writes one
// {"type":"tree",...} line, then answers commands on `in` with JSON lines on `out`
//   solve [iterations] [target]  progress lines (exploitability in mbb per hand), then {"type":"solved",...};
//                                "stop" (or the end of the input) ends it
//   node <path>                  the spot after a path of action indices, e.g. "node 1 0 0 2"
//   save <file>                  writes the solution, {"type":"saved"}
//   quit
void write_preflop_tree_info(const PreflopSolver& solver, double build_seconds, std::ostream& out);
void serve_preflop(PreflopSolver& solver, std::istream& in, std::ostream& out);
