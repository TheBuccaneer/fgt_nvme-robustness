#!/usr/bin/env python3
"""
04_plot_latency.py

Plot Figure 2: Latency by Policy

Shows policy-sensitive latency metrics (mean_latency, p95_latency, max_latency)
grouped by policy, filtered by bound_k and fault_mode.

Usage:
  python3 scripts/04_plot_latency.py --in out/csv/results_sw4.csv --out out/fig/plot2_latency.pdf
  python3 scripts/04_plot_latency.py --in out/csv/results_sw4.csv --out out/fig/plot2_latency.pdf --metric p95_latency --bound-k inf --fault-mode NONE
"""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path
from typing import Dict, List

import matplotlib.pyplot as plt


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="inp", required=True, help="Input results.csv")
    ap.add_argument("--out", required=True, help="Output PDF/PNG path")
    ap.add_argument("--bound-k", default="inf", help="Filter by bound_k (default: inf)")
    ap.add_argument("--fault-mode", default="NONE", help="Filter by fault_mode (default: NONE)")
    ap.add_argument("--metric", default="p95_latency_step", 
                    choices=["mean_latency_step", "p95_latency_step", "max_latency_step",
                             "mean_latency_disp", "p95_latency_disp", "max_latency_disp"],
                    help="Which latency metric to plot (default: p95_latency_step)")
    args = ap.parse_args()

    inp = Path(args.inp)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    # Collect data: policy -> list of metric values
    data: Dict[str, List[float]] = defaultdict(list)

    with inp.open("r", encoding="utf-8", newline="") as fp:
        r = csv.DictReader(fp)
        for row in r:
            if row["bound_k"] != args.bound_k:
                continue
            if row["fault_mode"] != args.fault_mode:
                continue
            if row.get("mismatch", "0") != "0":
                continue  # skip corrupted runs
            
            policy = row["policy"]
            val = row.get(args.metric, "")
            if val == "":
                continue
            data[policy].append(float(val))

    if not data:
        print(f"[error] No data found for bound_k={args.bound_k}, fault_mode={args.fault_mode}")
        return 1

    # Compute means per policy
    policies = ["FIFO", "RANDOM", "ADVERSARIAL", "BATCHED"]
    means = []
    stds = []
    labels = []
    
    for p in policies:
        if p in data and data[p]:
            vals = data[p]
            mean = sum(vals) / len(vals)
            if len(vals) > 1:
                # sample std (n-1)
                variance = sum((x - mean) ** 2 for x in vals) / (len(vals) - 1)
                std = variance ** 0.5
            else:
                std = 0.0
            means.append(mean)
            stds.append(std)
            labels.append(p)

    if not means:
        print(f"[error] No policies with data")
        return 1

    # Plot
    fig, ax = plt.subplots(figsize=(8, 5))
    
    x = range(len(labels))
    bars = ax.bar(x, means, yerr=stds, capsize=5, edgecolor='black', alpha=0.8)
    
    ax.set_xlabel("Policy", fontsize=12)
    ax.set_ylabel(f"{args.metric} (steps)", fontsize=12)
    ax.set_title(f"Command Latency by Policy\n(bound_k={args.bound_k}, fault_mode={args.fault_mode})", fontsize=14)
    ax.set_xticks(x)
    ax.set_xticklabels(labels, fontsize=11)
    
    # Add value labels on bars
    for bar, mean in zip(bars, means):
        ax.annotate(f'{mean:.2f}',
                    xy=(bar.get_x() + bar.get_width() / 2, bar.get_height()),
                    xytext=(0, 3),
                    textcoords="offset points",
                    ha='center', va='bottom', fontsize=10)
    
    ax.set_ylim(bottom=0)
    ax.grid(axis='y', alpha=0.3)
    
    plt.tight_layout()
    plt.savefig(out, dpi=150, bbox_inches='tight')
    plt.close()
    
    print(f"[ok] Wrote {out}")
    print(f"     Policies: {labels}")
    print(f"     Means: {[f'{m:.2f}' for m in means]}")
    
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
