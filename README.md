# Poker-Solver-v3

Heads-up hold'em postflop solver. Give it both ranges, the pot, the effective stack, the board (flop, turn or river)
and a set of bet sizes. It builds the whole game tree once, solves it to a Nash equilibrium with Discounted CFR,
and lets you inspect the equilibrium strategy, ranges and EVs at every spot in the tree.

Both betting structures are supported:

- **no-limit with a bet-size abstraction**: bets as % of pot, raises as multiples or % of pot, all-in, min-raise rule
- **fixed-limit**: fixed chip sizes per street and a raise cap (`examples/limit_srp_flop.txt`)

A preflop solver (2 to 6 players) sits in front of it: give it the preflop sizes, and any line that ends in a
heads-up flop turns into the two ranges, pot and stack for the postflop solve (see [Preflop solver](#preflop-solver)).

## Build

Needs g++ with C++20 and OpenMP (MSYS2 ucrt64 works). From a plain `cmd`/PowerShell prompt:

```
mingw32-make            # builds solver.exe and run_tests.exe
mingw32-make test       # correctness tests (about a minute)
```

On Git Bash put `/c/msys64/ucrt64/bin` first in `PATH`, otherwise g++ picks up Git's own DLLs and fails silently.

## Usage

```
solver examples/nl_srp_flop.txt                 # build, solve, then explore interactively
solver examples/nl_turn.txt --save turn.sol     # keep the solution
solver --load turn.sol                          # explore it again later without re-solving
solver examples/limit_srp_flop.txt --dry-run    # only build the tree and print its size / memory
solver examples/preflop_6max.txt --preflop      # preflop: solve, then walk lines and `export` postflop configs
```

Options: `--threads N`, `--iters N`, `--target X` (stop at X% of pot exploitability), `--no-iso`,
`--no-compress`, `--save FILE`, `--load FILE`, `--dry-run`, `--no-explore`, `--serve` (JSON protocol for the web UI,
see `include/solver/protocol.h`).

### Web UI

```
python ui/server.py            # then open http://localhost:8000
```

Python 3 standard library only, the server runs `solver.exe --serve` and needs it built first (it adds
`C:\msys64\ucrt64\bin` to the solver's `PATH` for the MinGW DLLs, override with `SOLVER_DLL_DIR`).

- **Preflop**: the preflop game (players, stack, open / 3-bet / 4-bet sizes, limping, the flop model), *Solve
  preflop* (a few seconds for 6-max, solved games are cached in `ui/work/preflop` and load instantly), then the same
  navigator for preflop lines. At a heads-up flop, *Use these ranges in Setup* fills both ranges, the pot and the stack.
- **Setup**: paint both ranges on 13x13 grids (drag to paint, weight slider for partial hands) or paste PioSolver text,
  pick the board, pot and stack, and the bet / raise sizes per street for each player. *Check tree size* shows the
  node count and memory before you commit to a solve.
- **Solve**: exploitability (% of pot, log scale) over time while it runs, *Stop* at any point and *Keep solving* to a
  lower target later.
- **Strategy**: GTO Wizard style navigator. The decision line across the top (click any earlier option to branch off),
  the acting player's strategy on the grid (cell height = how much of the hand class is still in the range), per-combo
  strategy on hover, range and EV views, and a card picker at chance nodes. Backspace goes back a step.

### Config file

```
board = Qs Jh 2c              # 3, 4 or 5 cards; the solve starts on that street
pot = 5.5                     # pot at the start of the street
stack = 97.5                  # effective stack behind
oop_range = 22-QQ,A2s-AQs,K9s+,AJo:0.5,AhKh     # PioSolver syntax, weights after ':'
ip_range = 22+,A2s+,K9o+

flop_bet = 33%, 75%           # % of pot
flop_raise = 3x               # x = raise to a multiple of the bet faced, % = of the pot after calling
turn_bet = 66%, 125%
turn_raise = 3x
river_bet = 66%, 125%, a      # a = all-in, c = fixed chips (limit): river_bet = 4c
river_raise = 3x
oop_turn_bet = 50%            # per-player overrides: oop_/ip_ prefix, take precedence in any order

max_raises = 2                # raises per street after the first bet (fixed-limit cap of 4 bets = 3)
add_allin_threshold = 1.5     # offer all-in when it is at most 1.5 pots (0 = never)
force_allin_threshold = 0.15  # a bet leaving less than 0.15 pots behind becomes all-in
isomorphism = true            # merge suit-isomorphic turn / river cards (exact)
compress = true               # fp16 regrets / strategies: half the memory and memory traffic

iterations = 1000             # iteration limit
target_exploitability = 0.5   # stop once exploitability <= 0.5% of the pot
check_every = 10              # iterations between exploitability checks
threads = 0                   # 0 = all hardware threads
```

### Explorer

After solving (or `--load`) you walk the tree. Turn and river cards are typed as real cards; if a card was merged by
isomorphism, the explorer maps it to the stored representative and translates the suits of every hand back for you.

```
<n> | <action>     take an action by index or name (check, call, fold, bet 3.3, allin, ...)
<card>             deal a turn / river card at a chance node, e.g. Kd
back | .. | root   navigate
hands [ev]         strategy of every combo of the player to act (and its EV)
grid [n]           13x13 grid of action frequencies
range oop|ip       13x13 grid of a player's range at this spot
ev                 average EV of both ranges here
json <file>        export this spot (strategy + EV per combo)
```

EVs are "chips won from the current pot minus future investments", so the two players' EVs add up to the pot.
Exploitability is the mean of the two players' best-response gains, as % of the starting pot.

## How it works

The design keeps the base's split: **all game logic runs once while building the tree**, and the solver just walks flat
arrays. What changed:

- **Vector-form CFR over range vectors.** Each traversal carries the opponent's reach for every hand down the public
  tree and returns the traverser's counterfactual values for every hand, so a node costs O(actions × hands) with
  SIMD-friendly loops and nothing per individual hand pair.
- **Only hands in the ranges.** Every vector is indexed by the player's own hand list (hands with weight > 0 that
  don't touch the board), not by all 1326 combos, so typical ranges are 2-4x cheaper. The indexing is the same at
  every node, so nothing needs remapping at chance nodes.
- **O(n) terminals with exact card removal.** Fold values use inclusion-exclusion with per-card reach sums
  (`total - holding card 1 - holding card 2 + identical combo`). Showdowns use one sweep over precomputed river
  rankings, with tie groups precomputed and totals from a SIMD prefix sum. When the traverser faces a river bet, its
  fold and call are evaluated in the same pass.
- **Exact suit isomorphism.** On monotone, two-tone and paired boards, a turn or river card isomorphic to an already
  built one gets no subtree. Its values are the representative's with suits swapped in every hand (a precomputed hand
  permutation). Exactness needs the reach into those chance nodes to stay suit-symmetric, so hand orbits are averaged
  at chance and early fold nodes. The solver then works on the quotient game, which has the same equilibria. Tests
  check that it matches a naive per-hand-pair recursion and the solve with isomorphism off.
- **DCFR** (α = 1.5, β = 0, γ = 3) with alternating updates. The average strategy is weighted by each player's own
  reach. Restarting the average at powers of 4 (as b-inary's solver does) was measured and dropped: it only adds an
  exploitability spike after each restart.
- **Pruning**: subtrees the opponent's current strategy never reaches are skipped.
- **Threads**: chance nodes spawn OpenMP tasks, one per dealt card, nested for turn → river. Each thread gets its own
  stack-like scratch arena, so the hot loop never calls the allocator, and denormals are flushed to zero.
- **fp16 storage** (default): regrets and cumulative strategies are stored as half floats scaled by each node's
  largest magnitude (F16C conversion), which halves memory and memory traffic. The cumulative strategy grows to
  roughly t/4 times each update, so plain rounding would bias it and stall convergence around 0.07% of pot. It is
  written with stochastic rounding instead (noise is a hash of node, element and iteration, so results stay
  deterministic), and fp16 then converges like fp32.

## Preflop solver

Solving preflop together with every flop is far too big (it's what full-game MCCFR bots grind on for days with card
abstractions). Instead the preflop tree is solved on its own and a pot that reaches the flop gets a value from a model:

- **The game**: 2 to 6 players with equal stacks, a fixed size per raise level (open, SB open, raise over limps, 3-bet
  and 4-bet by position against the raiser, bigger squeezes, all-in from a set raise or above a share of the stack),
  limping off / SB only / anyone. A call that would make the flop multiway isn't offered (fold or re-raise instead), so
  every flop is heads-up: exactly what the postflop solver takes. 6-max 100bb is about 6.8k nodes.
- **Flop value**: all-in equity, realized relative to the opponent. A hand's share of the pot is `e·R / (e·R + (1-e)·R')`
  where R is its realization: OOP gets `realization` (0.7) of IP's, and suited / connected hands more than offsuit
  gappers (`playability`, a heuristic: suited +8%, offsuit -6%, connectors +6% down to -3% for big gaps). Both fade
  out between SPR `realization_spr` and 1, all-in pots are plain equity. Without the hand term the model opens offsuit
  junk over suited connectors, the classic weakness of equity-only preflop models. The factors are meant to be
  calibrated against postflop solves of the resulting spots.
- **Exact equities**: all 812k hand pairs (47k up to suit permutation) over all their 1.7M boards. Walking every board
  once and scoring every matchup on it makes that about 45 seconds, done once and cached in `data/preflop_equity.bin`.
- **Vector-form DCFR over the 169 hand classes.** With no board the combos of a class are interchangeable, so one
  strategy per class is exact and every traversal carries each seat's reach per class. Card removal between the
  traverser and each opponent is exact through precomputed 169 x 169 matrices (compatible combo pairs and their pot
  shares), the opponents are treated as independent of each other: exact heads-up, and with 3+ players the seats' EVs
  add up to within a few hundredths of a bb. A 6-max iteration takes about 4 ms; exploitability falls to about 1.3 mbb
  per hand per seat in 1000 iterations (4 s) and levels off there, as CFR does with more than two players.

In the CLI explorer, `range btn` shows a seat's range and `export` prints the postflop config of a heads-up flop.

## Tests

`run_tests` checks:

- the evaluator against the 2+2 table (if `data/hand_ranks.dat` is present)
- range parsing
- every fold / showdown node against an O(n²) brute force
- the full solver's EV against an independent naive recursion over every hand pair and every dealt card, on river,
  turn and flop trees with isomorphism and all-in runouts
- isomorphism on vs off
- a polarized-vs-bluff-catcher river game against its closed-form equilibrium
- convergence
- save / load
- preflop: the equity table against exact enumeration, the class-vector solver's EVs against a naive walk over every
  hand pair (heads-up, with realization and playability), heads-up 10bb push / fold against its known equilibrium
  (SB shoves 58%, BB calls 37%), a 6-max solve, the postflop export, save / load

## Layout

```
include/core   cards, ranges, hand evaluator, scratch arena
include/game   config parsing, tree builder (all game logic)
include/solver DCFR engine, explorer, JSON protocol for the UI
include/preflop preflop game and tree, equity table, class-vector DCFR, explorer, protocol
src/...        implementations, src/main.cpp is the CLI
ui/            web UI: server.py (bridge to `solver --serve`) and the static page
tests/         test suite
examples/      NL flop / turn / river and fixed-limit flop configs, 6-max and push / fold preflop games
legacy/        the previous base, kept for reference
```
