# scripts/09_rdss_compare_thresholds.py
#!/usr/bin/env python3
"""
Compare RDSS vs Uniform baseline on threshold hit counts.

Inputs:
  - rdss top_poison.csv (from scripts/rdss_ce.py)
  - uniform top_poison.csv (from scripts/rdss_ce.py)

Output:
  - prints a compact summary, e.g.
    [RDSS] N=40 best_slack=31.0 hits>=25:1 hits>=30:1 hits>=31:1
    [UNIFORM] N=50 best_slack=31.0 hits>=25:2 hits>=30:1 hits>=31:1
"""

import argparse
import csv
import math
from pathlib import Path
from typing import List, Tuple


def read_slacks(path: Path) -> List[float]:
    with path.open(newline="", encoding="utf-8") as f:
        r = csv.DictReader(f)
        slacks = []
        for row in r:
            v = row.get("tail_slack_step", "")
            if v is None or v == "":
                continue
            try:
                slacks.append(float(v))
            except ValueError:
                continue
        return slacks


def summarize(name: str, slacks: List[float], thresholds: List[float]) -> str:
    n = len(slacks)
    best = max(slacks) if slacks else float("nan")
    parts = [f"[{name}] N={n} best_slack={best:.1f}" if best == best else f"[{name}] N={n} best_slack=nan"]
    for t in thresholds:
        hits = sum(1 for s in slacks if s >= t)
        # thresholds are typically ints; keep formatting simple
        if float(t).is_integer():
            parts.append(f"hits>={int(t)}:{hits}")
        else:
            parts.append(f"hits>={t}:{hits}")
    return " ".join(parts)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rdss", required=True, help="path to RDSS top_poison.csv")
    ap.add_argument("--uniform", required=True, help="path to Uniform top_poison.csv")
    ap.add_argument("--thresholds", nargs="+", required=True, type=float, help="threshold list, e.g. 25 30 31")
    args = ap.parse_args()

    rdss_path = Path(args.rdss)
    uni_path = Path(args.uniform)

    if not rdss_path.exists():
        raise SystemExit(f"[err] not found: {rdss_path}")
    if not uni_path.exists():
        raise SystemExit(f"[err] not found: {uni_path}")

    thresholds = list(args.thresholds)

    rdss_slacks = read_slacks(rdss_path)
    uni_slacks = read_slacks(uni_path)

    print(summarize("RDSS", rdss_slacks, thresholds))
    print(summarize("UNIFORM", uni_slacks, thresholds))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
