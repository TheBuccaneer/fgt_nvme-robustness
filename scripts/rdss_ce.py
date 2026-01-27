
#!/usr/bin/env python3
from __future__ import annotations
import argparse, csv, os, random, subprocess, sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple

@dataclass
class RunObs:
    schedule_seed: int
    tail_slack_step: float
    tail_exceed: int
    log_file: str

def run_one(config: Path, out_dir: Path, schedule_seed: int, submit_window: str, mode: str) -> None:
    """
    mode: "rust" or "c"
    Runs a single schedule_seed by setting schedule_seeds to "s-s".
    """
    tmp_cfg = out_dir / f"cfg_seed_{schedule_seed}.yaml"
    cfg_txt = config.read_text(encoding="utf-8")
    # replace schedule_seeds line (expects 'schedule_seeds:' exists)
    lines = []
    replaced = False
    for line in cfg_txt.splitlines():
        if line.strip().startswith("schedule_seeds:"):
            lines.append(f"schedule_seeds: \"{schedule_seed}-{schedule_seed}\"")
            replaced = True
        else:
            lines.append(line)
    if not replaced:
        raise SystemExit("[err] config missing schedule_seeds field")
    out_dir.mkdir(parents=True, exist_ok=True)
    tmp_cfg.write_text("\n".join(lines) + "\n", encoding="utf-8")

    if mode == "rust":
        cmd = ["./target/release/nvme-lite-oracle", "run-matrix",
               "--config", str(tmp_cfg), "--out-dir", str(out_dir), "--submit-window", str(submit_window)]
        cwd = Path.cwd()
    elif mode == "c":
        cmd = ["./nvme-lite-dut", "run-matrix",
               "--config", str(tmp_cfg), "--out-dir", str(out_dir), "--submit-window", str(submit_window)]
        cwd = Path.cwd() / "c_dut"
    else:
        raise SystemExit("[err] invalid mode")

    r = subprocess.run(cmd, cwd=str(cwd), capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout)
        print(r.stderr, file=sys.stderr)
        raise SystemExit(f"[err] run-matrix failed for seed={schedule_seed}")

def parse_csv(logs_dir: Path, out_csv: Path) -> None:
    cmd = [sys.executable, "scripts/01_parse_check.py", "--logs", str(logs_dir), "--out", str(out_csv)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout)
        print(r.stderr, file=sys.stderr)
        raise SystemExit("[err] parser failed")

def read_obs(csv_path: Path) -> List[RunObs]:
    out: List[RunObs] = []
    with csv_path.open(newline="", encoding="utf-8") as f:
        rd = csv.DictReader(f)
        for row in rd:
            try:
                ss = int(row.get("schedule_seed","0") or 0)
            except:
                continue
            try:
                slack = float(row.get("tail_slack_step","0") or 0.0)
            except:
                slack = 0.0
            try:
                te = int(float(row.get("tail_exceed","0") or 0))
            except:
                te = 0
            out.append(RunObs(ss, slack, te, row.get("log_file","")))
    return out

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", required=True, type=Path, help="base run-matrix yaml")
    ap.add_argument("--mode", choices=["rust","c"], default="rust")
    ap.add_argument("--out", required=True, type=Path, help="output dir for rdss runs")
    ap.add_argument("--submit-window", default="4")
    ap.add_argument("--pool", default="0-999", help="schedule_seed pool, e.g. 0-999")
    ap.add_argument("--rounds", type=int, default=6)
    ap.add_argument("--batch", type=int, default=60, help="runs per round")
    ap.add_argument("--rho", type=float, default=0.10, help="elite fraction")
    ap.add_argument("--explore", type=float, default=0.20, help="fraction uniform exploration each round")
    ap.add_argument("--seed", type=int, default=1, help="rng seed for the sampler")
    args = ap.parse_args()

    random.seed(args.seed)

    a,b = args.pool.split("-",1)
    pool = list(range(int(a), int(b)+1))
    if len(pool) < args.batch:
        raise SystemExit("[err] pool smaller than batch size")

    out_root: Path = args.out
    out_root.mkdir(parents=True, exist_ok=True)

    # tracking best known scores per schedule_seed
    seen: Dict[int, RunObs] = {}

    # initialize sampling distribution = uniform over pool
    elite: List[int] = []

    for r in range(args.rounds):
        round_dir = out_root / f"round_{r:02d}"
        logs_dir = round_dir
        csv_path = round_dir / "results.csv"

        # choose seeds
        n_explore = int(args.batch * args.explore)
        n_exploit = args.batch - n_explore

        chosen: List[int] = []
        # exploit from elite (with replacement)
        if elite and n_exploit > 0:
            for _ in range(n_exploit):
                chosen.append(random.choice(elite))
        else:
            # if no elite yet, fall back to uniform
            n_explore = args.batch
            n_exploit = 0

        # explore uniformly from pool
        chosen.extend(random.sample(pool, k=n_explore))

        # run each schedule_seed once (skip if already seen; but still allow re-sampling elites)
        for ss in chosen:
            # allow re-run elites to confirm; but keep it simple: skip if already ran once
            if ss in seen:
                continue
            run_one(args.config, logs_dir, ss, args.submit_window, args.mode)

        # parse and read observations (for this round dir)
        parse_csv(logs_dir, csv_path)
        obs = read_obs(csv_path)
        for o in obs:
            # keep best slack per seed (max)
            prev = seen.get(o.schedule_seed)
            if prev is None or o.tail_slack_step > prev.tail_slack_step:
                seen[o.schedule_seed] = o

        # compute elite set based on seen scores
        scored = sorted(seen.values(), key=lambda x: x.tail_slack_step, reverse=True)
        if not scored:
            raise SystemExit("[err] no observations parsed")

        elite_n = max(1, int(len(scored) * args.rho))
        elite = [o.schedule_seed for o in scored[:elite_n]]

        # write status
        status_path = out_root / "rdss_status.csv"
        write_header = not status_path.exists()
        with status_path.open("a", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            if write_header:
                w.writerow(["round","seen","elite_n","best_slack","best_seed","best_exceed"])
            best = scored[0]
            w.writerow([r, len(seen), elite_n, f"{best.tail_slack_step:.6f}", best.schedule_seed, best.tail_exceed])

        print(f"[rdss] round={r} seen={len(seen)} elite_n={elite_n} best_slack={scored[0].tail_slack_step:.3f} seed={scored[0].schedule_seed}")

    # dump final elite list
    elite_path = out_root / "elite_seeds.txt"
    with elite_path.open("w", encoding="utf-8") as f:
        for ss in elite:
            f.write(f"{ss}\n")
    print("[ok] wrote", elite_path)

    # dump top-50 poison schedules summary
    top_path = out_root / "top_poison.csv"
    top = sorted(seen.values(), key=lambda x: x.tail_slack_step, reverse=True)[:50]
    with top_path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["schedule_seed","tail_slack_step","tail_exceed","log_file"])
        for o in top:
            w.writerow([o.schedule_seed, f"{o.tail_slack_step:.6f}", o.tail_exceed, o.log_file])
    print("[ok] wrote", top_path)

    return 0

if __name__ == "__main__":
    raise SystemExit(main())
