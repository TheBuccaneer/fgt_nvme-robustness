#!/usr/bin/env python3
import argparse, csv, os, math
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

def parse_bound_k(s: str) -> float:
    return float("inf") if s == "inf" else float(s)

def fmt_bound_k(s: str) -> str:
    return "inf" if s == "inf" else s

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="inp", required=True, help="risk_cliff_*.csv")
    ap.add_argument("--fault", required=True, help="e.g. NONE/RESET/TIMEOUT")
    ap.add_argument("--metric", required=True, help="column name, e.g. mean_p95_latency_step or exceed_rate")
    ap.add_argument("--out", required=True, help="output pdf path")
    args = ap.parse_args()

    # Load rows
    rows = []
    with open(args.inp, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r.get("fault_mode") == args.fault:
                rows.append(r)

    if not rows:
        raise SystemExit(f"[err] no rows for fault_mode={args.fault} in {args.inp}")

    policies = sorted(set(r["policy"] for r in rows))
    bounds = sorted(set(r["bound_k"] for r in rows), key=parse_bound_k)

    # Build grid [policy][bound]
    grid = [[math.nan for _ in bounds] for _ in policies]
    for r in rows:
        p = r["policy"]
        b = r["bound_k"]
        try:
            v = float(r.get(args.metric, "nan"))
        except ValueError:
            v = math.nan
        pi = policies.index(p)
        bi = bounds.index(b)
        grid[pi][bi] = v

    # Plot
    fig, ax = plt.subplots(figsize=(1.2 + 0.8*len(bounds), 1.2 + 0.6*len(policies)))
    im = ax.imshow(grid, aspect="auto", cmap="viridis")

    ax.set_xticks(range(len(bounds)))
    ax.set_xticklabels([f"k={fmt_bound_k(b)}" for b in bounds], rotation=45, ha="right")
    ax.set_yticks(range(len(policies)))
    ax.set_yticklabels(policies)

    ax.set_title(f"Risk-cliff heatmap ({args.fault}) – p95 latency (mean across runs)")

    # Cell annotations
    for i in range(len(policies)):
        for j in range(len(bounds)):
            v = grid[i][j]
            txt = "—" if (v != v) else f"{v:.2f}"
            # Pick readable text color depending on cell brightness
            def _ann_color(val):
                if val != val:  # NaN
                    return "black"
                lo, hi = im.get_clim()
                t = 0.5 if hi == lo else (val - lo) / (hi - lo)
                r, g, b, _ = im.cmap(t)
                luminance = 0.2126*r + 0.7152*g + 0.0722*b
                return "white" if luminance < 0.5 else "black"
            ax.text(j, i, txt, ha="center", va="center", fontsize=8, color=_ann_color(v))

    cbar = fig.colorbar(im, ax=ax)
    cbar.set_label(args.metric)

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    fig.tight_layout()
    fig.savefig(args.out)
    print("[ok] wrote", args.out)

if __name__ == "__main__":
    main()
