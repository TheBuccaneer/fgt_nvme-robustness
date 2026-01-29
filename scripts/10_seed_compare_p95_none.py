# scripts/10_seed_compare_p95_none.py
#!/usr/bin/env python3
"""
Create a compact seed1 vs seed2 comparison for mean p95 latency by bound_k,
restricted to fault_mode=NONE. This is meant for Option B (generality check).

Inputs:
  - --seed1: results_swinf.csv
  - --seed2: results_swinf_seed2.csv

Output:
  - a small text file with 2 blocks (seed1/seed2), listing per-policy mean p95
    across bounds present in the CSVs.

Notes (audit):
- We compute mean(p95_latency_step) across runs grouped by (policy, bound_k)
  for fault_mode=NONE.
- bound_k is treated as a string; ordering is numeric-ish with "inf" last.
"""

import argparse
import csv
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Tuple


def bound_sort_key(b: str) -> Tuple[int, float]:
    # ("inf" last), then numeric bounds by float value.
    if b.strip().lower() == "inf":
        return (1, 0.0)
    try:
        return (0, float(b))
    except ValueError:
        return (0, 0.0)


def load_means(path: Path) -> Tuple[List[str], Dict[str, Dict[str, float]]]:
    """
    Returns:
      - bounds sorted list
      - dict policy -> dict bound_k -> mean(p95_latency_step)
    """
    acc: Dict[Tuple[str, str], List[float]] = defaultdict(list)
    bounds_set = set()
    policies_set = set()

    with path.open(newline="", encoding="utf-8") as f:
        r = csv.DictReader(f)
        for row in r:
            if row.get("fault_mode", "") != "NONE":
                continue
            pol = row.get("policy", "")
            bk = row.get("bound_k", "")
            v = row.get("p95_latency_step", "")
            if not pol or bk == "" or v == "":
                continue
            try:
                fv = float(v)
            except ValueError:
                continue
            acc[(pol, bk)].append(fv)
            bounds_set.add(bk)
            policies_set.add(pol)

    bounds = sorted(bounds_set, key=bound_sort_key)
    # stable policy order (matches your paper usage)
    policy_order = ["FIFO", "RANDOM", "BATCHED", "ADVERSARIAL"]
    policies = [p for p in policy_order if p in policies_set] + sorted(policies_set - set(policy_order))

    means: Dict[str, Dict[str, float]] = {p: {} for p in policies}
    for (pol, bk), vals in acc.items():
        means.setdefault(pol, {})
        means[pol][bk] = sum(vals) / len(vals)

    return bounds, means


def format_block(title: str, bounds: List[str], means: Dict[str, Dict[str, float]]) -> str:
    lines = []
    lines.append(f"[{title}] mean p95_latency_step, fault=NONE")
    lines.append("bounds: " + ", ".join(bounds))
    for pol in ["FIFO", "RANDOM", "BATCHED", "ADVERSARIAL"]:
        if pol not in means:
            continue
        xs = []
        for b in bounds:
            v = means[pol].get(b, None)
            xs.append("nan" if v is None else f"{v:.2f}")
        lines.append(f"{pol} " + " ".join(xs))
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed1", required=True, help="path to seed1 results_swinf.csv")
    ap.add_argument("--seed2", required=True, help="path to seed2 results_swinf_seed2.csv")
    ap.add_argument("--out", required=True, help="output txt path")
    args = ap.parse_args()

    p1 = Path(args.seed1)
    p2 = Path(args.seed2)
    out = Path(args.out)

    if not p1.exists():
        raise SystemExit(f"[err] not found: {p1}")
    if not p2.exists():
        raise SystemExit(f"[err] not found: {p2}")

    bounds1, m1 = load_means(p1)
    bounds2, m2 = load_means(p2)

    # Use union of bounds for a consistent table (sorted)
    bounds = sorted(set(bounds1) | set(bounds2), key=bound_sort_key)

    # Ensure all policies exist in dicts (for printing order)
    for pol in ["FIFO", "RANDOM", "BATCHED", "ADVERSARIAL"]:
        m1.setdefault(pol, {})
        m2.setdefault(pol, {})

    text = []
    text.append(format_block("seed1", bounds, m1))
    text.append("")
    text.append(format_block("seed2", bounds, m2))
    text.append("")

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(text), encoding="utf-8")
    print(f"[ok] wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
