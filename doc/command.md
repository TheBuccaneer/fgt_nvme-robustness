# commands.md — nvme-lite robustness (repro / paper)

This file documents a minimal, reproducible pipeline:
Build → run-matrix (SW2/SW4/SWinf) → parse → aggregate → Plot1/Plot2 → Risk-Cliff → Poison → RDSS vs Uniform → (Option B seed2).

Assumptions:
- Run commands from repo root (contains `Cargo.toml`)
- Python scripts are in `scripts/`
- Outputs are written to `out/`

---

## 0) Python environment (venv)

python3 -m venv .venv
source .venv/bin/activate
python -m pip install -U pip
python -m pip install matplotlib

---

## 1) Build (Rust)

cargo build --release

Binary:
./target/release/nvme-lite-oracle

---

## 2) Run experiments (run-matrix)

Template:
./target/release/nvme-lite-oracle run-matrix --config <CONFIG_YAML> --out-dir <LOG_DIR> --submit-window <2|4|inf>

### SW2
mkdir -p out/logs_sw2
./target/release/nvme-lite-oracle run-matrix --config configs/main.yaml --out-dir out/logs_sw2 --submit-window 2

### SW4
mkdir -p out/logs_sw4
./target/release/nvme-lite-oracle run-matrix --config configs/main.yaml --out-dir out/logs_sw4 --submit-window 4

### SWinf (paper main)
mkdir -p out/logs_swinf
./target/release/nvme-lite-oracle run-matrix --config configs/main.yaml --out-dir out/logs_swinf --submit-window inf

---

## 3) Parse logs → run-level CSV (01_parse_check)

python3 scripts/01_parse_check.py --logs <LOG_DIR> --out <CSV_OUT>

mkdir -p out/csv
python3 scripts/01_parse_check.py --logs out/logs_sw2   --out out/csv/results_sw2.csv
python3 scripts/01_parse_check.py --logs out/logs_sw4   --out out/csv/results_sw4.csv
python3 scripts/01_parse_check.py --logs out/logs_swinf --out out/csv/results_swinf.csv

---

## 4) Aggregate results → summary CSV (02_aggregate)

python3 scripts/02_aggregate.py --in <RESULTS_CSV> --out <SUMMARY_CSV>

python3 scripts/02_aggregate.py --in out/csv/results_sw2.csv   --out out/csv/summary_sw2.csv
python3 scripts/02_aggregate.py --in out/csv/results_sw4.csv   --out out/csv/summary_sw4.csv
python3 scripts/02_aggregate.py --in out/csv/results_swinf.csv --out out/csv/summary_swinf.csv

---

## 5) Plot 1 — RD vs bound_k (03_robustness)

python3 scripts/03_robustness.py --in <SUMMARY_CSV> --out <PLOT_OUT_PDF>

mkdir -p out/fig
python3 scripts/03_robustness.py --in out/csv/summary_sw2.csv   --out out/fig/plot1_rd_vs_bound_sw2.pdf
python3 scripts/03_robustness.py --in out/csv/summary_sw4.csv   --out out/fig/plot1_rd_vs_bound_sw4.pdf
python3 scripts/03_robustness.py --in out/csv/summary_swinf.csv --out out/fig/plot1_rd_vs_bound_swinf.pdf

---

## 6) Plot 2 — p95 latency slice (04_plot_latency_fixedv2)

python3 scripts/04_plot_latency_fixedv2.py --in <RESULTS_CSV> --out <PLOT_OUT_PDF> \
  --metric p95_latency_step --bound-k inf --fault-mode NONE

python3 scripts/04_plot_latency_fixedv2.py --in out/csv/results_swinf.csv --out out/fig/plot2_latency_swinf_p95_k=inf_fault=NONE.pdf \
  --metric p95_latency_step --bound-k inf --fault-mode NONE

---

## 7) Risk-Cliff (SWinf) — aggregate + markdown table

python3 scripts/05_risk_cliff_aggregate.py --in out/csv/results_swinf.csv --out out/tab/risk_cliff_swinf.csv
python3 scripts/06_risk_cliff_table.py     --in out/tab/risk_cliff_swinf.csv --out out/tab/risk_cliff_swinf.md

mkdir -p out/tab
python3 scripts/05_risk_cliff_aggregate.py --in out/csv/results_swinf.csv --out out/tab/risk_cliff_swinf.csv
python3 scripts/06_risk_cliff_table.py     --in out/tab/risk_cliff_swinf.csv --out out/tab/risk_cliff_swinf.md

---

## 8) Poison schedules (SWinf) — Top-K + condensed traces

python3 scripts/07_poison_top_p95.py --in out/csv/results_swinf.csv --out out/poison/poison_top_p95_swinf.csv --k 50
python3 scripts/08_poison_export_traces.py --in out/poison/poison_top_p95_swinf.csv --out-dir out/poison/traces

mkdir -p out/poison
python3 scripts/07_poison_top_p95.py --in out/csv/results_swinf.csv --out out/poison/poison_top_p95_swinf.csv --k 50
python3 scripts/08_poison_export_traces.py --in out/poison/poison_top_p95_swinf.csv --out-dir out/poison/traces

---

## 9) RDSS vs Uniform baseline (SW4)

Note: rdss_ce.py requires integer submit-window; SWinf is not supported here.

### RDSS
rm -rf out/rdss_sw4
python3 scripts/rdss_ce.py --config configs/main.yaml --mode rust --out out/rdss_sw4 --submit-window 4 \
  --pool 0-99 --rounds 3 --batch 30 --rho 0.10 --explore 0.20 --seed 0

### Uniform baseline (matched total budget)
rm -rf out/uniform_sw4
python3 scripts/rdss_ce.py --config configs/main.yaml --mode rust --out out/uniform_sw4 --submit-window 4 \
  --pool 0-99 --rounds 1 --batch 90 --rho 0.10 --explore 1.00 --seed 0

### Compare RDSS vs Uniform (threshold hit counts)
# requires helper script: scripts/09_rdss_compare_thresholds.py
python3 scripts/09_rdss_compare_thresholds.py --rdss out/rdss_sw4/top_poison.csv --uniform out/uniform_sw4/top_poison.csv --thresholds 25 30 31

---

## 10) Option B — second workload seed (SWinf)

### Create config (copy + edit)
cp -a configs/main.yaml configs/main_seed2.yaml
# edit configs/main_seed2.yaml: set seeds: - "seeds/seed_002.json" (only)

### Run SWinf for seed2
rm -rf out/logs_swinf_seed2
mkdir -p out/logs_swinf_seed2
./target/release/nvme-lite-oracle run-matrix --config configs/main_seed2.yaml --out-dir out/logs_swinf_seed2 --submit-window inf

### Parse + aggregate
python3 scripts/01_parse_check.py --logs out/logs_swinf_seed2 --out out/csv/results_swinf_seed2.csv
python3 scripts/02_aggregate.py  --in out/csv/results_swinf_seed2.csv --out out/csv/summary_swinf_seed2.csv

### Seed1 vs Seed2 p95-by-k comparison (fault=NONE)
# requires helper script: scripts/10_seed_compare_p95_none.py
python3 scripts/10_seed_compare_p95_none.py --seed1 out/csv/results_swinf.csv --seed2 out/csv/results_swinf_seed2.csv --out out/tab/seed_compare_p95_none.txt

---

## 11) Copy paper artifacts into out/artifacts/

mkdir -p out/artifacts/{fig,tab,poison,rdss,optionB_seed2}

cp -a out/fig/plot1_rd_vs_bound_swinf.pdf out/fig/plot2_latency_swinf_p95_k=inf_fault=NONE.pdf out/artifacts/fig/
cp -a out/tab/risk_cliff_swinf.csv out/tab/risk_cliff_swinf.md out/artifacts/tab/
cp -a out/poison/poison_top_p95_swinf.csv out/artifacts/poison/
cp -a out/poison/traces out/artifacts/poison/

cp -a out/rdss_sw4 out/artifacts/rdss/rdss_sw4
cp -a out/uniform_sw4 out/artifacts/rdss/uniform_sw4

mkdir -p out/artifacts/optionB_seed2/{configs,csv,tab}
cp -a configs/main_seed2.yaml out/artifacts/optionB_seed2/configs/
cp -a out/csv/results_swinf_seed2.csv out/csv/summary_swinf_seed2.csv out/artifacts/optionB_seed2/csv/
cp -a out/tab/seed_compare_p95_none.txt out/artifacts/optionB_seed2/tab/


---

## 12) C-DUT validation

Goal: Show that the “poison schedules” are not an oracle artifact by replaying the same (seed_id, policy, schedule_seed) on the independent C DUT and diffing event traces.

### 1) Build C DUT
cd c_dut
make clean
make
cd ..

### 2) Run validation: top-K poison (fault=NONE, k=inf, SWinf) on Rust + C, then diff “short traces”
mkdir -p out/c_dut_val
python3 -c 'import csv,subprocess,difflib; from pathlib import Path; K=10; inp=Path("out/poison/poison_top_p95_swinf.csv"); out=Path("out/c_dut_val"); (out/"rust").mkdir(parents=True,exist_ok=True); (out/"c").mkdir(parents=True,exist_ok=True); (out/"short").mkdir(parents=True,exist_ok=True); (out/"diff").mkdir(parents=True,exist_ok=True); rows=list(csv.DictReader(inp.open(newline="",encoding="utf-8")))[:K]; keep=("RUN_HEADER(","SUBMIT(","COMPLETE(","FENCE(","RESET(","RUN_END("); summary=[]; 
def short(src,dst): 
 p=Path(src); q=Path(dst); q.write_text("".join([ln for ln in p.read_text(encoding="utf-8",errors="replace").splitlines(True) if ln.startswith(keep)]), encoding="utf-8")
for i,r in enumerate(rows,1):
 seed_id=r["seed_id"]; ss=r["schedule_seed"]; pol=r["policy"]; seed_path=f"seeds/{seed_id}.json"; rust_log=out/"rust"/f"rust_{seed_id}_{pol}_{ss}.log"; c_log=out/"c"/f"c_{seed_id}_{pol}_{ss}.log"; 
 subprocess.run(["./target/release/nvme-lite-oracle","run-one","--seed-file",seed_path,"--schedule-seed",str(ss),"--policy",pol,"--bound-k","inf","--fault-mode","NONE","--submit-window","inf","--out-log",str(rust_log),"--scheduler-version","v1.0"], check=True)
 subprocess.run(["./c_dut/nvme-lite-dut","run-one","--seed-file",seed_path,"--schedule-seed",str(ss),"--policy",pol,"--bound-k","inf","--fault-mode","NONE","--submit-window","inf","--out-log",str(c_log),"--scheduler-version","v1.0"], check=True)
 rust_short=out/"short"/f"rust_{seed_id}_{pol}_{ss}.short"; c_short=out/"short"/f"c_{seed_id}_{pol}_{ss}.short"; short(rust_log,rust_short); short(c_log,c_short)
 a=rust_short.read_text(encoding="utf-8",errors="replace").splitlines(True); b=c_short.read_text(encoding="utf-8",errors="replace").splitlines(True)
 ok=(a==b)
 if not ok:
  d="".join(difflib.unified_diff(a,b,fromfile=str(rust_short),tofile=str(c_short))); (out/"diff"/f"diff_{seed_id}_{pol}_{ss}.patch").write_text(d,encoding="utf-8")
 summary.append((i,seed_id,pol,ss,"MATCH" if ok else "DIFF"))
 (out/"summary.csv").write_text("i,seed_id,policy,schedule_seed,status\n" + "\n".join([f"{i},{sid},{pol},{ss},{st}" for (i,sid,pol,ss,st) in summary]) + "\n", encoding="utf-8")
print("[ok] wrote out/c_dut_val/summary.csv; diffs (if any) in out/c_dut_val/diff/")'

### 3) Quick check
cat out/c_dut_val/summary.csv

---

