//! Logging: Exact log format for differential testing and metric computation
//!
//! Log format is designed for:
//! - RD: Submit and complete order reconstruction
//! - FE: Fence position tracking
//! - SSI: pending_peak per run
//! - RCS: Reset state tracking

use crate::model::Status;
use crate::scheduler::{BoundK, Policy};
use anyhow::Result;
use std::io::Write;

/// Submit window - controls how many commands can be pending simultaneously
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SubmitWindow {
    Finite(usize),
    Infinite,
}

impl SubmitWindow {
    pub fn parse(s: &str) -> std::result::Result<Self, String> {
        if s.to_lowercase() == "inf" {
            Ok(SubmitWindow::Infinite)
        } else {
            s.parse::<usize>()
                .map(SubmitWindow::Finite)
                .map_err(|_| format!("Invalid submit_window: {}", s))
        }
    }

    pub fn value(&self) -> usize {
        match self {
            SubmitWindow::Finite(n) => *n,
            SubmitWindow::Infinite => usize::MAX,
        }
    }
}

impl std::fmt::Display for SubmitWindow {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            SubmitWindow::Finite(n) => write!(f, "{}", n),
            SubmitWindow::Infinite => write!(f, "inf"),
        }
    }
}

/// Fault mode for a run
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FaultMode {
    NONE,
    TIMEOUT,
    RESET,
}

impl std::fmt::Display for FaultMode {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            FaultMode::NONE => write!(f, "NONE"),
            FaultMode::TIMEOUT => write!(f, "TIMEOUT"),
            FaultMode::RESET => write!(f, "RESET"),
        }
    }
}

impl std::str::FromStr for FaultMode {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_uppercase().as_str() {
            "NONE" => Ok(FaultMode::NONE),
            "TIMEOUT" => Ok(FaultMode::TIMEOUT),
            "RESET" => Ok(FaultMode::RESET),
            _ => Err(format!("Unknown fault mode: {}", s)),
        }
    }
}

/// Logger that writes the exact log format
pub struct Logger {
    buffer: Vec<String>,
}

impl Logger {
    pub fn new() -> Self {
        Self { buffer: Vec::new() }
    }

    /// Write the run header
    #[allow(dead_code)]
    pub fn write_header(
        &mut self,
        run_id: &str,
        seed_id: &str,
        schedule_seed: u64,
        policy: Policy,
        bound_k: BoundK,
        fault_mode: FaultMode,
        n_cmds: usize,
        scheduler_version: &str,
        git_commit: &str,
    ) {
        let line = format!(
            "RUN_HEADER(run_id={}, seed_id={}, schedule_seed={}, policy={}, bound_k={}, fault_mode={}, n_cmds={}, scheduler_version={}, git_commit={})",
            run_id, seed_id, schedule_seed, policy, bound_k, fault_mode, n_cmds, scheduler_version, git_commit
        );
        self.buffer.push(line);
    }

    /// Write the run header with submit_window
    pub fn write_header_with_window(
        &mut self,
        run_id: &str,
        seed_id: &str,
        schedule_seed: u64,
        policy: Policy,
        bound_k: BoundK,
        fault_mode: FaultMode,
        n_cmds: usize,
        scheduler_version: &str,
        git_commit: &str,
        submit_window: SubmitWindow,
    ) {
        let line = format!(
            "RUN_HEADER(run_id={}, seed_id={}, schedule_seed={}, policy={}, bound_k={}, fault_mode={}, n_cmds={}, submit_window={}, scheduler_version={}, git_commit={})",
            run_id, seed_id, schedule_seed, policy, bound_k, fault_mode, n_cmds, submit_window, scheduler_version, git_commit
        );
        self.buffer.push(line);
    }

    /// Log a SUBMIT event
    pub fn log_submit(&mut self, cmd_id: u32, cmd_type: &str) {
        let line = format!("SUBMIT(cmd_id={}, cmd_type={})", cmd_id, cmd_type);
        self.buffer.push(line);
    }

    /// Log a FENCE event
    pub fn log_fence(&mut self, fence_id: u32) {
        let line = format!("FENCE(fence_id={})", fence_id);
        self.buffer.push(line);
    }

    /// Log a COMPLETE event
    pub fn log_complete(&mut self, cmd_id: u32, status: Status, output: u32) {
        let line = format!(
            "COMPLETE(cmd_id={}, status={}, out={})",
            cmd_id, status, output
        );
        self.buffer.push(line);
    }

    /// Log a RESET event
    pub fn log_reset(&mut self, reason: &str, pending_before: u32) {
        let line = format!(
            "RESET(reason={}, pending_before={})",
            reason, pending_before
        );
        self.buffer.push(line);
    }

    /// Log the RUN_END event
    pub fn log_run_end(&mut self, pending_left: u32, pending_peak: u32) {
        let line = format!(
            "RUN_END(pending_left={}, pending_peak={})",
            pending_left, pending_peak
        );
        self.buffer.push(line);
    }

    /// Write log to a file
    pub fn write_to_file(&self, path: &std::path::Path) -> Result<()> {
        let mut file = std::fs::File::create(path)?;
        for line in &self.buffer {
            writeln!(file, "{}", line)?;
        }
        Ok(())
    }

    /// Get log as string (for testing)
    #[allow(dead_code)]
    pub fn to_string(&self) -> String {
        self.buffer.join("\n")
    }

    /// Get log lines
    #[allow(dead_code)]
    pub fn lines(&self) -> &[String] {
        &self.buffer
    }
}

impl Default for Logger {
    fn default() -> Self {
        Self::new()
    }
}

/// Schedule serialization for reproducibility
#[derive(Debug, serde::Serialize, serde::Deserialize)]
pub struct SerializedSchedule {
    pub seed_id: String,
    pub schedule_seed: u64,
    pub policy: String,
    pub bound_k: String,
    pub fault_mode: String,
    pub steps: Vec<ScheduleStep>,
}

#[derive(Debug, serde::Serialize, serde::Deserialize)]
#[serde(tag = "type")]
pub enum ScheduleStep {
    CompletePick { pick_index: usize },
    FAULT { fault_type: String, at_step: usize },
}

impl SerializedSchedule {
    pub fn new(
        seed_id: &str,
        schedule_seed: u64,
        policy: Policy,
        bound_k: BoundK,
        fault_mode: FaultMode,
    ) -> Self {
        Self {
            seed_id: seed_id.to_string(),
            schedule_seed,
            policy: policy.to_string(),
            bound_k: bound_k.to_string(),
            fault_mode: fault_mode.to_string(),
            steps: Vec::new(),
        }
    }

    pub fn add_complete(&mut self, pick_index: usize) {
        self.steps.push(ScheduleStep::CompletePick { pick_index });
    }

    pub fn add_fault(&mut self, fault_type: &str, at_step: usize) {
        self.steps.push(ScheduleStep::FAULT {
            fault_type: fault_type.to_string(),
            at_step,
        });
    }

    pub fn write_to_file(&self, path: &std::path::Path) -> Result<()> {
        let json = serde_json::to_string_pretty(self)?;
        std::fs::write(path, json)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_log_format() {
        let mut logger = Logger::new();

        logger.write_header(
            "seed_001_FIFO_0_42_NONE",
            "seed_001",
            42,
            Policy::FIFO,
            BoundK::Finite(0),
            FaultMode::NONE,
            3,
            "v1.0",
            "abc123",
        );

        logger.log_submit(0, "WRITE");
        logger.log_submit(1, "READ");
        logger.log_submit(2, "FENCE");
        logger.log_fence(0);
        logger.log_complete(0, Status::OK, 0);
        logger.log_complete(1, Status::OK, 12345);
        logger.log_complete(2, Status::OK, 0);
        logger.log_run_end(0, 2);

        let log = logger.to_string();
        assert!(log.contains("RUN_HEADER("));
        assert!(log.contains("SUBMIT(cmd_id=0, cmd_type=WRITE)"));
        assert!(log.contains("FENCE(fence_id=0)"));
        assert!(log.contains("COMPLETE(cmd_id=0, status=OK, out=0)"));
        assert!(log.contains("RUN_END(pending_left=0, pending_peak=2)"));
    }
}
