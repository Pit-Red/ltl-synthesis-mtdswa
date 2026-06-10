#!/usr/bin/env python3
"""
Visualize benchmark results from csv_results/.

Usage:
    python3 runnable/plot_bench.py [csv_dir] [output_dir]

Defaults:
    csv_dir   = csv_results/
    output_dir = bench_plots/
"""

import sys
import re
import pathlib
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

# ---- Paths ----
csv_dir = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parent.parent / "csv_results"
out_dir = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else pathlib.Path(__file__).resolve().parent.parent / "bench_plots"
out_dir.mkdir(exist_ok=True)

# ---- Load all CSVs, extract seed & tree_size from filename ----
frames = []
for f in sorted(csv_dir.glob("bench_seed*_tree*_n*.csv")):
    m = re.search(r"seed(\d+)_tree(\d+)_n(\d+)", f.name)
    if not m:
        continue
    df = pd.read_csv(f)
    df["seed"] = int(m.group(1))
    df["tree_size"] = int(m.group(2))
    frames.append(df)

if not frames:
    print(f"No CSV files found in {csv_dir}")
    sys.exit(1)

data = pd.concat(frames, ignore_index=True)
data = data[data["error"] == False].copy()  # drop errored rows
print(f"Loaded {len(data)} data points from {len(frames)} files")

tree_sizes = sorted(data["tree_size"].unique())
seeds = sorted(data["seed"].unique())

# ---- Style ----
plt.rcParams.update({
    "figure.facecolor": "white",
    "axes.grid": True,
    "grid.alpha": 0.3,
    "font.size": 11,
})

# =========================================================================
# Plot 1: Timing comparison  (scatter: swa vs ltlsynt, log-log)
# =========================================================================
fig, ax = plt.subplots(figsize=(8, 7))
colors = plt.cm.viridis(np.linspace(0.15, 0.85, len(tree_sizes)))

for ts, c in zip(tree_sizes, colors):
    sub = data[data["tree_size"] == ts]
    ax.scatter(sub["swa_ms"], sub["ltlsynt_ms"],
               alpha=0.35, s=14, color=c, label=f"tree={ts}")

# diagonal (equal time)
lims = [
    max(data["swa_ms"].min(), 0.01),
    max(data["ltlsynt_ms"].max(), data["swa_ms"].max()) * 1.2,
]
ax.plot(lims, lims, "--", color="gray", lw=1, label="equal time")
ax.set_xscale("log"); ax.set_yscale("log")
ax.set_xlabel("SolveAutEL_swa  (ms)")
ax.set_ylabel("solve_with_ltlsynt  (ms)")
ax.set_title("Per-formula timing: SWA vs ltlsynt")
ax.legend(fontsize=9)
fig.tight_layout()
fig.savefig(out_dir / "timing_scatter.png", dpi=150)
print(f"  -> {out_dir / 'timing_scatter.png'}")

# =========================================================================
# Plot 2: Speedup distribution per tree_size  (box plot)
# =========================================================================
fig, ax = plt.subplots(figsize=(8, 5))
box_data = [data[data["tree_size"] == ts]["speedup"].values for ts in tree_sizes]
bp = ax.boxplot(box_data, labels=[str(ts) for ts in tree_sizes],
                patch_artist=True, showfliers=False, medianprops=dict(color="black"))
for patch, c in zip(bp["boxes"], colors):
    patch.set_facecolor(c)
    patch.set_alpha(0.7)
ax.set_xlabel("Formula tree size")
ax.set_ylabel("Speedup  (ltlsynt_time / swa_time)")
ax.set_title("Speedup distribution by tree size")
fig.tight_layout()
fig.savefig(out_dir / "speedup_boxplot.png", dpi=150)
print(f"  -> {out_dir / 'speedup_boxplot.png'}")

# =========================================================================
# Plot 3: Timing vs automaton states  (scatter, colored by solver)
# =========================================================================
fig, ax = plt.subplots(figsize=(8, 5))
ax.scatter(data["num_states"], data["ltlsynt_ms"],
           alpha=0.3, s=12, color="tab:orange", label="ltlsynt")
ax.scatter(data["num_states"], data["swa_ms"],
           alpha=0.3, s=12, color="tab:blue", label="SWA")
ax.set_yscale("log")
ax.set_xlabel("Automaton states")
ax.set_ylabel("Time (ms)")
ax.set_title("Solving time vs automaton size")
ax.legend()
fig.tight_layout()
fig.savefig(out_dir / "timing_vs_states.png", dpi=150)
print(f"  -> {out_dir / 'timing_vs_states.png'}")

# =========================================================================
# Plot 4: Median timing per tree_size  (grouped bar chart)
# =========================================================================
agg = data.groupby("tree_size").agg(
    ltlsynt_median=("ltlsynt_ms", "median"),
    swa_median=("swa_ms", "median"),
    ltlsynt_q25=("ltlsynt_ms", lambda x: x.quantile(0.25)),
    ltlsynt_q75=("ltlsynt_ms", lambda x: x.quantile(0.75)),
    swa_q25=("swa_ms", lambda x: x.quantile(0.25)),
    swa_q75=("swa_ms", lambda x: x.quantile(0.75)),
    count=("swa_ms", "count"),
).reset_index()

x = np.arange(len(agg))
w = 0.35
fig, ax = plt.subplots(figsize=(8, 5))
ax.bar(x - w/2, agg["ltlsynt_median"], w, label="ltlsynt",
       color="tab:orange", alpha=0.8,
       yerr=[agg["ltlsynt_median"] - agg["ltlsynt_q25"],
             agg["ltlsynt_q75"] - agg["ltlsynt_median"]],
       capsize=3)
ax.bar(x + w/2, agg["swa_median"], w, label="SWA",
       color="tab:blue", alpha=0.8,
       yerr=[agg["swa_median"] - agg["swa_q25"],
             agg["swa_q75"] - agg["swa_median"]],
       capsize=3)
ax.set_xticks(x)
ax.set_xticklabels(agg["tree_size"].astype(str))
ax.set_xlabel("Formula tree size")
ax.set_ylabel("Time (ms)")
ax.set_title("Median solving time by tree size  (IQR error bars)")
ax.legend()
fig.tight_layout()
fig.savefig(out_dir / "median_timing_bars.png", dpi=150)
print(f"  -> {out_dir / 'median_timing_bars.png'}")

# =========================================================================
# Plot 5: Mismatch summary table (printed to console)
# =========================================================================
total = len(data)
mismatches = data[data["match"] == False]
print(f"\n--- Mismatches: {len(mismatches)} / {total} ---")
if len(mismatches) > 0:
    print(mismatches[["formula", "num_states", "ltlsynt_realizable", "swa_realizable", "tree_size", "seed"]].to_string(index=False))

print(f"\nAll plots saved to: {out_dir}/")
