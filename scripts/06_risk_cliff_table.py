"""
Convert risk-cliff CSV into a Markdown table for paper use.
Cell format: p95/RD/exceed
"""
import csv
import os

INP = "out/tab/risk_cliff_swinf.csv"
OUT = "out/tab/risk_cliff_swinf.md"

BOUNDS = ["0", "1", "2", "3", "5", "10", "inf"]
POLICIES = ["FIFO", "RANDOM", "ADVERSARIAL", "BATCHED"]
FAULTS = ["NONE", "RESET", "TIMEOUT"]

def main() -> None:
    os.makedirs(os.path.dirname(OUT), exist_ok=True)

    data = {}
    with open(INP, newline="", encoding="utf-8") as fin:
        for row in csv.DictReader(fin):
            key = (row["policy"], row["fault_mode"])
            data.setdefault(key, {})
            data[key][row["bound_k"]] = {
                "p95": float(row["mean_p95_latency_step"]),
                "rd": float(row["mean_RD"]),
                "ex": float(row["exceed_rate"]),
            }

    lines = []
    lines.append("| policy | fault | " + " | ".join(["k=" + b for b in BOUNDS]) + " |")
    lines.append("|---|---|" + "|".join(["---"] * len(BOUNDS)) + "|")

    for pol in POLICIES:
        for fm in FAULTS:
            key = (pol, fm)
            if key not in data:
                continue
            cells = []
            for b in BOUNDS:
                v = data[key].get(b)
                cells.append("—" if v is None else ("%.2f/%.3f/%.2f" % (v["p95"], v["rd"], v["ex"])))
            lines.append("| %s | %s | %s |" % (pol, fm, " | ".join(cells)))

    with open(OUT, "w", encoding="utf-8") as fout:
        fout.write("\n".join(lines) + "\n")

    print("[ok] wrote", OUT)

if __name__ == "__main__":
    main()
