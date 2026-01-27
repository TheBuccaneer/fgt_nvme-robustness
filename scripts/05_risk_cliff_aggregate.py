"""
Aggregate SW∞ run-level results into a risk-cliff CSV:
(policy × fault_mode × bound_k) -> mean_RD, mean_p95_latency_step, exceed_rate
"""
import csv
import os
from collections import defaultdict

INP = "out/csv/results_swinf.csv"
OUT = "out/tab/risk_cliff_swinf.csv"

def f(x: str) -> float:
    try:
        return float(x)
    except Exception:
        return 0.0

def main() -> None:
    os.makedirs(os.path.dirname(OUT), exist_ok=True)

    g = defaultdict(lambda: {"n": 0, "rd": 0.0, "p95": 0.0, "ex": 0.0})

    with open(INP, newline="", encoding="utf-8") as fin:
        r = csv.DictReader(fin)
        for row in r:
            key = (row["policy"], row["fault_mode"], row["bound_k"])
            g[key]["n"] += 1
            g[key]["rd"] += f(row.get("RD", "0"))
            g[key]["p95"] += f(row.get("p95_latency_step", "0"))

            ex = row.get("tail_exceed", "")
            if ex == "":
                ex = 1.0 if f(row.get("tail_slack_step", "0")) > 0 else 0.0
            g[key]["ex"] += f(ex)

    with open(OUT, "w", newline="", encoding="utf-8") as fout:
        w = csv.writer(fout)
        w.writerow(["policy", "fault_mode", "bound_k", "n", "mean_RD", "mean_p95_latency_step", "exceed_rate"])
        for (pol, fm, bk), v in sorted(g.items()):
            n = v["n"]
            w.writerow([pol, fm, bk, n, (v["rd"] / n) if n else 0.0, (v["p95"] / n) if n else 0.0, (v["ex"] / n) if n else 0.0])

    print("[ok] wrote", OUT)

if __name__ == "__main__":
    main()
