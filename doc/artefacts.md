```md
# artifacts.md — Paper artifact bundle map

This document describes the canonical *paper-ready* outputs under:

`out/artifacts/`

Use the short IDs (A1, F1, T1, …) to reference artifacts in the paper draft, internal notes, or reviewer responses.

 

## Quick index

### Figures
- **F1** — RD vs `bound_k` (SWinf): `fig/plot1_rd_vs_bound_swinf.pdf`
- **F2** — Tail-latency slice (SWinf, `fault_mode=NONE`, `k=inf`): `fig/plot2_latency_swinf_p95_k=inf_fault=NONE.pdf`

### Risk-Cliff tables
- **T1** — Risk-cliff aggregation (SWinf): `tab/risk_cliff_swinf.csv`
- **T2** — Risk-cliff markdown table (SWinf): `tab/risk_cliff_swinf.md`
- **T3** — Baseline table (SWinf, `fault_mode=NONE`, `k=inf`): `tab/table1_baseline_inf.csv`


### Poison schedules (case study)
- **P1** — Top-K poison table (SWinf, ranked by p95): `poison/poison_top_p95_swinf.csv` *(if present, otherwise see working copy `out/poison/poison_top_p95_swinf.csv`)*
- **P2** — Condensed poison traces (subset): `poison/traces/*.log`

### RDSS vs Uniform (method artifact, SW4)
- **R1** — RDSS run status summary: `rdss/rdss_sw4/rdss_status.csv`
- **R2** — RDSS top poison schedules: `rdss/rdss_sw4/top_poison.csv`
- **R3** — RDSS elite seeds list: `rdss/rdss_sw4/elite_seeds.txt`
- **R4** — RDSS per-round logs (repro details): `rdss/rdss_sw4/round_*/`

- **U1** — Uniform baseline status: `rdss/uniform_sw4/rdss_status.csv`
- **U2** — Uniform top poison schedules: `rdss/uniform_sw4/top_poison.csv`
- **U3** — Uniform elite seeds list: `rdss/uniform_sw4/elite_seeds.txt`
- **U4** — Uniform per-round logs: `rdss/uniform_sw4/round_*/`

### Option B (second seed generality check, SWinf)
- **B1** — Seed2 experiment config: `optionB_seed2/configs/main_seed2.yaml`
- **B2** — Seed2 run-level results: `optionB_seed2/csv/results_swinf_seed2.csv`
- **B3** — Seed2 aggregated summary: `optionB_seed2/csv/summary_swinf_seed2.csv`
- **B4** — Seed1 vs Seed2 p95-by-k comparison (fault=NONE): `optionB_seed2/tab/seed_compare_p95_none.txt`

### Base experiment config
- - **C1** — Main experiment matrix config (seed1): `configs/main.yaml` (repo root) + snapshot in `out/artifacts/configs/main.yaml`


 

## Folder-by-folder details

### `fig/` — paper figures (PDF)
Contains the two paper figures used in the main narrative:
- **F1** demonstrates that the reordering knob (`bound_k`) increases effective reordering (RD).
- **F2** shows tail impact (p95 latency) for the SWinf setting under `fault_mode=NONE` at `k=inf`.

### `tab/` — paper tables
- **T1/T2** are the risk-cliff “map”: p95/RD/exceed-rate across `bound_k`, faceted by policy×fault.

### `poison/` — poison schedules case study
- **P1** is the Top-K ranking of worst tail schedules (for SWinf).
- **P2** contains condensed traces for inspection and paper examples (RUN_HEADER/SUBMIT/COMPLETE/FENCE/RESET/RUN_END only).

### `rdss/` — method artifacts (SW4)
- `rdss_sw4/` stores the RDSS-driven sampling results.
- `uniform_sw4/` stores the uniform baseline with a matched budget.
Files **R1–R4** and **U1–U4** allow reproducing and comparing the two samplers.

### `optionB_seed2/` — second-seed check (SWinf)
Provides a controlled repeat of the SWinf matrix on a second workload seed:
- configs (B1), run-level CSV (B2), aggregated summary (B3),
- and the compact p95-by-k comparison for `fault_mode=NONE` (B4).

### `config/` — experiment config snapshot
This folder scontains the “seed1” main config snapshot used for the paper (**C1**).

 

## Notes / conventions
- **SWinf** refers to `submit_window = inf` and is treated as the paper’s primary stress setting.
- **SW2 / SW4** are considered ablations. RDSS artifacts are currently produced for **SW4**.
- `fault_mode=TIMEOUT` primarily exercises a fault path, main performance/tail claims are anchored on `fault_mode=NONE`.

 
```
