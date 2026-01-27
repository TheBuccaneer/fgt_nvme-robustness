#!/usr/bin/env python3
"""
02_aggregate.py

Aggregiert results.csv -> summary.csv

Gruppierung (fest):
(seed_id, policy, bound_k, fault_mode) über alle schedule_seeds

Outputs:
- runs_n
- mismatch_rate, timeout_rate, crash_rate
- RD_mean, RD_std
- FE_mean, FE_std
- pending_peak_mean, pending_peak_std, pending_peak_median
- RCS_mean, RCS_std
- SSI (CV) für pending_peak: std/mean (pro Gruppe)

Usage:
  python3 scripts/02_aggregate.py --in out/csv/results.csv --out out/csv/summary.csv
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple


@dataclass
class Agg:
    runs: int = 0
    mismatch: int = 0
    timeout: int = 0
    crash: int = 0
    RD: List[float] = None
    FE: List[float] = None
    pending_peak: List[float] = None
    pending_area: List[float] = None

    RCS: List[float] = None

    def __post_init__(self):
        self.RD = []
        self.FE = []
        self.pending_peak = []
        self.pending_area = []
        self.RCS = []


def fmean(xs: List[float]) -> float:
    return sum(xs) / len(xs) if xs else 0.0


def fstd(xs: List[float]) -> float:
    if len(xs) < 2:
        return 0.0
    return statistics.pstdev(xs)  # population std for stability


def fmedian(xs: List[float]) -> float:
    if not xs:
        return 0.0
    return float(statistics.median(xs))

def bound_k_sort_key(k: str) -> float:
    if k == "inf":
        return float("inf")
    return float(k)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="inp", required=True, help="Input results.csv")
    ap.add_argument("--out", required=True, help="Output summary.csv")
    args = ap.parse_args()

    inp = Path(args.inp)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    groups: Dict[Tuple[str, str, str, str], Agg] = defaultdict(Agg)

    with inp.open("r", encoding="utf-8", newline="") as fp:
        r = csv.DictReader(fp)
        for row in r:
            seed_id = row["seed_id"]
            policy = row["policy"]
            bound_k = row["bound_k"]
            fault_mode = row["fault_mode"]
            key = (seed_id, policy, bound_k, fault_mode)

            a = groups[key]
            a.runs += 1
            a.mismatch += int(row["mismatch"])
            a.timeout += int(row["timeout"])
            a.crash += int(row["crash"])

            a.RD.append(float(row["RD"]))
            a.FE.append(float(row["FE"]))
            # pending_peak can be empty on crash; treat empty as NaN and skip
            pp = row.get("pending_peak", "")
            if pp != "":
                a.pending_peak.append(float(pp))

            pa = row.get("pending_area", "")
            if pa != "":
                a.pending_area.append(float(pa))

            a.RCS.append(float(row["RCS"]))

    fieldnames = [
        "seed_id",
        "policy",
        "bound_k",
        "fault_mode",
        "runs_n",
        "mismatch_rate",
        "timeout_rate",
        "crash_rate",
        "RD_mean",
        "RD_std",
        "FE_mean",
        "FE_std",
        "pending_peak_mean",
        "pending_peak_std",
        "pending_peak_median",
        "pending_area_mean",
        "pending_area_std",
        "pending_area_median",
        "SSI_area",
        "SSI",
        "RCS_mean",
        "RCS_std",
    ]

    with out.open("w", encoding="utf-8", newline="") as fp:
        w = csv.DictWriter(fp, fieldnames=fieldnames)
        w.writeheader()

        for (seed_id, policy, bound_k, fault_mode), a in sorted(
            groups.items(),
            key=lambda x: (x[0][0], x[0][1], bound_k_sort_key(x[0][2]), x[0][3])
        ):
            runs = a.runs if a.runs > 0 else 1

            mismatch_rate = a.mismatch / runs
            timeout_rate = a.timeout / runs
            crash_rate = a.crash / runs

            rd_mean = fmean(a.RD)
            rd_std = fstd(a.RD)
            fe_mean = fmean(a.FE)
            fe_std = fstd(a.FE)

            pp_mean = fmean(a.pending_peak) if a.pending_peak else 0.0
            pp_std = fstd(a.pending_peak) if a.pending_peak else 0.0
            pp_median = fmedian(a.pending_peak) if a.pending_peak else 0.0
            pa_mean = fmean(a.pending_area) if a.pending_area else 0.0
            pa_std = fstd(a.pending_area) if a.pending_area else 0.0
            pa_median = fmedian(a.pending_area) if a.pending_area else 0.0
            ssi_area = (pa_std / pa_mean) if pa_mean > 0 else 0.0


            ssi = (pp_std / pp_mean) if pp_mean > 0 else 0.0

            rcs_mean = fmean(a.RCS)
            rcs_std = fstd(a.RCS)

            w.writerow(
                {
                    "seed_id": seed_id,
                    "policy": policy,
                    "bound_k": bound_k,
                    "fault_mode": fault_mode,
                    "runs_n": a.runs,
                    "mismatch_rate": f"{mismatch_rate:.6f}",
                    "timeout_rate": f"{timeout_rate:.6f}",
                    "crash_rate": f"{crash_rate:.6f}",
                    "RD_mean": f"{rd_mean:.6f}",
                    "RD_std": f"{rd_std:.6f}",
                    "FE_mean": f"{fe_mean:.6f}",
                    "FE_std": f"{fe_std:.6f}",
                    "pending_peak_mean": f"{pp_mean:.6f}",
                    "pending_peak_std": f"{pp_std:.6f}",
                    "pending_peak_median": f"{pp_median:.6f}",
                    "pending_area_mean": f"{pa_mean:.6f}",
                    "pending_area_std": f"{pa_std:.6f}",
                    "pending_area_median": f"{pa_median:.6f}",
                    "SSI_area": f"{ssi_area:.6f}",

                    "SSI": f"{ssi:.6f}",
                    "RCS_mean": f"{rcs_mean:.6f}",
                    "RCS_std": f"{rcs_std:.6f}",
                }
            )

    print(f"[ok] Wrote {out} with {len(groups)} groups")
    return 0




if __name__ == "__main__":
    raise SystemExit(main())
