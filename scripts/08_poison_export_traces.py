#!/usr/bin/env python3
"""
Export condensed traces for the Top-N poison schedules.

Reads:
- out/poison/poison_top_p95_swinf.csv

Writes:
- out/poison/traces/*.log

Keeps only high-level events:
RUN_HEADER / SUBMIT / COMPLETE / FENCE / RESET / RUN_END
"""
import csv
import os
import re

INP = "out/poison/poison_top_p95_swinf.csv"
OUT_DIR = "out/poison/traces"
TOP_N = 10

KEEP = re.compile(r"^(RUN_HEADER|SUBMIT|COMPLETE|FENCE|RESET|RUN_END)\(")

def main() -> None:
    os.makedirs(OUT_DIR, exist_ok=True)

    rows = []
    with open(INP, newline="", encoding="utf-8") as fin:
        for r in csv.DictReader(fin):
            try:
                p95 = float(r.get("p95_latency_step", ""))
            except Exception:
                continue
            rows.append((p95, r))

    rows.sort(key=lambda x: x[0], reverse=True)
    top = rows[:TOP_N]

    written = 0
    for p95, r in top:
        p = r.get("log_file", "")
        if not p or not os.path.exists(p):
            continue

        lines = []
        with open(p, encoding="utf-8", errors="replace") as f:
            for ln in f:
                ln = ln.strip()
                if KEEP.match(ln):
                    lines.append(ln)

        fn = "policy=%s_seed=%s_p95=%.2f.log" % (r.get("policy", ""), r.get("schedule_seed", ""), p95)
        with open(os.path.join(OUT_DIR, fn), "w", encoding="utf-8") as fout:
            fout.write("\n".join(lines) + "\n")
        written += 1

    print("[ok] wrote", written, "traces in", OUT_DIR)

if __name__ == "__main__":
    main()
