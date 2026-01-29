#!/usr/bin/env python3
"""
01_parse_and_check.py

Parst NVMe-lite Oracle Logs -> out/csv/results.csv

- Liest RUN_HEADER / SUBMIT / COMPLETE / FENCE / RESET / RUN_END
- Prüft Minimal-Invarianten (7.14)
- Berechnet RD, FE, RCS (RCS aktuell sinnvoll v.a. für RESET; bei NONE = 1.0)
- Setzt run-level Flags: mismatch, timeout, crash

Usage:
  python3 scripts/01_parse_and_check.py --logs out/logs --out out/csv/results.csv
"""

from __future__ import annotations

import argparse
import csv
import math
import os
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple


# ----------------------------
# Helpers: Fenwick for RD
# ----------------------------
class Fenwick:
    def __init__(self, n: int):
        self.n = n
        self.bit = [0] * (n + 1)

    def add(self, i: int, delta: int = 1) -> None:
        i += 1  # to 1-based
        while i <= self.n:
            self.bit[i] += delta
            i += i & -i

    def sum(self, i: int) -> int:
        # prefix sum [0..i]
        i += 1
        s = 0
        while i > 0:
            s += self.bit[i]
            i -= i & -i
        return s

    def range_sum(self, l: int, r: int) -> int:
        if r < l:
            return 0
        return self.sum(r) - (self.sum(l - 1) if l > 0 else 0)


# ----------------------------
# Log parsing
# ----------------------------
RE_HEADER = re.compile(r"^RUN_HEADER\((.*)\)\s*$")
RE_SUBMIT = re.compile(r"^SUBMIT\(cmd_id=(\d+),\s*cmd_type=([A-Z_]+)\)\s*$")
RE_COMPLETE = re.compile(r"^COMPLETE\(cmd_id=(\d+),\s*status=([A-Z_]+),\s*out=([0-9]+)\)\s*$")
RE_FENCE = re.compile(r"^FENCE\(fence_id=(\d+)\)\s*$")
RE_RESET = re.compile(r"^RESET\(reason=([^,]+),\s*pending_before=(\d+)\)\s*$")
RE_RUN_END = re.compile(r"^RUN_END\(pending_left=(\d+),\s*pending_peak=(\d+)\)\s*$")


def parse_kv_list(s: str) -> Dict[str, str]:
    """
    Parse "k=v, a=b, ..." where values contain no commas (as in our logs).
    """
    out: Dict[str, str] = {}
    parts = [p.strip() for p in s.split(",")]
    for p in parts:
        if not p:
            continue
        if "=" not in p:
            continue
        k, v = p.split("=", 1)
        out[k.strip()] = v.strip()
    return out


@dataclass
class FenceInfo:
    fence_id: int
    fence_cmd_id: int
    fence_submit_pos: int


@dataclass
class RunData:
    # Header
    run_id: str = ""
    seed_id: str = ""
    schedule_seed: int = 0
    policy: str = ""
    bound_k: str = ""
    fault_mode: str = ""
    n_cmds: int = 0
    scheduler_version: str = ""
    git_commit: str = ""

    # Parsed events
    submit_order: List[int] = field(default_factory=list)  # cmd_id in submit order
    submit_pos: Dict[int, int] = field(default_factory=dict)  # cmd_id -> submit index
    cmd_type: Dict[int, str] = field(default_factory=dict)

    complete_order: List[int] = field(default_factory=list)  # cmd_id in completion order
    complete_pos: Dict[int, int] = field(default_factory=dict)  # cmd_id -> completion index
    status: Dict[int, str] = field(default_factory=dict)  # cmd_id -> status

    # Event-step tracking for true latency
    submit_step: Dict[int, int] = field(default_factory=dict)  # cmd_id -> event step
    complete_step: Dict[int, int] = field(default_factory=dict)  # cmd_id -> event step

    fences: List[FenceInfo] = field(default_factory=list)
    _last_fence_cmd_id: Optional[int] = None

    reset_pending_before: Optional[int] = None

    pending_left: Optional[int] = None
    pending_peak: Optional[int] = None
    pending_area: Optional[int] = None  # sum of pending over event steps
    pending_mean: Optional[float] = None


    # Flags / derived
    mismatch: int = 0
    timeout: int = 0
    crash: int = 0

    RD: float = 0.0
    FE: float = 1.0
    RCS: float = 1.0

    n_ok: int = 0
    n_err: int = 0
    n_timeout: int = 0

    # Invariant counters (paper-facing)
    viol_complete_unexpected: int = 0
    viol_reset_pending_mismatch: int = 0
    viol_bound_k_overflow: int = 0


def compute_rd(run: RunData) -> float:
    """
    RD = normalized inversion count between submit order and completion order.
    Uses completion list C; ranks by submit position.
    """
    C = run.complete_order
    m = len(C)
    if m < 2:
        return 0.0

    # Map completion sequence to submit ranks
    ranks: List[int] = []
    for cid in C:
        if cid in run.submit_pos:
            ranks.append(run.submit_pos[cid])
        else:
            # Unknown cmd_id already counts as mismatch; ignore in RD
            continue

    m2 = len(ranks)
    if m2 < 2:
        return 0.0

    # Compress ranks to 0..m2-1
    sorted_unique = sorted(set(ranks))
    comp = {v: i for i, v in enumerate(sorted_unique)}
    arr = [comp[r] for r in ranks]

    fw = Fenwick(len(sorted_unique))
    inv = 0
    seen = 0
    for x in arr:
        # inversions with previous > x
        inv += seen - fw.sum(x)
        fw.add(x, 1)
        seen += 1

    denom = m2 * (m2 - 1) / 2
    return float(inv) / float(denom) if denom > 0 else 0.0


def compute_fe(run: RunData) -> float:
    """
    FE = weighted fence constraint satisfaction.
    For each fence i:
      BEFORE_i = submitted before fence_submit_pos (excluding FENCE commands)
      AFTER_i  = submitted after fence_submit_pos (excluding FENCE commands)
      ok_i counts pairs (x in BEFORE, y in AFTER) with complete_pos[x] < complete_pos[y]
    Aggregate: FE = sum(ok_i) / sum(total_i). If no constraints, FE = 1.
    """
    if not run.fences:
        return 1.0

    # Completion position; missing -> inf
    def cpos(cid: int) -> float:
        return float(run.complete_pos.get(cid, math.inf))

    ok_sum = 0
    total_sum = 0

    for f in run.fences:
        before: List[int] = []
        after: List[int] = []

        for cid, sp in run.submit_pos.items():
            ctype = run.cmd_type.get(cid, "")
            if ctype == "FENCE":
                continue  # fence command itself is not constrained
            if sp < f.fence_submit_pos:
                before.append(cid)
            elif sp > f.fence_submit_pos:
                after.append(cid)

        total_i = len(before) * len(after)
        if total_i == 0:
            continue

        # Count satisfied pairs
        for x in before:
            cx = cpos(x)
            for y in after:
                cy = cpos(y)
                if cx < cy:
                    ok_sum += 1

        total_sum += total_i

    if total_sum == 0:
        return 1.0
    return ok_sum / total_sum


def compute_rcs(run: RunData) -> float:
    """
    RCS: for RESET runs:
      expected = pending_before (from RESET event)
      left     = pending_left (from RUN_END)
      RCS = (expected - left) / expected, with expected==0 -> 1
    For NONE: 1.0
    For TIMEOUT: left as 1.0 unless you later add timeout snapshot.
    """
    if run.fault_mode != "RESET":
        return 1.0

    expected = run.reset_pending_before if run.reset_pending_before is not None else 0
    left = run.pending_left if run.pending_left is not None else 0

    if expected == 0:
        return 1.0

    rcs = (expected - left) / expected
    # Clamp
    if rcs < 0.0:
        rcs = 0.0
    if rcs > 1.0:
        rcs = 1.0
    return float(rcs)


def parse_log_file(path: Path) -> RunData:
    run = RunData()
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()

    # Event step counter (increments for SUBMIT/COMPLETE/FENCE)
    event_step = 0

    # Invariant counters (paper-facing)
    outstanding = set()
    viol_complete_unexpected = 0
    viol_reset_pending_mismatch = 0
    viol_submit_window_overflow = 0
    viol_bound_k_overflow = 0

    # Crash detection: if missing RUN_END, we'll set crash=1 later.
    for line in lines:
        line = line.strip()
        if not line:
            continue

        m = RE_HEADER.match(line)
        if m:
            kv = parse_kv_list(m.group(1))
            run.run_id = kv.get("run_id", run.run_id)
            run.seed_id = kv.get("seed_id", run.seed_id)
            run.schedule_seed = int(kv.get("schedule_seed", "0") or "0")
            run.policy = kv.get("policy", run.policy)
            run.bound_k = kv.get("bound_k", run.bound_k)
            run.fault_mode = kv.get("fault_mode", run.fault_mode)
            run.n_cmds = int(kv.get("n_cmds", "0") or "0")
            run.scheduler_version = kv.get("scheduler_version", run.scheduler_version)
            run.git_commit = kv.get("git_commit", run.git_commit)
            continue

        m = RE_SUBMIT.match(line)
        if m:
            cid = int(m.group(1))
            ctype = m.group(2)            # Invariants: track outstanding (exclude FENCE from backlog)
            if ctype != "FENCE":
                outstanding.add(cid)
                # bound_k from header; treat 'inf' as None
                bk = None
                try:
                    if run.bound_k not in (None, "", "inf"):
                        bk = int(run.bound_k)
                except Exception:
                    bk = None
                if bk is not None and len(outstanding) > bk:
                    viol_bound_k_overflow += 1

            if cid in run.submit_pos:
                run.mismatch = 1  # duplicate SUBMIT
            else:
                run.submit_pos[cid] = len(run.submit_order)
                run.submit_order.append(cid)
                run.cmd_type[cid] = ctype
                run.submit_step[cid] = event_step

            # Track fence-cmd to associate with next FENCE(fence_id=...)
            if ctype == "FENCE":
                run._last_fence_cmd_id = cid
            event_step += 1
            continue

        m = RE_FENCE.match(line)
        if m:
            fence_id = int(m.group(1))
            if run._last_fence_cmd_id is None:
                # fence without a preceding fence-cmd submit
                run.mismatch = 1
                # still record boundary at current submit length
                run.fences.append(FenceInfo(fence_id=fence_id, fence_cmd_id=-1, fence_submit_pos=len(run.submit_order)))
            else:
                fc = run._last_fence_cmd_id
                fpos = run.submit_pos.get(fc, len(run.submit_order))
                run.fences.append(FenceInfo(fence_id=fence_id, fence_cmd_id=fc, fence_submit_pos=fpos))
                run._last_fence_cmd_id = None
            event_step += 1
            continue

        m = RE_COMPLETE.match(line)
        if m:
            cid = int(m.group(1))
            st = m.group(2)
            # out = m.group(3)  # not needed for metrics here, but parsed by regex

            # Invariants: COMPLETE must refer to outstanding (exclude FENCE)
            if run.cmd_type.get(cid) != "FENCE":
                if cid not in outstanding:
                    viol_complete_unexpected += 1
                else:
                    outstanding.remove(cid)

            if cid not in run.submit_pos:
                run.mismatch = 1  # complete for unknown cmd_id

            if cid in run.complete_pos:
                run.mismatch = 1  # duplicate COMPLETE
            else:
                run.complete_pos[cid] = len(run.complete_order)
                run.complete_order.append(cid)
                run.status[cid] = st
                run.complete_step[cid] = event_step

            if st == "OK":
                run.n_ok += 1
            elif st == "ERR":
                run.n_err += 1
            elif st == "TIMEOUT":
                run.n_timeout += 1
                run.timeout = 1

            event_step += 1
            continue

        m = RE_RESET.match(line)
        if m:
            # reason = m.group(1).strip()
            pending_before = int(m.group(2))
            run.reset_pending_before = pending_before
            # Invariants: RESET(pending_before=X) should match outstanding backlog
            if pending_before != len(outstanding):
                viol_reset_pending_mismatch += 1
            outstanding.clear()
            event_step += 1
            continue

        m = RE_RUN_END.match(line)
        if m:
            run.pending_left = int(m.group(1))
            run.pending_peak = int(m.group(2))
            continue

        # Unknown line: ignore (but you can choose to mismatch=1)
        # run.mismatch = 1

    # Post checks
    if run.pending_left is None or run.pending_peak is None:
        run.crash = 1
        run.mismatch = 1  # crash implies mismatch per spec

    if run.fault_mode == "NONE" and run.pending_left is not None and run.pending_left > 0:
        run.mismatch = 1

    # Derive pending_area / pending_mean from SUBMIT/COMPLETE sequence.
    # We exclude FENCE from backlog accounting so "pending" reflects in-flight I/O commands.
    pending = 0
    area = 0
    steps = 0
    for line in lines:
        line = line.strip()

        m = RE_SUBMIT.match(line)
        if m:
            cid = int(m.group(1))
            ctype = m.group(2)
            if ctype == "FENCE":
                continue
            pending += 1
            area += pending
            steps += 1
            continue

        m = RE_COMPLETE.match(line)
        if m:
            cid = int(m.group(1))
            if run.cmd_type.get(cid) == "FENCE":
                continue
            pending = max(0, pending - 1)
            area += pending
            steps += 1
            continue
    run.pending_area = area
    run.pending_mean = (area / steps) if steps > 0 else 0.0

    # Compute metrics
    run.RD = compute_rd(run)
    run.FE = compute_fe(run)
    run.RCS = compute_rcs(run)

    # Store invariant counters
    run.viol_complete_unexpected = viol_complete_unexpected
    run.viol_reset_pending_mismatch = viol_reset_pending_mismatch
    run.viol_bound_k_overflow = viol_bound_k_overflow

    return run


def iter_log_files(logs_path: Path) -> List[Path]:
    if logs_path.is_file():
        return [logs_path]
    files = sorted([p for p in logs_path.rglob("*.log") if p.is_file()])
    return files


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--logs", required=True, help="Log file or directory (e.g., out/logs)")
    ap.add_argument("--out", required=True, help="Output CSV path (e.g., out/csv/results.csv)")
    args = ap.parse_args()

    logs_path = Path(args.logs)
    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    files = iter_log_files(logs_path)
    if not files:
        print(f"[error] No .log files found under: {logs_path}")
        return 2

    # Keep (run, file) pairs to avoid misalignment when some logs fail parsing
    pairs: List[Tuple[RunData, Path]] = []
    failed = 0

    for f in files:
        try:
            run = parse_log_file(f)
            pairs.append((run, f))
        except Exception as e:
            failed += 1
            print(f"[error] Failed parsing {f}: {e}")
            continue

    if not pairs:
        print(f"[error] All logs failed parsing under: {logs_path}")
        return 2

    fieldnames = [
        "run_id",
        "seed_id",
        "schedule_seed",
        "policy",
        "bound_k",
        "fault_mode",
        "n_cmds",
        "scheduler_version",
        "git_commit",
        "mismatch",
        "timeout",
        "crash",
        "pending_left",
        "pending_peak",
        "pending_area",
        "pending_mean",
        "RD",
        "FE",
        "RCS",
        "completion_rate",
        "mean_latency_disp",
        "p95_latency_disp",
        "max_latency_disp",
        "mean_latency_step",
        "p95_latency_step",
        "max_latency_step",
        "n_ok",
        "n_err",
        "n_timeout",
        "n_fences",
        "viol_complete_unexpected",
        "viol_reset_pending_mismatch",
        "viol_bound_k_overflow",
        "tail_budget_step",
        "tail_slack_step",
        "tail_exceed",
        "log_file",
    ]

    with out_path.open("w", newline="", encoding="utf-8") as fp:
        w = csv.DictWriter(fp, fieldnames=fieldnames)
        w.writeheader()
        for r, f in pairs:
            completion_rate = (r.n_ok / r.n_cmds) if r.n_cmds > 0 else 1.0
            
            # Compute latency stats (exclude FENCE cmds)
            # Displacement latency (rank-based)
            latencies_disp = []
            # True step latency (event-step based)
            latencies_step = []
            
            for cid, sp in r.submit_pos.items():
                if r.cmd_type.get(cid) == "FENCE":
                    continue
                cp = r.complete_pos.get(cid)
                if cp is None:
                    continue  # not completed (timeout/crash)
                
                # Displacement (can be negative)
                latencies_disp.append(cp - sp)
                
                # True step latency (always >= 0)
                step_sub = r.submit_step.get(cid)
                step_com = r.complete_step.get(cid)
                if step_sub is not None and step_com is not None:
                    if step_com < step_sub:
                        r.mismatch = 1
                        continue  # skip corrupted ordering for this cmd_id
                    latencies_step.append(step_com - step_sub)
            
            # Stats for displacement
            latencies_disp.sort()
            if latencies_disp:
                mean_latency_disp = sum(latencies_disp) / len(latencies_disp)
                max_latency_disp = float(latencies_disp[-1])
                idx = int(0.95 * (len(latencies_disp) - 1))
                p95_latency_disp = float(latencies_disp[idx])
            else:
                mean_latency_disp = 0.0
                p95_latency_disp = 0.0
                max_latency_disp = 0.0
            
            # Stats for true step latency
            latencies_step.sort()
            if latencies_step:
                mean_latency_step = sum(latencies_step) / len(latencies_step)
                max_latency_step = float(latencies_step[-1])
                idx = int(0.95 * (len(latencies_step) - 1))
                p95_latency_step = float(latencies_step[idx])
            else:
                mean_latency_step = 0.0
                p95_latency_step = 0.0
                max_latency_step = 0.0
            
            # Tail-exceedance (paper-facing)
            bk_int = None
            try:
                if r.bound_k not in (None, '', 'inf'):
                    bk_int = int(r.bound_k)
            except Exception:
                bk_int = None
            if bk_int is not None:
                tail_budget_step = 2 * bk_int + 3
            else:
                peak = int(r.pending_peak) if r.pending_peak is not None else 0
                tail_budget_step = 2 * peak + 3
            tail_slack_step = float(p95_latency_step) - float(tail_budget_step)
            tail_exceed = 1 if tail_slack_step > 0 else 0

            w.writerow(
                {
                    "run_id": r.run_id,
                    "seed_id": r.seed_id,
                    "schedule_seed": r.schedule_seed,
                    "policy": r.policy,
                    "bound_k": r.bound_k,
                    "fault_mode": r.fault_mode,
                    "n_cmds": r.n_cmds,
                    "scheduler_version": r.scheduler_version,
                    "git_commit": r.git_commit,
                    "mismatch": r.mismatch,
                    "timeout": r.timeout,
                    "crash": r.crash,
                    "pending_left": "" if r.pending_left is None else r.pending_left,
                    "pending_peak": "" if r.pending_peak is None else r.pending_peak,
                    "pending_area": "" if r.pending_area is None else r.pending_area,
                    "pending_mean": "" if r.pending_mean is None else f"{r.pending_mean:.6f}",
                    "RD": f"{r.RD:.6f}",
                    "FE": f"{r.FE:.6f}",
                    "RCS": f"{r.RCS:.6f}",
                    "completion_rate": f"{completion_rate:.6f}",
                    "mean_latency_disp": f"{mean_latency_disp:.6f}",
                    "p95_latency_disp": f"{p95_latency_disp:.6f}",
                    "max_latency_disp": f"{max_latency_disp:.6f}",
                    "mean_latency_step": f"{mean_latency_step:.6f}",
                    "p95_latency_step": f"{p95_latency_step:.6f}",
                    "tail_budget_step": tail_budget_step,
                    "tail_slack_step": f"{tail_slack_step:.6f}",
                    "tail_exceed": tail_exceed,
                    "max_latency_step": f"{max_latency_step:.6f}",
                    "n_ok": r.n_ok,
                    "n_err": r.n_err,
                    "n_timeout": r.n_timeout,
                    "n_fences": len(r.fences),
                    "log_file": str(f),
                }
            )

    print(f"[ok] Parsed {len(pairs)} logs ({failed} failed) -> {out_path}")
    return 0



if __name__ == "__main__":
    raise SystemExit(main())
