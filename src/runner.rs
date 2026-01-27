//! Runner: Executes a single run with given parameters
//!
//! Coordinates model, scheduler, and logger to produce deterministic runs.

use crate::logging::{FaultMode, Logger, SerializedSchedule, SubmitWindow};
use crate::model::{NvmeLiteModel, Status};
use crate::scheduler::{BoundK, Policy, Scheduler};
use crate::seed::Seed;
use anyhow::Result;
use std::path::Path;

/// Configuration for a single run
#[derive(Debug, Clone)]
pub struct RunConfig {
    pub seed_id: String,
    pub schedule_seed: u64,
    pub policy: Policy,
    pub bound_k: BoundK,
    pub fault_mode: FaultMode,
    pub submit_window: SubmitWindow,
    pub scheduler_version: String,
    pub git_commit: String,
    pub dump_schedule: bool,
}

impl RunConfig {
    /// Generate deterministic run_id from parameters
    pub fn run_id(&self) -> String {
        format!(
            "{}_{}_{}_{}_{}",
            self.seed_id, self.policy, self.bound_k, self.schedule_seed, self.fault_mode
        )
    }
}

/// Result of a run (for quick access without parsing log)
#[derive(Debug)]
pub struct RunResult {
    pub run_id: String,
    pub pending_left: u32,
    pub pending_peak: u32,
    pub had_reset: bool,
    pub commands_lost: u32,
}

/// Execute a single run
pub fn execute_run(
    seed: &Seed,
    config: &RunConfig,
    out_log: &Path,
    out_schedule: Option<&Path>,
) -> Result<RunResult> {
    let mut model = NvmeLiteModel::new();
    let mut scheduler = Scheduler::new(config.policy, config.bound_k, config.schedule_seed);
    let mut logger = Logger::new();
    let mut schedule = SerializedSchedule::new(
        &seed.seed_id,
        config.schedule_seed,
        config.policy,
        config.bound_k,
        config.fault_mode,
    );

    let n_cmds = seed.commands.len();
    let run_id = config.run_id();
    let submit_window = config.submit_window.value();

    // Write header (with submit_window)
    logger.write_header_with_window(
        &run_id,
        &seed.seed_id,
        config.schedule_seed,
        config.policy,
        config.bound_k,
        config.fault_mode,
        n_cmds,
        &config.scheduler_version,
        &config.git_commit,
        config.submit_window,
    );

    // Tracking
    let mut next_cmd: usize = 0;
    let mut pending_peak: u32 = 0;

    // Helper to submit one command
    let do_submit = |model: &mut NvmeLiteModel, logger: &mut Logger, idx: usize| {
        let command = &seed.commands[idx];
        let (cmd_id, is_fence, fence_id) = model.submit(command.clone());
        logger.log_submit(cmd_id, command.type_name());
        if is_fence {
            if let Some(fid) = fence_id {
                logger.log_fence(fid);
            }
        }
    };

    // No initial fill - start with interleaved loop immediately
    // This allows RNG to affect pending_peak from the start

    // Interleaved submit/complete loop
    let mut step_count = 0;
    let fault_step = if config.fault_mode != FaultMode::NONE {
        Some(n_cmds / 2)
    } else {
        None
    };
    #[allow(unused_assignments)]
    let mut _fault_injected = false;
    let mut stop_submits = false; // Set to true after TIMEOUT injection

    // BATCHED policy tracking
    let mut batch_remaining: usize = 0;
    const BATCH_SIZE: usize = 4;

    loop {
        let pending_count = model.pending_count();
        let submit_ok = pending_count < submit_window && next_cmd < n_cmds && !stop_submits;
        let complete_ok = pending_count > 0;

        if !submit_ok && !complete_ok {
            break;
        }

        // Decide: submit or complete?
        let do_complete = if config.policy == Policy::BATCHED && batch_remaining > 0 {
            // During a burst, force COMPLETE
            true
        } else if submit_ok && complete_ok {
            // Use RNG bit to decide
            let bit = scheduler.next_bit();
            bit == 1
        } else if complete_ok {
            true
        } else {
            false
        };

        if do_complete {
            // Check fault injection first
            if let Some(fs) = fault_step {
                if step_count >= fs && !_fault_injected {
                    match config.fault_mode {
                        FaultMode::TIMEOUT => {
                            let pending = model.get_pending_canonical();
                            if let Some(&cmd_id) = pending.first() {
                                if let Some(result) = model.complete(cmd_id, Some(Status::TIMEOUT))
                                {
                                    logger.log_complete(
                                        result.cmd_id,
                                        result.status,
                                        result.output,
                                    );
                                    schedule.add_fault("TIMEOUT", step_count);
                                }
                            }
                            _fault_injected = true;
                            stop_submits = true; // No more SUBMITs after TIMEOUT
                            step_count += 1;
                            continue;
                        }
                        FaultMode::RESET => {
                            let pending_before = model.reset();
                            logger.log_reset("INJECTED", pending_before);
                            schedule.add_fault("RESET", step_count);
                            _fault_injected = true;
                            break;
                        }
                        FaultMode::NONE => {}
                    }
                }
            }

            // Normal completion
            let pending = model.get_pending_canonical();

            // BATCHED: start new burst if not in one
            if config.policy == Policy::BATCHED && batch_remaining == 0 && !pending.is_empty() {
                batch_remaining = std::cmp::min(BATCH_SIZE, pending.len());
            }

            if let Some(decision) = scheduler.pick_next(&pending) {
                if let Some(result) = model.complete(decision.cmd_id, None) {
                    logger.log_complete(result.cmd_id, result.status, result.output);
                    schedule.add_complete(decision.pick_index);

                    // Decrement batch counter for BATCHED policy
                    if config.policy == Policy::BATCHED && batch_remaining > 0 {
                        batch_remaining -= 1;
                    }
                }
            }
            step_count += 1;
        } else {
            // Submit next command
            do_submit(&mut model, &mut logger, next_cmd);
            next_cmd += 1;
            let current = model.pending_count() as u32;
            if current > pending_peak {
                pending_peak = current;
            }
        }
    }

    // Write run end - use our tracked peak (more accurate with interleaving)
    let pending_left = model.pending_count() as u32;
    // Take max of model's peak and our tracked peak
    let final_peak = std::cmp::max(pending_peak, model.pending_peak());
    logger.log_run_end(pending_left, final_peak);

    // Write outputs
    logger.write_to_file(out_log)?;

    if config.dump_schedule {
        if let Some(schedule_path) = out_schedule {
            schedule.write_to_file(schedule_path)?;
        }
    }

    Ok(RunResult {
        run_id,
        pending_left,
        pending_peak: final_peak,
        had_reset: model.had_reset(),
        commands_lost: model.commands_lost(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::seed::Command;

    fn test_seed() -> Seed {
        Seed {
            seed_id: "test".to_string(),
            commands: vec![
                Command::WRITE {
                    lba: 0,
                    len: 4,
                    pattern: 42,
                },
                Command::READ { lba: 0, len: 4 },
                Command::FENCE,
                Command::WRITE {
                    lba: 8,
                    len: 2,
                    pattern: 7,
                },
            ],
        }
    }

    #[test]
    fn test_fifo_run() {
        let seed = test_seed();
        let config = RunConfig {
            seed_id: "test".to_string(),
            schedule_seed: 0,
            policy: Policy::FIFO,
            bound_k: BoundK::Infinite,
            fault_mode: FaultMode::NONE,
            submit_window: SubmitWindow::Infinite,
            scheduler_version: "test".to_string(),
            git_commit: "none".to_string(),
            dump_schedule: false,
        };

        let tmp = std::env::temp_dir().join("test_fifo.log");
        let result = execute_run(&seed, &config, &tmp, None).unwrap();

        assert_eq!(result.pending_left, 0);
        assert!(result.pending_peak >= 1);

        let log = std::fs::read_to_string(&tmp).unwrap();
        assert!(log.contains("RUN_HEADER("));
        assert!(log.contains("RUN_END(pending_left=0"));
    }

    #[test]
    fn test_reset_run() {
        let seed = test_seed();
        let config = RunConfig {
            seed_id: "test".to_string(),
            schedule_seed: 0,
            policy: Policy::FIFO,
            bound_k: BoundK::Infinite,
            fault_mode: FaultMode::RESET,
            submit_window: SubmitWindow::Infinite,
            scheduler_version: "test".to_string(),
            git_commit: "none".to_string(),
            dump_schedule: false,
        };

        let tmp = std::env::temp_dir().join("test_reset.log");
        let result = execute_run(&seed, &config, &tmp, None).unwrap();

        assert!(result.had_reset);

        let log = std::fs::read_to_string(&tmp).unwrap();
        assert!(log.contains("RESET(reason=INJECTED"));
    }

    #[test]
    fn test_submit_window() {
        let seed = test_seed();
        let config = RunConfig {
            seed_id: "test".to_string(),
            schedule_seed: 42,
            policy: Policy::RANDOM,
            bound_k: BoundK::Infinite,
            fault_mode: FaultMode::NONE,
            submit_window: SubmitWindow::Finite(2),
            scheduler_version: "test".to_string(),
            git_commit: "none".to_string(),
            dump_schedule: false,
        };

        let tmp = std::env::temp_dir().join("test_window.log");
        let result = execute_run(&seed, &config, &tmp, None).unwrap();

        assert_eq!(result.pending_left, 0);
        // With window=2, peak should be <= 2
        assert!(result.pending_peak <= 2);
    }
}
