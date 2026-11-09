# ltl-synthesis-mtd(twa/swa)

Game-theoretic LTL synthesis using multi-tape deterministic automata, built on top of the [Spot](https://spot.lrde.epita.fr/) formal verification library.

Given an LTL specification and a partition of atomic propositions into controllable (system) and uncontrollable (environment), the solver determines whether the specification is realizable and computes the set of winning states.

Two solver backends are available:

- **MTDTWA** (`SolveAutEL`) — uses transition-based acceptance via `spot::dtwa_to_mtdtwa`
- **MTDSWA** (`SolveAutEL_swa`) — uses state-based acceptance via `spot::dtwa_to_mtdswa`. The automaton is first converted from transition-based to state-based acceptance using `spot::sbacc()` (which moves acceptance marks from edges to states). This variant avoids a bug where `dtwa_to_mtdtwa` collapses accepting sink states into `bddtrue`, bypassing the terminal callback. With `mtdswa` + `sinks_as_states()`, sinks become proper states with acceptance colors that the `HierachicalChecker` can evaluate.

## Prerequisites

| Dependency | Version | Purpose |
|------------|---------|---------|
| **g++** | C++17 support | Compiler |
| **[Spot](https://spot.lrde.epita.fr/install.html)** | >= 2.12 | Automata & LTL library (`libspot`, `libbddx`) |
| **[syfco](https://github.com/reactive-systems/syfco)** | latest | TLSF specification parser (only needed for TLSF benchmarks) |
| **Graphviz** | any | Automaton visualization (`dot` command, optional) |

## Installing Spot

Spot is the core dependency. Install it so that `libspot` and `libbddx` are available on your library path.

### From a tarball (recommended)

Download the latest release from https://spot.lrde.epita.fr/install.html, then:

```bash
./configure
make -j$(nproc)
sudo make install
```

### With Homebrew (macOS)

```bash
brew install spot
```

### With apt (Debian/Ubuntu)

Follow the instructions at https://spot.lrde.epita.fr/install.html to add the Spot repository, then:

```bash
sudo apt install libspot-dev
```

### Verify installation

```bash
# Both should resolve without errors
echo '#include <spot/twa/twagraph.hh>' | g++ -std=c++17 -x c++ -c - -o /dev/null
echo 'int main(){}' | g++ -x c++ - -lspot -lbddx -o /dev/null
```

## Building

All build commands are run from the `mtdtwa_function/` directory:

```bash
cd mtdtwa_function
make all        # Build all binaries
make debug      # Debug build (-g -O0)
make sanitize   # AddressSanitizer + UBSan build
make clean      # Remove build artifacts
```

This produces four binaries:

| Binary | Description |
|--------|-------------|
| `el_solver` | Main solver — parses an LTL formula, builds the automaton, solves the Emerson-Lei game |
| `synth_runner` | Generates random LTL formulas and compares results against `ltlsynt` |
| `tlsf_runner` | Solves a single TLSF file with MTDSWA and compares the verdict with `ltlsynt` |
| `tlsf_batch` | Same for every TLSF file under a folder; writes `results.csv` |

## Usage

```bash
# Solve an LTL formula (runs both backends and compares results by default)
make run FORMULA="GF a"

# Specify controllable variables
make run FORMULA="GF a & GF b" CONTR_VRB="a"

# Run a specific backend only
make run-twa FORMULA="GF a"    # MTDTWA backend only
make run-swa FORMULA="GF a"    # MTDSWA backend only

# Random LTL synthesis comparison: <inputs> <outputs> <seed> <n_samples>
make synth ARGS="r1,r2 g1,g2 42 20"        # both backends
make synth-twa ARGS="r1,r2 g1,g2 42 20"    # MTDTWA only
make synth-swa ARGS="r1,r2 g1,g2 42 20"    # MTDSWA only

# TLSF benchmarks (requires syfco and ltlsynt on PATH)
make tlsf ARGS="path/to/spec.tlsf"
make tlsf-batch ARGS="path/to/benchmark/folder/"
make tlsf-sanity                       # small specs with known answers
```

### TLSF tests

`tlsf_runner` uses the MTDSWA backend only. It reads the specification
under its TLSF `SEMANTICS` (Mealy, Moore, Strict,Mealy, Strict,Moore),
reports whether it is realizable, and compares with an `ltlsynt` reference.
Since `ltlsynt` cannot solve Moore games, the reference first converts the
specification to Mealy semantics with `syfco -os mealy`. Exit code:
0 = match, 1 = mismatch, 2 = inconclusive (error or timeout).

For many files, `tests-tlsf/scripts/run_suite.sh` runs `tlsf_runner` in
parallel with a time and memory limit per file, and summarizes the results.
It also checks MTDSWA against the `//STATUS` tags used by SYNTCOMP:

```bash
# SYNTCOMP benchmarks: git clone https://github.com/SYNTCOMP/benchmarks
tests-tlsf/scripts/run_suite.sh -j 5 -t 90 -o results/syntcomp ~/benchmarks/tlsf

# The same benchmarks under Moore semantics (SYNTCOMP is almost all Mealy)
tests-tlsf/scripts/make_moore_variants.sh ~/benchmarks/tlsf /tmp/tlsf-moore
tests-tlsf/scripts/run_suite.sh -j 5 -t 90 -o results/syntcomp-moore /tmp/tlsf-moore
```

See `tests-tlsf/sanity/README.md` for the small hand-written specs.

The solver backend can also be controlled via the `EL_SOLVER` environment variable: `twa`, `swa`, or `both` (default).

### Debug tracing

Set `EL_TRACE=1` to get colored, indented trace output of the solving algorithm:

```bash
EL_TRACE=1 ./el_solver "GF a & GF b" "a"
```

## Project structure

```
mtdtwa_function/
├── solve_el_aut.cc/hh          Core EL game solver — MTDTWA backend (SolveAutEL)
├── solve_el_aut_swa.cc/hh      Core EL game solver — MTDSWA backend (SolveAutEL_swa)
├── HierachicalChecker.cc/hh    Acceptance condition checker
├── test.cc                     el_solver entry point
├── mtdtwa_attractor.cc         Attractor computation binary
├── mtdtwa_attractor_lazy.cc    Lazy attractor variant
├── invoke_attractory.py        Python wrapper for attractor tests
├── makefile                    Build system
├── tests-randltl/              Random LTL synthesis tests
│   ├── test_synth.cc           synth_runner entry point
│   ├── ltlgen.cc/hh            Random LTL formula generator
│   └── ltlsynt.cc/hh           ltlsynt CLI wrapper
└── tests-tlsf/                 TLSF benchmark tests
    ├── test_tlsf.cc            tlsf_runner entry point
    ├── test_tlsf_batch.cc      tlsf_batch entry point
    ├── extract_tlsf_info.cc/hh TLSF reader (syfco) and ltlsynt reference
    ├── sanity/                 Small specs with known answers
    └── scripts/                Parallel suite runner, Moore-variant generator
```

## Algorithm overview

1. Parse the LTL formula and build a deterministic complete TWA using Spot
2. Convert the TWA to a multi-tape deterministic automaton:
   - **MTDTWA path**: `spot::dtwa_to_mtdtwa` (transition-based acceptance)
   - **MTDSWA path**: `spot::sbacc` (converts acceptance marks from edges to states) + `spot::dtwa_to_mtdswa` + `sinks_as_states()` (state-based acceptance)
3. Build a Zielonka tree from the acceptance condition
4. Recursively solve the Emerson-Lei game over the Zielonka tree nodes
5. Compute attractors using BDD quantification — Player 0 (system) existentially quantifies over controllable variables, Player 1 (environment) over uncontrollable ones
6. Return the set of winning states for Player 0

### MTDSWA sink handling

The MTDSWA backend identifies accepting sink states (self-loop terminals whose color satisfies the acceptance condition) and treats them as absolute wins for the accepting player, bypassing the `HierachicalChecker`. This correctly handles cases where the MTDTWA backend would collapse such sinks into `bddtrue` and lose acceptance information.
