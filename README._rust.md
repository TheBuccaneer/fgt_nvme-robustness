# NVMe-lite Oracle (Reference Model)

A small **NVMe-lite** host <-> device queue model written in Rust.
It is intended as a **deterministic reference/oracle** for schedule-aware experiments (logs -> offline metrics).

- Focus: **correctness + reproducibility**, not performance.
- Output: **one parseable log per run** (see “Log Format” below).
- Offline: logs are later turned into `results.csv`, `summary.csv`, plots and a table (outside this crate).


 

## Quick Start

```bash
# Build
cargo build --release

# Run single experiment (writes one log)
cargo run --release -- run-one \
  --seed-file seeds/seed_001.json \
  --schedule-seed 42 \
  --policy FIFO \
  --bound-k 0 \
  --fault-mode NONE \
  --out-log out/test.log \
  --scheduler-version v1.0

# Run full experiment matrix from YAML config
cargo run --release -- run-matrix \
  --config configs/main.yaml \
  --out-dir out/logs \
  --schedule-seeds 0-99
```



## Project Structure

```
nvme-lite-oracle/
├── src/
│   ├── main.rs        # CLI (clap)
│   ├── model.rs       # NVMe-lite state machine (Oracle)
│   ├── scheduler.rs   # Policies + bound_k logic
│   ├── logging.rs     # Exact log format
│   ├── runner.rs      # Run execution
│   ├── config.rs      # YAML config loading
│   └── seed.rs        # Command seed parsing
├── seeds/             # JSON command seeds
├── configs/           # YAML experiment configs
└── out/               # Generated logs (and later CSV/fig/tab from offline tools)
```



## CLI Commands

### `run-one`
Execute a single run with explicit parameters.

```bash
cargo run -- run-one \
  --seed-file <PATH>        # JSON seed file
  --schedule-seed <N>       # RNG seed for scheduling
  --policy <POLICY>         # FIFO | RANDOM | ADVERSARIAL | BATCHED
  --bound-k <K>             # 0, 1, 2, ... or "inf"
  --fault-mode <MODE>       # NONE | TIMEOUT | RESET (default: NONE)
  --out-log <PATH>          # Output log path
  --scheduler-version <V>   # Version string (default: v1.0)
  --dump-schedule <PATH>    # Optional: dump schedule.json for this run
```

### `run-matrix`
Execute an experiment matrix from YAML config.

```bash
cargo run -- run-matrix \
  --config <PATH>           # YAML config
  --out-dir <PATH>          # Output directory for logs
  --schedule-seeds <RANGE>  # e.g. "0-99" or "42"
  --dump-schedules          # Optional: dump schedule.json per run
```



## Scheduling Policies

| Policy | Description |
|-----|----|
| FIFO | Complete the oldest pending command first (baseline, RD≈0). |
| RANDOM | Random selection among allowed candidates (seeded by `schedule_seed`). |
| ADVERSARIAL | Picks the worst-case candidate (largest `cmd_id` among allowed). |
| BATCHED | Completes in batches of 4. **each pick still respects `bound_k`**. |

 

## `bound_k` (Reorder Bound)

`bound_k` restricts which pending commands may be completed next.

At each scheduler step:
1) Build the **canonical pending list**: pending commands sorted by `cmd_id` ascending:
   `P = [p0, p1, ..., pm-1]`
2) Allowed candidates are:
   - `bound_k=0`: only `p0`
   - `bound_k=k`: `p0..p_min(k, m-1)`
   - `bound_k=inf`: all pending

**Important:** `bound_k` is **not a policy**.
The policy chooses *within* the candidate set defined by `bound_k`.

 

## Semantics & Invariants

### Fault Injection (TIMEOUT / RESET)

`fault_mode` controls whether the run injects a fault into the completion trace.

**TIMEOUT semantics**
- When `fault_mode=TIMEOUT`, at a fixed scheduler step (e.g., `step = floor(n_cmds/2)`),
  one or more pending commands receive an explicit terminal event:
  `COMPLETE(cmd_id=..., status=TIMEOUT, out=...)`.
- TIMEOUT is **not** a silent hang. There is no “pending without terminal event” TIMEOUT semantics.
- After TIMEOUT injection, **no new SUBMITs are generated** for the remainder of the run
  (this is crucial for unambiguous recovery interpretation).

**RESET semantics**
- When `fault_mode=RESET`, at a fixed scheduler step, the oracle writes:
  `RESET(reason=..., pending_before=N)`.
- After RESET, the oracle clears/aborts pending state for the rest of the run.
- After RESET, **no new SUBMITs are generated**.
- Expected behavior: `pending_left = 0` at RUN_END. If `pending_left > 0` (and no further SUBMITs),
  this indicates incomplete recovery.

### Policy vs. `bound_k` (Orthogonal Dimensions)

- **Policy** (FIFO, RANDOM, ADVERSARIAL, BATCHED) determines *how* to pick from allowed candidates.
- **bound_k** determines *which* candidates are allowed (first k+1 pending in canonical order).
- Example: `RANDOM + bound_k=3` means “pick randomly from the first 4 pending commands”.

 

## Schedule Dump (`schedule.json`)

When `--dump-schedule` (or `--dump-schedules`) is enabled, the runner writes a JSON decision trace.

**Canonical pending list:** at each scheduler step, the pending set is ordered canonically
(sorted by `cmd_id` ascending). Decisions refer to this list.

**Meaning of `pick_index`:**
`pick_index` is the index into the canonical pending list at the time of the decision.
This keeps the trace valid even if fault injection changes the pending set later.

Example:

```json
{
  "schema": 1,
  "seed_id": "seed_001",
  "schedule_seed": 42,
  "policy": "RANDOM",
  "bound_k": 3,
  "fault_mode": "NONE",
  "steps": [
    {"type": "COMPLETE_PICK", "pick_index": 0},
    {"type": "COMPLETE_PICK", "pick_index": 2},
    {"type": "COMPLETE_PICK", "pick_index": 1}
  ]
}
```

 

## Log Format

Each run produces a plain-text log.

```
RUN_HEADER(run_id=..., seed_id=..., schedule_seed=..., policy=..., bound_k=..., fault_mode=..., n_cmds=..., scheduler_version=..., git_commit=...)
SUBMIT(cmd_id=0, cmd_type=WRITE)
SUBMIT(cmd_id=1, cmd_type=READ)
FENCE(fence_id=0)
COMPLETE(cmd_id=0, status=OK, out=0)
COMPLETE(cmd_id=1, status=OK, out=12345)
RUN_END(pending_left=0, pending_peak=2)
```

### Log Invariants (required for offline parsing)

1) **One SUBMIT per cmd_id** (in issue order).
2) **One terminal COMPLETE per cmd_id**:
   `COMPLETE(cmd_id, status=OK|ERR|TIMEOUT, out=...)`.
3) `COMPLETE` must only reference previously submitted `cmd_id`s.
4) Exactly one `RUN_END` per run.
5) If `fault_mode=NONE`, then `pending_left` must be `0`.
   If `fault_mode!=NONE`, `pending_left>0` is allowed (recovery is evaluated by metrics).
6) If the process crashes before `RUN_END`, offline tooling records `crash=1` and `mismatch=1`.

### `out=` normalization

`out` must be comparable across implementations (Oracle vs DUT).
- WRITE: `out=0`
- FENCE: `out=0`
- READ: `out=<deterministic value>` (e.g., u64 hash/CRC/value derived from read data)

 

## Seed File Format (JSON)

```json
{
  "seed_id": "seed_001",
  "commands": [
    {"type": "WRITE", "lba": 0, "len": 4, "pattern": 123},
    {"type": "READ",  "lba": 0, "len": 4},
    {"type": "FENCE"},
    {"type": "WRITE", "lba": 8, "len": 2, "pattern": 7}
  ]
}
```

 

## Config File Format (YAML)

```yaml
seeds:
  - "seeds/seed_001.json"
  - "seeds/seed_002.json"
policies:
  - FIFO
  - RANDOM
  - ADVERSARIAL
  - BATCHED
bounds:
  - "0"
  - "3"
  - "inf"
faults:
  - NONE
schedule_seeds: "0-99"
scheduler_version: "v1.0"
git_commit: "auto"
```

Notes:
- `bounds` are strings to support `"inf"` in YAML cleanly.
- `git_commit: auto` is recorded for traceability (it should not affect behavior).

 

## Determinism Guarantee

A run is deterministic (same log output) given the same:
`(seed_id, schedule_seed, policy, bound_k, fault_mode, scheduler_version)`
**and the same program version**.

`git_commit` is recorded to document the exact code revision. it is not intended to change behavior by itself.

 

## Metrics (computed offline from logs)

- **RD (Reordering Degree)**: normalized inversion count between SUBMIT order and COMPLETE order
- **FE (Fence Effectiveness)**: fraction of satisfied before/after-fence ordering constraints
- **SSI (Schedule Sensitivity Index)**: coefficient of variation of `pending_peak` over schedules (fixed seed_id)
- **RCS (Recovery Completeness Score)**: how completely pending work is cleaned up after fault injection

 

## Tests

```bash
cargo test
```

 

## Dependencies

- clap (CLI)
- serde, serde_json, serde_yaml (serialization)
- rand (deterministic RNG)
- anyhow (error handling)
