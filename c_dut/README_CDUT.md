# NVMe-lite DUT (C Implementation)

A C implementation of the NVMe-lite model for **differential testing** against the Rust Oracle.

## Quick Start

```bash
# Build
make

# Run single experiment
./nvme-lite-dut run-one \
  --seed-file seeds/seed_001.json \
  --schedule-seed 42 \
  --policy FIFO \
  --bound-k 0 \
  --fault-mode NONE \
  --out-log out/test.log \
  --scheduler-version v1.0

# Run full matrix
./nvme-lite-dut run-matrix \
  --config configs/main.yaml \
  --out-dir out/logs \
  --schedule-seeds 0-99
```

## Project Structure

```
c_dut/
├── Makefile
├── README.md
├── src/
│   ├── main.c          # CLI entry point
│   ├── config.c/h      # YAML config loading
│   ├── seed.c/h        # JSON seed parsing
│   ├── model.c/h       # NVMe-lite device state
│   ├── scheduler.c/h   # Scheduling policies + bound_k
│   ├── logging.c/h     # Log output
│   ├── runner.c/h      # Run execution loop
│   └── rng.c/h         # Deterministic PRNG (splitmix64)
├── vendor/
│   └── mini_json.c/h   # Minimal JSON parser
├── seeds/              # Seed JSON files
├── configs/            # YAML config files
└── out/                # Output logs
```

## CLI Commands

### `run-one`

Execute a single run with explicit parameters.

```bash
./nvme-lite-dut run-one \
  --seed-file <path>        # JSON seed file
  --schedule-seed <N>       # RNG seed for scheduling
  --policy <POLICY>         # FIFO | RANDOM | ADVERSARIAL | BATCHED
  --bound-k <K>             # 0, 1, 2, ... or "inf"
  --fault-mode <MODE>       # NONE | TIMEOUT | RESET (default: NONE)
  --submit-window <N|inf>   # Max pending commands (default: inf)
  --out-log <path>          # Output log file
  --scheduler-version <V>   # Version string (default: v1.0)
  --git-commit <hash>       # Git commit (default: empty)
```

### `run-matrix`

Execute an experiment matrix from YAML config.

```bash
./nvme-lite-dut run-matrix \
  --config <path>           # YAML config file
  --out-dir <path>          # Output directory for logs
  --schedule-seeds <range>  # Override: "0-99" or "42"
  --submit-window <N|inf>   # Max pending commands (default: inf)
```

## Log Format

Identical to the Rust Oracle:

```
RUN_HEADER(run_id=..., seed_id=..., schedule_seed=..., policy=..., bound_k=..., fault_mode=..., n_cmds=..., submit_window=..., scheduler_version=..., git_commit=...)
SUBMIT(cmd_id=0, cmd_type=WRITE)
SUBMIT(cmd_id=1, cmd_type=READ)
FENCE(fence_id=0)
COMPLETE(cmd_id=0, status=OK, out=0)
COMPLETE(cmd_id=1, status=OK, out=12345)
RUN_END(pending_left=0, pending_peak=2)
```

## Seed Format (JSON)

```json
{
  "seed_id": "seed_001",
  "commands": [
    {"type": "WRITE", "lba": 0, "len": 4, "pattern": 123},
    {"type": "READ", "lba": 0, "len": 4},
    {"type": "FENCE"},
    {"type": "WRITE", "lba": 8, "len": 2, "pattern": 7}
  ]
}
```

## Config Format (YAML)

```yaml
seeds:
  - "seeds/seed_001.json"
policies:
  - FIFO
  - RANDOM
bounds:
  - "0"
  - "inf"
faults:
  - NONE
schedule_seeds: "0-99"
scheduler_version: "v1.0"
git_commit: "auto"
```

## Tests

```bash
make test
```

This runs:
1. **Determinism test**: Same inputs → identical logs
2. **bound_k=0 test**: Forces FIFO completion order
3. **fault_mode=NONE test**: pending_left must be 0

## Implementation Notes

### PRNG
Uses splitmix64 for deterministic random number generation. This is different from Rust's ChaCha8, but both are deterministic with the same seed.

### Differences from Rust
- The C implementation uses a different PRNG (splitmix64 vs ChaCha8)
- Logs will NOT be byte-identical to Rust logs
- Semantics ARE identical (same state transitions, same rules)
- Differential testing compares semantic equivalence, not byte equality

### Semantics Implemented
- **Policies**: FIFO, RANDOM, ADVERSARIAL, BATCHED (batch_size=4)
- **bound_k**: Candidate set limitation (0, 1, 2, ..., inf)
- **Fault modes**: NONE, TIMEOUT, RESET
- **submit_window**: Interleaved submit/complete with RNG-controlled decisions
- **Hash for READ**: `hash = hash * 31 + value` (wrapping)

## Dependencies

- C11 compiler (gcc, clang)
- Standard POSIX libraries
- No external dependencies (JSON/YAML parsers included)

## License

Same as the Rust Oracle.
