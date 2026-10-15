#pragma once

#include <iosfwd>
#include "include/solver/solver.h"

// machine interface for the web UI (ui/server.py): `solver <config> --serve` builds the tree, writes one
// {"type":"tree",...} line, then reads commands from `in` and answers with one JSON line each on `out`
//   solve [iterations] [target]  {"type":"iteration"} / {"type":"progress"} lines while solving, then {"type":"solved"},
//                                "stop" (or the end of the input) ends it after the current iteration,
//                                "solve" again continues
//   node <path>                  the spot after a path of action indices and dealt cards, e.g. "node 0 1 Kd"
//   ev <path>                    EV of every combo `node <path>` lists
//   quit
// a failed command answers {"type":"error","message":...}
void write_tree_info(const Solver& solver, double build_seconds, std::ostream& out);
void serve(Solver& solver, std::istream& in, std::ostream& out);
