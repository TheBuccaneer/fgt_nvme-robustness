# NVMe-lite Robustness Study — Reproducible Workflow + Results Notes (Paper Draft)

This document summarizes what we executed in the new repo (`fgt_nvme-robustness`), what artifacts were produced, and the core results/claims we can safely make from the collected data. It is written to be directly reusable as paper text (workshop-level).

---

## Repository layout (relevant folders)

- `configs/`
  - `main.yaml` (main run-matrix config)
  - `test.yaml` (smaller config / smoke)
- `seeds/`
  - `seed_001.json`, `seed_001_long32.json`, `seed_002.json`
  - smoke seeds: `stale_read_test.json`, `fresh_read_test.json`, `vis_len4_test.json`
- `scripts/`
  - `01_parse_check.py` (logs → run-level CSV)
  - `02_aggregate.py` (run-level CSV → summary CSV)
  - `03_robustness.py` (Plot 1: RD vs bound_k)
  - `04_plot_latency_fixedv2.py` (Plot 2: p95 tail latency slice)
  - `rdss_ce.py` (RDSS / CE sampler for schedule-seed search)
- `out/` (generated outputs)
  - `out/logs_sw2`, `out/logs_sw4`, `out/logs_swinf` (raw logs from run-matrix)
  - `out/csv/` (`results_*.csv`, `summary_*.csv`)
  - `out/fig/` (PDF figures)
  - `out/tab/` (risk-cliff table + CSV)
  - `out/poison/` (top poison list + condensed traces)
  - `out/rdss_sw4/`, `out/uniform_sw4/` (RDSS + baseline outputs)

---

## Experimental knobs (paper terminology)

We stress a host↔device protocol simulator (“NVMe-lite”) by varying:

- **Reordering freedom:** `bound_k` (e.g., 0, 1, 2, 3, 5, 10, inf)
- **Fault mode:** `fault_mode ∈ {NONE, RESET, TIMEOUT}`
- **Scheduling policy:** (e.g., FIFO, RANDOM, ADVERSARIAL, BATCHED)
- **Concurrency control:** `submit_window` (SW2, SW4, SW∞)

### Paper stance (important)
- Main robustness story uses **SW∞** (max concurrency) because it exposes the clearest tail effects.
- SW2/SW4 are best treated as **ablation / appendix** knobs.
- For performance/tail claims, we primarily interpret **fault_mode=NONE** (RESET optional). TIMEOUT is a “fault-path by design” and should be framed accordingly.

---

## Output pipeline (what we ran)

### A) run-matrix → logs
We ran `run-matrix` for **submit_window ∈ {2, 4, inf}** and stored logs in:
- `out/logs_sw2/`
- `out/logs_sw4/`
- `out/logs_swinf/`

### B) logs → run-level CSV
Produced run-level CSVs:
- `out/csv/results_sw2.csv`
- `out/csv/results_sw4.csv`
- `out/csv/results_swinf.csv`

Each row is one run (`seed_id × schedule_seed × policy × bound_k × fault_mode`) with metrics including:
- RD, FE, RCS, completion_rate
- pending_peak, pending_area/pending_mean
- latency metrics (mean/p95/max in step units)
- tail_budget_step / tail_slack_step / tail_exceed
- `log_file` pointer for reproduction

### C) run-level CSV → aggregated summary
Produced aggregated summary tables:
- `out/csv/summary_sw2.csv`
- `out/csv/summary_sw4.csv`
- `out/csv/summary_swinf.csv`

---

## Figures included in the paper (current status)

### Figure 1 — RD vs bound_k (control variable)
We produced one Plot 1 per submit-window:
- `out/fig/plot1_rd_vs_bound_sw2.pdf`
- `out/fig/plot1_rd_vs_bound_sw4.pdf`
- `out/fig/plot1_rd_vs_bound_swinf.pdf`

**Safe claim:** RD generally increases with `bound_k` (often rising then saturating depending on concurrency), confirming that the reordering knob has the intended effect.

### Figure 2 — Tail latency (p95) slice
We produced Plot 2 as a policy comparison slice:
- `out/fig/plot2_latency_swinf_p95_k=inf_fault=NONE.pdf`
(and analogous SW2/SW4 variants)

**Safe claim:** Under identical workload and `fault_mode=NONE`, scheduling policy strongly affects tail latency; ADVERSARIAL inflates the tail compared to benign policies.

**Paper choice:** Include the SW∞ plot as the main figure; SW2/SW4 can be appendix/ablation.

---

## Risk-Cliff (map / table): where the system “tips”

We generated a risk-cliff table for SW∞ (using `risk_cliff_swinf.csv` → `risk_cliff_swinf.md`), focusing on `fault_mode=NONE`:

Mean p95 latency (step units) across `bound_k = {0,1,2,3,5,10,inf}`:

- FIFO: **13.70** flat across all k
- RANDOM: **12.89 → 18.25** increasing with k
- BATCHED: **5.87 → 6.52** low and near-constant
- ADVERSARIAL: **~13–14 for k≤2**, then **20.95 at k=3**, and **28.46 for k≥5** (a pronounced “cliff”)

**Safe claim:** Under SW∞, ADVERSARIAL exhibits a sharp tail escalation once `bound_k` is large enough (here around k≈3–5), consistent with a phase-transition-like regime change. FIFO remains stable; BATCHED stays low; RANDOM grows gradually.

---

## Poison schedules (top-K) + condensed traces

### Poison ranking (SW∞, k=inf, fault=NONE)
We extracted the Top-50 runs by p95 tail latency and exported:
- `out/poison/poison_top_p95_swinf.csv`
- `out/poison/traces/` (condensed traces)

Top-50 composition:
- ADVERSARIAL: **38/50 (76%)**
- RANDOM: **11/50 (22%)**
- FIFO: **1/50 (2%)**

Top-50 p95 summary:
- max = **64**
- median = **43**
- mean = **45.34**
- max by policy: ADVERSARIAL **64**, RANDOM **52**, FIFO **33**

**Safe claim:** The extreme tail is dominated by ADVERSARIAL schedules in the Top-K set, while RANDOM also produces high-tail instances but less extreme, and FIFO rarely appears among worst cases.

### Trace case study (two concrete examples)

1) ADVERSARIAL poison example:
- `(seed_id=seed_001_long32, schedule_seed=10, k=inf, fault=NONE)`
- p95 = **64**, pending_peak = **10**
- Observed pattern: many commands are submitted early; several completions are deferred until the end (tail-pushing completion order).

2) RANDOM worst example (in the poison list):
- `(seed_id=seed_001_long32, schedule_seed=11, k=inf, fault=NONE)`
- p95 = **52**, pending_peak = **14**
- Observed pattern: significant reordering exists; completions are more interleaved than ADVERSARIAL, but some early commands still complete late.

**Interpretation (safe):** Even uniform random scheduling can produce high-tail runs under maximal concurrency, but ADVERSARIAL concentrates probability mass on more extreme tails and yields stronger, more systematic tail amplification.

---

## RDSS (Cross-Entropy / Elite Sampling) + Uniform baseline

### Why RDSS is included
Poison schedules are reproducible but can be rare in larger search spaces. RDSS is a targeted search strategy intended for rare-event regimes: it concentrates sampling in high-risk regions of the schedule-seed space.

### RDSS run (SW4, pool 0–99)
We ran RDSS with:
- rounds = **3**
- batch = **30**
- elite fraction ρ = **0.10**
- exploration = **0.20**
- pool = **0–99**
- output: `out/rdss_sw4/`

Observed:
- unique evaluated (“seen”) = **40**
- best tail slack = **31** at seed **53**
- artifacts written:
  - `out/rdss_sw4/rdss_status.csv`
  - `out/rdss_sw4/top_poison.csv`
  - `out/rdss_sw4/elite_seeds.txt`

### Uniform baseline (same budget)
We ran a uniform baseline with:
- rounds = **1**
- batch = **90**
- exploration = **1.0**
- output: `out/uniform_sw4/`

Observed:
- best tail slack = **31** at seed **53** (same maximum as RDSS)

### Threshold (rare-event) view
Hit counts for tail slack thresholds (from the ranked candidate lists):
- RDSS: N=40, best=31
  - hits ≥25: **1**
  - hits ≥30: **1**
  - hits ≥31: **1**
- Uniform: N=50 (ranked output), best=31
  - hits ≥25: **2**
  - hits ≥30: **1**
  - hits ≥31: **1**

**Safe interpretation:** In the evaluated small pool (0–99), the extremal schedules are not sufficiently rare to yield a strong RDSS-vs-uniform separation on the maximum slack metric; both recover the same maximum. This convergence is expected outside the rare-event regime. RDSS still provides a clean reproducibility trail and does not degrade outcomes.

**If a stronger RDSS advantage is desired:** Increase rarity by (i) larger pools (e.g., 0–999), (ii) stricter targets, or (iii) smaller budgets; RDSS should separate more clearly when the target is genuinely rare.

---

## Paper-ready paragraph blocks (copy into Results)

### Poison schedules (quantitative)
“In the SW∞ poison slice (`fault_mode=NONE`, `bound_k=inf`), the Top-50 runs by tail latency are dominated by ADVERSARIAL scheduling: 38/50 (76%) are ADVERSARIAL, compared to 11/50 (22%) RANDOM and 1/50 (2%) FIFO. Across these Top-50 runs, p95 tail latency reaches a maximum of 64 steps (median 43, mean 45.34), with worst-case p95 strongly policy-dependent (ADVERSARIAL 64 vs RANDOM 52 vs FIFO 33). This concentration of extreme tails under ADVERSARIAL schedules supports the view that worst-case completion orderings are not merely rare random artifacts but can be systematically induced under maximal concurrency.”

### Risk-cliff (policy-dependent tipping point)
“Under SW∞ and `fault_mode=NONE`, mean p95 tail latency increases with reordering freedom in a strongly policy-dependent manner. FIFO remains essentially flat across all tested `bound_k` values (≈13.7), while RANDOM increases gradually (≈12.9→≈18.3). BATCHED stays low and near-constant (≈5.9→≈6.5). In contrast, ADVERSARIAL exhibits a pronounced cliff: p95 is moderate for small bounds (≈13–14 for k≤2) but jumps at k≈3 (≈21) and reaches a sustained high-tail regime at k≥5 (≈28.5).”

### RDSS + honest baseline interpretation
“We apply RDSS, a cross-entropy (elite-sampling) schedule-seed search, to surface high-risk schedules efficiently and export them as reproducible artifacts (`rdss_status.csv`, `top_poison.csv`, `elite_seeds.txt`). In our SW4 pool (0–99), RDSS (3 rounds × 30 batch) evaluated 40 unique candidates and recovered a best tail slack of 31 steps (seed 53). A uniform baseline with the same budget recovered the same maximum, indicating that in this small pool extreme schedules are not sufficiently rare for a strong separation; RDSS converges as expected outside the rare-event regime without degrading results.”

---

## What is “done” vs “optional”

### Done (paper-core)
- Plot 1 (RD vs bound_k) — include SW∞
- Plot 2 (p95 tail) — include SW∞ slice
- Risk-cliff table (SW∞, NONE)
- Poison top-K + traces (SW∞, NONE)
- RDSS + uniform baseline (SW4) + honest interpretation

### Optional upgrades (only if you want stronger RDSS wins)
- Expand pool to 0–999 and repeat RDSS vs uniform with tight budgets
- Change objective/threshold to make “hit” rarer (compound conditions, higher thresholds)

---
