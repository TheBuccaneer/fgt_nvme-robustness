#!/usr/bin/env python3
"""
Export poison schedules (Top-K) by p95_latency_step from SW∞ results.
Slice: fault_mode=NONE, bound_k=inf
Output: out/poison/poison_top_p95_swinf.csv
"""
import csv
import os

INP = "out/csv/results_swinf.csv"
OUT = "out/poison/poison_top_p95_swinf.csv"
K = 50

def main() -> None:
    os.makedirs(os.path.dirname(OUT), exist_ok=True)

    rows = []
    with open(INP, newline="", encoding="utf-8") as fin:
        for r in csv.DictReader(fin):
            if r.get("fault_mode") != "NONE":
                continue
            if r.get("bound_k") != "inf":
                continue
            try:
                p95 = float(r.get("p95_latency_step", ""))
            except Exception:
                continue
            rows.append((p95, r))

    rows.sort(key=lambda x: x[0], reverse=True)
    top = rows[:K]

    with open(OUT, "w", newline="", encoding="utf-8") as fout:
        w = csv.writer(fout)
        w.writerow(["p95_latency_step", "policy", "schedule_seed", "seed_id", "log_file"])
        for p95, r in top:
            w.writerow([p95, r.get("policy", ""), r.get("schedule_seed", ""), r.get("seed_id", ""), r.get("log_file", "")])

    print("[ok] wrote", OUT)

if __name__ == "__main__":
    main()
