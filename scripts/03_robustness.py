#!/usr/bin/env python3
"""
03_plot_robustness.py

Plot 1: Robustness-Kurve über bound_k (k-Sweep)
- Input: summary.csv
- Filter: fault_mode=NONE
- Y: RD_mean (Reordering Degree)
- X: bound_k (numerisch sortiert; inf ganz rechts)
- Linien: pro policy (FIFO, RANDOM, ADVERSARIAL, BATCHED)
- Aggregation: Werte sind bereits pro (seed_id, policy, bound_k, fault_mode) aggregiert;
  hier mitteln wir zusätzlich über seed_id (mean of means) für eine kompakte Kurve.

Usage:
  python3 scripts/03_plot_robustness.py --in out/csv/summary.csv --out out/fig/plot1_rd_vs_bound.pdf
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path
from typing import Dict, List, Tuple

import matplotlib.pyplot as plt


def bound_k_sort_key(k: str) -> float:
    if k == "inf":
        return float("inf")
    return float(k)


def bound_k_label(k: str) -> str:
    return "inf" if k == "inf" else k


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="inp", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    inp = Path(args.inp)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    rows: List[Dict[str, str]] = []
    with inp.open("r", encoding="utf-8", newline="") as fp:
        r = csv.DictReader(fp)
        for row in r:
            if row["fault_mode"] != "NONE":
                continue
            rows.append(row)

    policies = sorted({r["policy"] for r in rows})
    bounds = sorted({r["bound_k"] for r in rows}, key=bound_k_sort_key)

    # (policy, bound_k) -> list of RD_mean across seeds
    by_pb: Dict[Tuple[str, str], List[float]] = {}
    for row in rows:
        key = (row["policy"], row["bound_k"])
        by_pb.setdefault(key, []).append(float(row["RD_mean"]))

    x = list(range(len(bounds)))
    xlabels = [bound_k_label(b) for b in bounds]

    fig = plt.figure()
    ax = fig.add_subplot(111)

    for p in policies:
        y = []
        for b in bounds:
            vals = by_pb.get((p, b), [])
            y.append(sum(vals) / len(vals) if vals else 0.0)
        ax.plot(x, y, marker="o", label=p)

    ax.set_xticks(x)
    ax.set_xticklabels(xlabels)
    ax.set_xlabel("bound_k")
    ax.set_ylabel("RD (Reordering Degree)")
    ax.set_title("Plot 1: RD vs bound_k (fault_mode=NONE)")
    ax.grid(True, which="both", axis="y", linestyle=":")

    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(out)
    print(f"[ok] wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
