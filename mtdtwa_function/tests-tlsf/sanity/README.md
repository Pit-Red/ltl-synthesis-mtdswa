# Sanity specs

Small TLSF specifications with known answers. Each takes milliseconds to solve.
They check the parts of the pipeline that large benchmarks don't test well:
how Mealy and Moore semantics are encoded, Strict semantics, and reading
`SEMANTICS` rather than `TARGET`.

Every file has its expected answer in a SYNTCOMP-style tag:

```
//#!SYNTCOMP
//STATUS : realizable
//#.
```

Run them all from `mtdtwa_function/`:

```bash
make tlsf-sanity
# same as:
tests-tlsf/scripts/run_suite.sh -r -t 60 -o build/tlsf-sanity tests-tlsf/sanity
```

The suite passes only if every spec matches both ltlsynt and its `STATUS` tag.

Input `r`/`r1,r2` belongs to the environment and output `g`/`g1,g2` to the
system. Bold entries are where Mealy and Moore (or Strict and non-Strict)
differ, so a broken encoding fails there.

## `mealy-vs-moore/`: same formula under both semantics

| Spec | Formula | Mealy | Moore |
|---|---|---|---|
| `copy_*` | `G (g <-> r)` | realizable | **unrealizable** |
| `gf_*` | `G F (g <-> r)` | realizable | **unrealizable** |
| `delay_*` | `G (r <-> X g)` | realizable | realizable |
| `predict_*` | `G (g <-> X r)` | unrealizable | unrealizable |
| `resp2_*` | `G ((r1 -> g1) && (r2 -> g2)) && G !(g1 && g2)` | unrealizable | unrealizable |
| `arb_moore` | `G (r1 -> F g1) && G (r2 -> F g2) && G !(g1 && g2)` | – | realizable |
| `nexts_moore` | `G (r -> X g) && G (!r -> X !g)` | – | realizable |

Under Moore semantics the system chooses its outputs before it sees the
inputs of the same step. A Moore controller is also a valid Mealy
controller, so Moore can only turn realizable into unrealizable, never the
reverse.

## `strict/`: Strict vs. non-Strict

All four files use `REQUIRE { r; }` and `ASSERT { g <-> X r; }`.

| Spec | Semantics | Formula (from syfco) | Expected |
|---|---|---|---|
| `pred_mealy` | Mealy | `G r -> G (g <-> X r)` | realizable |
| `pred_moore` | Moore | `G r -> G (g <-> X r)` | realizable |
| `pred_strictmealy` | Strict,Mealy | `(g <-> X r) W !r` | **unrealizable** |
| `pred_strictmoore` | Strict,Moore | `(g <-> X r) W !r` | **unrealizable** |

Non-strict: keeping `g` high works, because it only fails once `r` drops,
and then the assumption is already violated. Strict: the assertion must
hold until the step at which `r` drops, and predicting that step is
impossible.

syfco writes the Strict formula only when it converts the spec (`-os`).
Plain `syfco -f ltl` and `ltlsynt --tlsf` produce the non-strict formula.

## `semantics-vs-target/`: `SEMANTICS` and `TARGET` differ

| Spec | SEMANTICS | TARGET | Expected |
|---|---|---|---|
| `copy_sem-moore_target-mealy` | Moore | Mealy | unrealizable |
| `copy_sem-mealy_target-moore` | Mealy | Moore | realizable |

Both use `G (g <-> r)`. The runner must follow `SEMANTICS`. `ltlsynt --tlsf`
picks the game type from `TARGET`, and for the first file it answers
realizable. All 113 syfco examples are `SEMANTICS: Moore, TARGET: Mealy`.
