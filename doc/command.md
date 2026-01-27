# Python environment (venv)

## 1) Create venv
python3 -m venv .venv

## 2) Activate venv
source .venv/bin/activate

## 3) Upgrade pip
python -m pip install -U pip

## 4) Install dependencies (minimum for parsing; add matplotlib only if plotting)
python -m pip install matplotlib

## 5) Parse logs → CSV (01_parse_check)

python3 scripts/01_parse_check.py --logs <LOG_DIR> --out <CSV_OUT>

### Example (SW2)
python3 scripts/01_parse_check.py --logs out/logs_sw2 --out out/csv/results_sw2.csv

## 6) Aggregate run-level CSV → summary.csv (02_aggregate)

python3 scripts/02_aggregate.py --in <RESULTS_CSV> --out <SUMMARY_CSV>

### Options
- `--in`  Input run-level CSV (typically `out/csv/results_*.csv`)
- `--out` Output aggregated CSV (`summary.csv`), grouped by `(seed_id, policy, bound_k, fault_mode)`

### Example (SW2)
python3 scripts/02_aggregate.py --in out/csv/results_sw2.csv --out out/csv/summary_sw2.csv


## 7) Plot robustness curve RD vs bound_k (03_robustness.py)

python3 scripts/03_plot_robustness.py --in <SUMMARY_CSV> --out <PLOT_OUT>

### Options
- `--in`  Input `summary.csv`
- `--out` Output plot file (`.pdf`)
- The script filters `fault_mode=NONE` and plots `RD_mean` over `bound_k`, one line per policy

### Example (SW2)
python3 scripts/03_plot_robustness.py --in out/csv/summary_sw2.csv --out out/fig/plot1_rd_vs_bound.pdf

## 8) Plot 2 – Latency by policy (04_plot_latency_fixedv2)

python3 scripts/04_plot_latency_fixedv2.py --in <RESULTS_CSV> --out <PLOT_OUT> [--metric <METRIC>] [--bound-k <K>] [--fault-mode <MODE>]

### Options
- `--in` Input run-level CSV (e.g., `out/csv/results_sw4.csv`)
- `--out` Output PDF/PNG path
- `--metric` Which latency metric to plot (default: `p95_latency_step`)
  - choices: `mean_latency_step`, `p95_latency_step`, `max_latency_step`, `mean_latency_disp`, `p95_latency_disp`, `max_latency_disp`
- `--bound-k` Filter by `bound_k` (default: `inf`) 
- `--fault-mode` Filter by `fault_mode` (default: `NONE`) 

### Example (SW4, p95, k=inf, fault=NONE)
python3 scripts/04_plot_latency_fixedv2.py --in out/csv/results_sw4.csv --out out/fig/plot2_latency_sw4_p95_k=inf_fault=NONE.pdf --metric p95_latency_step --bound-k inf --fault-mode NONE


## Risk-Cliff (SW∞) — aggregate + paper table

### 9) Aggregate run-level results → risk-cliff CSV
python3 scripts/05_risk_cliff_aggregate.py

#### Output
- out/tab/risk_cliff_swinf.csv

### 10) Convert risk-cliff CSV → Markdown table (paper-ready)
python3 scripts/06_risk_cliff_table.py

#### Output
- out/tab/risk_cliff_swinf.md

#### Table cell format
Each cell is: `p95_latency_step / RD / exceed_rate`

## Poison schedules (SW∞) — Top-K + condensed traces

### 1) Export Top-K runs by tail latency (p95)
python3 scripts/07_poison_top_p95.py

#### Output
- out/poison/poison_top_p95_swinf.csv

### 2) Export condensed traces for Top-N poison runs
python3 scripts/08_poison_export_traces.py

#### Output
- out/poison/traces/*.log

## RDSS (Cross-Entropy) and Uniform baseline (SW4)

### RDSS run (targeted sampling)
python3 scripts/rdss_ce.py --config configs/main.yaml --mode rust --out out/rdss_sw4 --submit-window 4 --pool 0-99 --rounds 3 --batch 30 --rho 0.10 --explore 0.20 --seed 0

#### Outputs
- out/rdss_sw4/rdss_status.csv
- out/rdss_sw4/top_poison.csv
- out/rdss_sw4/elite_seeds.txt

### Uniform baseline (same budget, purely uniform exploration)
python3 scripts/rdss_ce.py --config configs/main.yaml --mode rust --out out/uniform_sw4 --submit-window 4 --pool 0-99 --rounds 1 --batch 90 --rho 0.10 --explore 1.0 --seed 0

#### Outputs
- out/uniform_sw4/rdss_status.csv
- out/uniform_sw4/top_poison.csv
- out/uniform_sw4/elite_seeds.txt

### RDSS vs Uniform summary (maxima)
python3 -c 'import csv; 
def read_best(p):
 r=list(csv.DictReader(open(p,newline="",encoding="utf-8"))); 
 last=r[-1]; 
 return int(last["seen"]), int(last["elite_n"]), float(last["best_slack"]), int(last["best_seed"])
for name, path in [("RDSS","out/rdss_sw4/rdss_status.csv"), ("UNIFORM","out/uniform_sw4/rdss_status.csv")]:
 seen, elite_n, best_slack, best_seed = read_best(path)
 print(f"[{name}] seen={seen} elite_n={elite_n} best_slack={best_slack:.1f} best_seed={best_seed})''

### Rare-event view (threshold hit counts)
python3 -c 'import csv; 
def slacks(path):
 rows=list(csv.DictReader(open(path,newline="",encoding="utf-8")))
 out=[]
 for r in rows:
  try: out.append(float(r["tail_slack_step"]))
  except: pass
 return out
def summarize(name, path):
 s=slacks(path)
 thr=[25,30,31]
 hits={t:sum(1 for x in s if x>=t) for t in thr}
 best=max(s) if s else float("nan")
 print(f"[{name}] N={len(s)} best_slack={best:.1f} hits>=25:{hits[25]} hits>=30:{hits[30]} hits>=31:{hits[31]}")
summarize("RDSS", "out/rdss_sw4/top_poison.csv")
summarize("UNIFORM", "out/uniform_sw4/top_poison.csv")'














# nvme-lite robustness – minimal commands

## 1) Build release binary
cargo build --release

## 2) Run experiments (generic template)
./target/release/nvme-lite-oracle run-matrix \
  --config configs/main.yaml \
  --out-dir out/logs \
  --submit-window 4

### Options
--config
Needed

--out-dir
Directory where one `.log` file per run is written.

--submit-window
Maximum number of in-flight submissions.
Typical values:
- 2   (low concurrency)
- 4   (medium concurrency)
- inf (unbounded / stress case)

### Generic template
cargo build --release
./target/release/nvme-lite-oracle run-matrix --config <CONFIG> --out-dir <OUT_DIR> --submit-window <2|4|inf>


## Option B) Second-seed generality check (SW∞)

### Goal
Repeat the SW∞ experiment with a second workload seed (seed_002) while keeping policies, bounds, faults, and schedule-seed pool unchanged. Then compare the qualitative p95-vs-k structure (fault_mode=NONE).

### 1) Create a second config with `seed_002.json`
python3 -c 'import yaml; p="configs/main.yaml"; o="configs/main_seed2.yaml"; cfg=yaml.safe_load(open(p,"r",encoding="utf-8")); cfg["seeds"]=["seeds/seed_002.json"]; yaml.safe_dump(cfg, open(o,"w",encoding="utf-8"), sort_keys=False); print("[ok] wrote", o)'

### 2) Run SW∞ matrix (seed_002) → logs
rm -rf out/logs_swinf_seed2; mkdir -p out/logs_swinf_seed2; ./target/release/nvme-lite-oracle run-matrix --config configs/main_seed2.yaml --out-dir out/logs_swinf_seed2 --submit-window inf

### 3) Parse logs → run-level CSV
mkdir -p out/csv; python3 scripts/01_parse_check.py --logs out/logs_swinf_seed2 --out out/csv/results_swinf_seed2.csv

### 4) Compare Seed1 vs Seed2 (fault=NONE): mean p95 latency by policy × bound_k
python3 -c 'import csv; bounds=["0","1","2","3","5","10","inf"]; policies=["FIFO","RANDOM","BATCHED","ADVERSARIAL"]; 
def mean_p95(path):
 d={}; n={}
 for r in csv.DictReader(open(path,newline="",encoding="utf-8")):
  if r.get("fault_mode")!="NONE": 
   continue
  pol=r.get("policy"); bk=r.get("bound_k"); x=r.get("p95_latency_step","")
  if pol is None or bk is None or x=="": 
   continue
  try: v=float(x)
  except: 
   continue
  k=(pol,bk); d[k]=d.get(k,0.0)+v; n[k]=n.get(k,0)+1
 out={}
 for pol in policies:
  out[pol]={}
  for bk in bounds:
   k=(pol,bk)
   out[pol][bk]=(d[k]/n[k]) if k in n and n[k]>0 else float("nan")
 return out
a=mean_p95("out/csv/results_swinf.csv")
b=mean_p95("out/csv/results_swinf_seed2.csv")
print("[seed1] mean p95_latency_step, fault=NONE")
for pol in policies:
 print(pol, " ".join(f"{a[pol][bk]:.2f}" if a[pol][bk]==a[pol][bk] else "nan" for bk in bounds))
print("[seed2] mean p95_latency_step, fault=NONE")
for pol in policies:
 print(pol, " ".join(f"{b[pol][bk]:.2f}" if b[pol][bk]==b[pol][bk] else "nan" for bk in bounds))'

### 5) Save the compare output to a file (for paper notes)
mkdir -p out/tab; python3 -c 'import csv; bounds=["0","1","2","3","5","10","inf"]; policies=["FIFO","RANDOM","BATCHED","ADVERSARIAL"]; 
def mean_p95(path):
 d={}; n={}
 for r in csv.DictReader(open(path,newline="",encoding="utf-8")):
  if r.get("fault_mode")!="NONE": 
   continue
  pol=r.get("policy"); bk=r.get("bound_k"); x=r.get("p95_latency_step","")
  if pol is None or bk is None or x=="": 
   continue
  try: v=float(x)
  except: 
   continue
  k=(pol,bk); d[k]=d.get(k,0.0)+v; n[k]=n.get(k,0)+1
 out={}
 for pol in policies:
  out[pol]={}
  for bk in bounds:
   k=(pol,bk)
   out[pol][bk]=(d[k]/n[k]) if k in n and n[k]>0 else float("nan")
 return out
a=mean_p95("out/csv/results_swinf.csv")
b=mean_p95("out/csv/results_swinf_seed2.csv")
lines=[]
lines.append("[seed1] mean p95_latency_step, fault=NONE")
for pol in policies:
 lines.append(pol+" "+" ".join(f"{a[pol][bk]:.2f}" if a[pol][bk]==a[pol][bk] else "nan" for bk in bounds))
lines.append("[seed2] mean p95_latency_step, fault=NONE")
for pol in policies:
 lines.append(pol+" "+" ".join(f"{b[pol][bk]:.2f}" if b[pol][bk]==b[pol][bk] else "nan" for bk in bounds))
open("out/tab/seed_compare_p95_none.txt","w",encoding="utf-8").write("\\n".join(lines)+"\\n"); print("[ok] wrote out/tab/seed_compare_p95_none.txt")'


