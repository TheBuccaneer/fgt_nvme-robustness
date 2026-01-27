//! NVMe-lite Oracle: Schedule-Aware Differential Fuzzing Reference Implementation
//!
//! Usage:
//!   nvme-lite-oracle run-one --seed-file seeds/seed_001.json --schedule-seed 42 ...
//!   nvme-lite-oracle run-matrix --config configs/main.yaml --out-dir out/logs
//!
//! Assumptions (design decisions):
//! - run_id format: {seed_id}_{policy}_{bound_k}_{schedule_seed}_{fault_mode}
//! - Deterministic: same inputs always produce identical logs
//! - BATCHED policy uses batch_size=4, with FIFO within batch window
//! - Fault injection happens at step n_cmds/2
//! - RESET clears all pending; TIMEOUT affects one command

mod config;
mod logging;
mod model;
mod runner;
mod scheduler;
mod seed;

use anyhow::{Context, Result};
use clap::{Parser, Subcommand};
use std::path::PathBuf;

use config::ExperimentConfig;
use logging::{FaultMode, SubmitWindow};
use runner::{execute_run, RunConfig};
use scheduler::{BoundK, Policy};
use seed::Seed;

#[derive(Parser)]
#[command(name = "nvme-lite-oracle")]
#[command(about = "NVMe-lite Schedule-Aware Differential Fuzzing Oracle")]
#[command(version = "1.0.0")]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Run a single experiment
    RunOne {
        /// Path to seed file (JSON)
        #[arg(long)]
        seed_file: PathBuf,

        /// Schedule seed (RNG seed for scheduling decisions)
        #[arg(long)]
        schedule_seed: u64,

        /// Scheduling policy: FIFO, RANDOM, ADVERSARIAL, BATCHED
        #[arg(long)]
        policy: String,

        /// Reorder bound: 0, 1, 2, ... or "inf"
        #[arg(long)]
        bound_k: String,

        /// Fault mode: NONE, TIMEOUT, RESET
        #[arg(long, default_value = "NONE")]
        fault_mode: String,

        /// Submit window: max pending commands (number or "inf", default: inf)
        #[arg(long, default_value = "inf")]
        submit_window: String,

        /// Output log file path
        #[arg(long)]
        out_log: PathBuf,

        /// Scheduler version string
        #[arg(long, default_value = "v1.0")]
        scheduler_version: String,

        /// Git commit (optional)
        #[arg(long, default_value = "")]
        git_commit: String,

        /// Dump schedule to JSON file
        #[arg(long)]
        dump_schedule: Option<PathBuf>,
    },

    /// Run full experiment matrix from config
    RunMatrix {
        /// Path to config file (YAML)
        #[arg(long)]
        config: PathBuf,

        /// Output directory for logs
        #[arg(long)]
        out_dir: PathBuf,

        /// Override schedule seeds (e.g., "0-99")
        #[arg(long)]
        schedule_seeds: Option<String>,

        /// Submit window: max pending commands (number or "inf", default: inf)
        #[arg(long, default_value = "inf")]
        submit_window: String,

        /// Dump schedules for all runs
        #[arg(long)]
        dump_schedules: bool,
    },
}

fn main() -> Result<()> {
    let cli = Cli::parse();

    match cli.command {
        Commands::RunOne {
            seed_file,
            schedule_seed,
            policy,
            bound_k,
            fault_mode,
            submit_window,
            out_log,
            scheduler_version,
            git_commit,
            dump_schedule,
        } => run_one(
            &seed_file,
            schedule_seed,
            &policy,
            &bound_k,
            &fault_mode,
            &submit_window,
            &out_log,
            &scheduler_version,
            &git_commit,
            dump_schedule.as_deref(),
        ),

        Commands::RunMatrix {
            config,
            out_dir,
            schedule_seeds,
            submit_window,
            dump_schedules,
        } => run_matrix(
            &config,
            &out_dir,
            schedule_seeds.as_deref(),
            &submit_window,
            dump_schedules,
        ),
    }
}

fn run_one(
    seed_file: &PathBuf,
    schedule_seed: u64,
    policy: &str,
    bound_k: &str,
    fault_mode: &str,
    submit_window: &str,
    out_log: &PathBuf,
    scheduler_version: &str,
    git_commit: &str,
    dump_schedule: Option<&std::path::Path>,
) -> Result<()> {
    // Load seed
    let seed = Seed::load(seed_file)?;

    // Parse parameters
    let policy: Policy = policy.parse().map_err(|e| anyhow::anyhow!("{}", e))?;
    let bound_k = BoundK::parse(bound_k).map_err(|e| anyhow::anyhow!("{}", e))?;
    let fault_mode: FaultMode = fault_mode.parse().map_err(|e| anyhow::anyhow!("{}", e))?;
    let submit_window = SubmitWindow::parse(submit_window).map_err(|e| anyhow::anyhow!("{}", e))?;

    let config = RunConfig {
        seed_id: seed.seed_id.clone(),
        schedule_seed,
        policy,
        bound_k,
        fault_mode,
        submit_window,
        scheduler_version: scheduler_version.to_string(),
        git_commit: git_commit.to_string(),
        dump_schedule: dump_schedule.is_some(),
    };

    // Create output directory if needed
    if let Some(parent) = out_log.parent() {
        std::fs::create_dir_all(parent)?;
    }

    // Execute run
    let result = execute_run(&seed, &config, out_log, dump_schedule)?;

    println!("Run completed: {}", result.run_id);
    println!("  pending_left: {}", result.pending_left);
    println!("  pending_peak: {}", result.pending_peak);
    if result.had_reset {
        println!("  commands_lost: {}", result.commands_lost);
    }

    Ok(())
}

fn run_matrix(
    config_path: &PathBuf,
    out_dir: &PathBuf,
    schedule_seeds_override: Option<&str>,
    submit_window_str: &str,
    dump_schedules: bool,
) -> Result<()> {
    // Load config
    let mut config = ExperimentConfig::load(config_path)?;

    // Parse submit_window
    let submit_window =
        SubmitWindow::parse(submit_window_str).map_err(|e| anyhow::anyhow!("{}", e))?;

    // Override schedule seeds if provided
    if let Some(seeds_str) = schedule_seeds_override {
        let (start, end) = parse_range(seeds_str)?;
        config.schedule_seed_range = (start, end);
    }

    // Create output directories
    std::fs::create_dir_all(out_dir)?;
    if dump_schedules {
        std::fs::create_dir_all(out_dir.join("schedules"))?;
    }

    let total = config.total_runs();
    println!("Running {} experiments...", total);
    println!("  Seeds: {}", config.seeds.len());
    println!("  Policies: {:?}", config.policies);
    println!("  Bounds: {:?}", config.bounds);
    println!("  Faults: {:?}", config.faults);
    println!(
        "  Schedule seeds: {}-{}",
        config.schedule_seed_range.0, config.schedule_seed_range.1
    );
    println!("  Submit window: {}", submit_window);

    let mut completed = 0;
    let mut errors = 0;

    // Iterate through all combinations
    for seed_path in &config.seeds.clone() {
        let seed = match Seed::load(std::path::Path::new(seed_path)) {
            Ok(s) => s,
            Err(e) => {
                eprintln!("Error loading seed {}: {}", seed_path, e);
                errors += 1;
                continue;
            }
        };

        for &policy in &config.policies {
            for &bound_k in &config.bounds {
                for &fault_mode in &config.faults {
                    for schedule_seed in config.schedule_seeds() {
                        let run_config = RunConfig {
                            seed_id: seed.seed_id.clone(),
                            schedule_seed,
                            policy,
                            bound_k,
                            fault_mode,
                            submit_window,
                            scheduler_version: config.scheduler_version.clone(),
                            git_commit: config.git_commit.clone(),
                            dump_schedule: dump_schedules,
                        };

                        let run_id = run_config.run_id();
                        let log_path = out_dir.join(format!("{}.log", run_id));
                        let schedule_path = if dump_schedules {
                            Some(out_dir.join("schedules").join(format!("{}.json", run_id)))
                        } else {
                            None
                        };

                        match execute_run(&seed, &run_config, &log_path, schedule_path.as_deref()) {
                            Ok(_) => {
                                completed += 1;
                                if completed % 100 == 0 {
                                    println!("Progress: {}/{}", completed, total);
                                }
                            }
                            Err(e) => {
                                eprintln!("Error in run {}: {}", run_id, e);
                                errors += 1;
                            }
                        }
                    }
                }
            }
        }
    }

    println!("\nCompleted: {}/{}", completed, total);
    if errors > 0 {
        println!("Errors: {}", errors);
    }

    Ok(())
}

/// Parse a range string like "0-99" or "42"
fn parse_range(s: &str) -> Result<(u64, u64)> {
    if let Some((start, end)) = s.split_once('-') {
        let start: u64 = start
            .parse()
            .with_context(|| format!("Invalid range start: {}", start))?;
        let end: u64 = end
            .parse()
            .with_context(|| format!("Invalid range end: {}", end))?;
        Ok((start, end))
    } else {
        let val: u64 = s
            .parse()
            .with_context(|| format!("Invalid single value: {}", s))?;
        Ok((val, val))
    }
}
