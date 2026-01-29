

This directory contains artifacts for validating that selected
**poison schedules** identified by the Rust oracle are **not oracle-specific**,
but manifest identically in an independent C implementation (C-DUT).



## Goal

The purpose of this validation is to demonstrate that high p95-latency
"poison schedules" are:

- not artifacts of the Rust oracle
- not measurement errors
- but properties of the protocol and scheduling semantics



## Configuration (Fixed)

All runs use identical parameters across implementations:

- seed_file: `seed_001_long32.json`
- policy: `ADVERSARIAL`
- bound_k: `inf`
- fault_mode: `NONE`
- submit_window: `inf`
- scheduler_version: `v1.0`

Only the implementation differs (Rust vs C).



## Directory Structure
out/C_dut_val/
├── logs_rust/ # Full Rust oracle logs
├── logs_c/ # Full C-DUT logs
├── short_traces/ # Filtered semantic traces (SUBMIT/COMPLETE/FENCE/RESET)
└── meta/
├── poison_top_p95_swinf.csv
├── risk_cliff_swinf.csv
└── validated_runs.txt


## Validation Method

For each selected poison schedule:

1. Run Rust oracle (`nvme-lite-oracle run-one`)
2. Run C-DUT (`nvme-lite-dut run-one`)
3. Filter logs to protocol-relevant events:
   `SUBMIT`, `COMPLETE`, `FENCE`, `RESET`, `RUN_END`
4. Compare traces using `diff`

All compared traces are **bit-identical**.



## Result

All validated poison schedules exhibit identical execution traces
in both implementations.

This confirms that the observed tail-latency amplification is an
implementation-independent property of the protocol and schedule.

