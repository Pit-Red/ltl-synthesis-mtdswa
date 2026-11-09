# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
make all          # Build all four binaries
make debug        # Debug build (-g -O0), rebuilds everything
make sanitize     # AddressSanitizer + UBSan build for memory debugging
make clean        # Remove build/ dir and all binaries
```

Build outputs:
- `el_solver` — main entry point (`test.cc`)
- `synth_runner` — random LTL synthesis comparison runner
- `tlsf_runner` — single TLSF benchmark solver
- `tlsf_batch` — batch TLSF benchmark solver

Run targets (shortcuts that build then execute):
```bash
make run FORMULA="GF a"                                          # el_solver with LTL formula
make synth ARGS="r1,r2 g1,g2 42 20"                             # synth_runner <inputs> <outputs> <seed> <n>
make tlsf ARGS="path/to/spec.tlsf"
make tlsf-batch ARGS="path/to/folder/"
make tlsf-sanity                                                 # small specs with known answers
```

Dependencies: SPOT (`-lspot -lbddx`), searched at `/usr/local/lib` and `/opt/homebrew/lib`.

## Architecture

The project implements game-theoretic LTL synthesis using multi-tape deterministic automata (MTDTWA), built on top of the [SPOT](https://spot.lrde.epita.fr/) formal verification library.

### Core Algorithm (`solve_el_aut.hh/cc`)

`SolveAutEL(aut, controllable_aps)` — the central function. Takes a deterministic complete TWA and a list of AP names controlled by the system (player 0), returns the set of winning states for that player. Internally:
1. Converts the TWA to an MTDTWA via `spot::dtwa_to_mtdtwa`
2. Builds a Zielonka tree from the automaton's acceptance condition
3. Calls `SolveAutEL_t` recursively over the Zielonka tree nodes

`attractor_states(mtdta, hc, vars, w_partition, l_partition, player)` — computes the attractor (fixed-point reachability) for a given player. Uses BDD quantification to handle controllable/uncontrollable variable splits. Player 0 existentially quantifies over controllable vars; player 1 existentially quantifies over uncontrollable vars.

`HierachicalChecker` (`HierachicalChecker.hh/cc`) — validates acceptance marks in the Emerson-Lei game. Maintains an ordered list of `(mark, player)` pairs; `check(mark, player)` returns true if the most relevant stored mark belongs to `player`.

### Entry Points

| File | Binary | Purpose |
|------|--------|---------|
| `test.cc` | `el_solver` | Parses LTL formula, builds automaton, calls `SolveAutEL`, saves DOT/PNG to `img/` |
| `mtdtwa_attractor.cc` | (separate) | Takes LTL formula + marks + controllable APs, outputs JSON bool array of winning states |
| `mtdtwa_attractor_lazy.cc` | (separate) | Lazy variant of attractor binary |
| `invoke_attractory.py` | — | Python wrapper that shells out to attractor binary with 12 predefined test cases |

### Test Infrastructure

- `tests-randltl/` — random LTL formula generation (`ltlgen.cc`) and synthesis comparison via `ltlsynt` CLI; the `synth_runner` binary compares `ltlsynt` results against `SolveAutEL`
- `tests-tlsf/` — TLSF tests. `tlsf_runner`/`tlsf_batch` use the MTDSWA backend (`SolveAutEL_swa`) only and compare with an `ltlsynt` reference. `extract_tlsf_info.cc` reads TLSF via syfco (`-f ltl -m fully -os mealy|moore`, so Strict semantics is encoded) and builds the reference (`syfco -f ltlxba -m fully -os mealy | ltlsynt --ins --outs`, lowercase names). Pitfalls: `ltlsynt --tlsf` follows TARGET not SEMANTICS and cannot do Moore; ltlsynt lowercases `--ins/--outs` but not the formula. `sanity/` holds small specs with `//STATUS` tags; `scripts/run_suite.sh` runs many files in parallel with time/memory limits. SYNTCOMP benchmarks are not in the repo (github.com/SYNTCOMP/benchmarks).

### Global State Pattern

`solve_el_aut.cc` uses module-level globals (`global_mtdta`, `global_aut`, `global_states`, etc.) as a workaround for passing context into BDD callback functions (`is_winning_terminal_with_acc`). All globals are set by `SolveAutEL` before any recursive calls.

### Visualization

`test.cc` uses SPOT's DOT export + `dot -Tpng` to generate automaton images saved to `img/`.
