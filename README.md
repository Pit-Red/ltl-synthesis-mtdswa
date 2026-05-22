# ltl-synthesis-mtdtwa

Game-theoretic LTL synthesis using multi-tape deterministic temporal word automata (MTDTWA), built on top of the [Spot](https://spot.lrde.epita.fr/) formal verification library.

Given an LTL specification and a partition of atomic propositions into controllable (system) and uncontrollable (environment), the solver determines whether the specification is realizable and computes the set of winning states.

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
| `tlsf_runner` | Solves a single TLSF benchmark file |
| `tlsf_batch` | Batch-processes a folder of TLSF benchmarks |

## Usage

```bash
# Solve an LTL formula (controllable AP defaults to all)
make run FORMULA="GF a"

# Specify controllable variables
make run FORMULA="GF a & GF b" CONTR_VRB="a"

# Random LTL synthesis comparison: <inputs> <outputs> <seed> <n_samples>
make synth ARGS="r1,r2 g1,g2 42 20"

# TLSF benchmarks (requires syfco on PATH)
make tlsf ARGS="path/to/spec.tlsf"
make tlsf-batch ARGS="path/to/benchmark/folder/"
```

### Debug tracing

Set `EL_TRACE=1` to get colored, indented trace output of the solving algorithm:

```bash
EL_TRACE=1 ./el_solver "GF a & GF b" "a"
```

## Project structure

```
mtdtwa_function/
├── solve_el_aut.cc/hh          Core EL game solver (SolveAutEL)
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
    └── extract_tlsf_info.cc/hh TLSF file parser
```

## Algorithm overview

1. Parse the LTL formula and build a deterministic complete TWA using Spot
2. Convert the TWA to an MTDTWA via `spot::dtwa_to_mtdtwa`
3. Build a Zielonka tree from the acceptance condition
4. Recursively solve the Emerson-Lei game over the Zielonka tree nodes (`SolveAutEL_t`)
5. Compute attractors using BDD quantification — Player 0 (system) existentially quantifies over controllable variables, Player 1 (environment) over uncontrollable ones
6. Return the set of winning states for Player 0
