#!/usr/bin/env bash
set -euo pipefail

# ---- Configuration ----
SEEDS=(42 123 256)
TREE_SIZES=(40 50 60)
N_SAMPLES=500
INPUTS="r1,r2"
OUTPUTS="g1,g2"
TIMEOUT_S=5

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BENCH_BIN="$PROJECT_DIR/bench_runner"
CSV_DIR="$PROJECT_DIR/csv_results"

# ---- Build ----
echo "Building bench_runner..."
make -C "$PROJECT_DIR" bench_runner
echo ""

# ---- Prepare output directory ----
mkdir -p "$CSV_DIR"

# ---- Run benchmarks ----
total=$((${#SEEDS[@]} * ${#TREE_SIZES[@]}))
run=0
failures=0

for tree_size in "${TREE_SIZES[@]}"; do
    for seed in "${SEEDS[@]}"; do
        run=$((run + 1))
        csv_file="$CSV_DIR/bench_seed${seed}_tree${tree_size}_n${N_SAMPLES}.csv"

        if [ -f "$csv_file" ]; then
            echo "[$run/$total] SKIP (exists): $(basename "$csv_file")"
        else
            echo "[$run/$total] seed=$seed  tree_size=$tree_size  n=$N_SAMPLES  -> $(basename "$csv_file")"
            if ! "$BENCH_BIN" "$INPUTS" "$OUTPUTS" "$seed" "$N_SAMPLES" "$tree_size" "$csv_file" "$TIMEOUT_S"; then
                echo "WARNING: mismatches or errors detected in this run"
                failures=$((failures + 1))
            fi
        fi
        echo ""
    done
done

echo "All $total runs complete ($failures with mismatches/errors). Results in: $CSV_DIR/"
ls -lh "$CSV_DIR"/*.csv
exit $failures
