//! NVMe-lite Model (Oracle/Reference Implementation)
//!
//! This module implements a simplified NVMe queue model with:
//! - Submission queue (host writes commands)
//! - Completion queue (device writes completions)
//! - Phase tag for wrap-around detection
//! - Deterministic execution for differential testing

use crate::seed::Command;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

/// Device storage size (in u32 words)
const STORAGE_SIZE: usize = 1024;

/// Terminal status of a command
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum Status {
    OK,
    ERR,
    TIMEOUT,
}

impl std::fmt::Display for Status {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Status::OK => write!(f, "OK"),
            Status::ERR => write!(f, "ERR"),
            Status::TIMEOUT => write!(f, "TIMEOUT"),
        }
    }
}

/// A pending command waiting for completion
#[derive(Debug, Clone)]
#[allow(dead_code)]
pub struct PendingCommand {
    pub cmd_id: u32,
    pub command: Command,
    pub fence_id: Option<u32>, // Which fence this command is associated with (if any)
}

/// Result of executing a command
#[derive(Debug, Clone)]
pub struct CommandResult {
    pub cmd_id: u32,
    pub status: Status,
    pub output: u32, // Hash/result code
}

/// The NVMe-lite model state
#[derive(Debug)]
pub struct NvmeLiteModel {
    /// Device storage (simplified as array of u32)
    host_storage: Vec<u32>,
    dev_storage: Vec<u32>,

    /// All submitted commands (for ordering tracking)
    submitted: Vec<PendingCommand>,

    /// Commands waiting for completion (cmd_id -> index in submitted)
    pending: HashMap<u32, usize>,

    /// Completed commands in completion order
    completed: Vec<CommandResult>,

    /// Next command ID to assign
    next_cmd_id: u32,

    /// Current fence ID (increments on each FENCE command)
    current_fence_id: u32,

    /// Fence completion tracking: fence_id -> number of commands before it that completed
    fence_tracking: HashMap<u32, (u32, u32)>, // (total_before, completed_before)

    /// Peak number of pending commands (for SSI metric)
    pending_peak: u32,

    /// Current phase tag (NVMe-style, toggles on wrap)
    #[allow(dead_code)]
    phase_tag: bool,

    /// Whether we've had a reset
    had_reset: bool,

    /// Commands lost to reset (for RCS)
    commands_lost_to_reset: u32,
}

impl NvmeLiteModel {
    /// Create a new model instance
    pub fn new() -> Self {
        Self {
            host_storage: vec![0; STORAGE_SIZE],
            dev_storage: vec![0; STORAGE_SIZE],
            submitted: Vec::new(),
            pending: HashMap::new(),
            completed: Vec::new(),
            next_cmd_id: 0,
            current_fence_id: 0,
            fence_tracking: HashMap::new(),
            pending_peak: 0,
            phase_tag: true, // NVMe 1.0: initial phase = 1
            had_reset: false,
            commands_lost_to_reset: 0,
        }
    }

    /// Submit a command to the model
    /// Returns (cmd_id, is_fence, fence_id_if_fence)
    pub fn submit(&mut self, command: Command) -> (u32, bool, Option<u32>) {
        let cmd_id = self.next_cmd_id;
        self.next_cmd_id += 1;

        // Check if this is a fence
        let is_fence = matches!(command, Command::FENCE);
        let fence_id = if is_fence {
            let fid = self.current_fence_id;
            self.current_fence_id += 1;

            // Track how many commands were submitted before this fence
            let commands_before = cmd_id; // All commands with ID < fence's cmd_id
            self.fence_tracking.insert(fid, (commands_before, 0));

            Some(fid)
        } else {
            None
        };

        let pending_cmd = PendingCommand {
            cmd_id,
            command,
            fence_id,
        };

        let idx = self.submitted.len();
        self.submitted.push(pending_cmd);
        self.pending.insert(cmd_id, idx);

        // Update peak
        let current_pending = self.pending.len() as u32;
        if current_pending > self.pending_peak {
            self.pending_peak = current_pending;
        }

        (cmd_id, is_fence, fence_id)
    }

    /// Get list of pending command IDs in submission order (canonical)
    pub fn get_pending_canonical(&self) -> Vec<u32> {
        let mut pending: Vec<u32> = self.pending.keys().copied().collect();
        pending.sort(); // Canonical order = sorted by cmd_id (submission order)
        pending
    }

    /// Get pending count
    pub fn pending_count(&self) -> usize {
        self.pending.len()
    }

    /// Get peak pending
    pub fn pending_peak(&self) -> u32 {
        self.pending_peak
    }

    /// Complete a command by its cmd_id
    /// Returns the result if the command was pending, None otherwise
    pub fn complete(&mut self, cmd_id: u32, force_status: Option<Status>) -> Option<CommandResult> {
        let idx = self.pending.remove(&cmd_id)?;

        // Clone the command to avoid borrow issues
        let command = self.submitted[idx].command.clone();

        // Execute the command and determine status/output
        let (status, output) = if let Some(forced) = force_status {
            (forced, 0)
        } else {
            self.execute_command(&command)
        };

        let result = CommandResult {
            cmd_id,
            status,
            output,
        };

        // Update fence tracking
        for (_fid, (total, completed)) in self.fence_tracking.iter_mut() {
            if cmd_id < *total {
                *completed += 1;
            }
        }

        self.completed.push(result.clone());
        Some(result)
    }

    /// Execute a command and return (status, output)
    fn execute_command(&mut self, command: &Command) -> (Status, u32) {
        match command {
            Command::WRITE { lba, len, pattern } => {
                let start = *lba as usize;
                let end = start + *len as usize;

                if end > self.host_storage.len() {
                    return (Status::ERR, 0);
                }

                for i in start..end {
                    self.host_storage[i] = *pattern;
                }
                (Status::OK, 0)
            }
            Command::READ { lba, len } => {
                let start = *lba as usize;
                let end = start + *len as usize;

                if end > self.dev_storage.len() {
                    return (Status::ERR, 0);
                }

                // Compute simple hash of read data
                let mut hash: u32 = 0;
                for i in start..end {
                    hash = hash.wrapping_mul(31).wrapping_add(self.dev_storage[i]);
                }
                (Status::OK, hash)
            }
            Command::FENCE => {
                // Fence itself just completes OK
                (Status::OK, 0)
            }
            Command::WRITE_VISIBLE { lba, len } => {
                let start = *lba as usize;
                let end = start + *len as usize;
                if end > self.dev_storage.len() {
                    return (Status::ERR, 0);
                }
                for i in start..end {
                    self.dev_storage[i] = self.host_storage[i];
                }
                (Status::OK, 0)
            }
        }
    }

    /// Perform a reset - clears all pending commands
    /// Returns the number of commands that were pending
    pub fn reset(&mut self) -> u32 {
        let pending_before = self.pending.len() as u32;
        self.commands_lost_to_reset = pending_before;
        self.pending.clear();
        self.had_reset = true;
        pending_before
    }

    /// Get commands lost to reset (for RCS calculation)
    pub fn commands_lost(&self) -> u32 {
        self.commands_lost_to_reset
    }

    /// Check if a reset occurred
    pub fn had_reset(&self) -> bool {
        self.had_reset
    }

    /// Get submission order (list of cmd_ids in submit order)
    #[allow(dead_code)]
    pub fn get_submit_order(&self) -> Vec<u32> {
        self.submitted.iter().map(|p| p.cmd_id).collect()
    }

    /// Get completion order (list of cmd_ids in completion order)
    #[allow(dead_code)]
    pub fn get_complete_order(&self) -> Vec<u32> {
        self.completed.iter().map(|r| r.cmd_id).collect()
    }

    /// Get fence data for FE calculation
    /// Returns Vec of (fence_cmd_id, commands_before_fence)
    #[allow(dead_code)]
    pub fn get_fence_data(&self) -> Vec<(u32, Vec<u32>)> {
        let mut result = Vec::new();

        for pending in &self.submitted {
            if let Some(_fence_id) = pending.fence_id {
                let fence_cmd_id = pending.cmd_id;
                // Commands before this fence are all with cmd_id < fence_cmd_id
                let before: Vec<u32> = self
                    .submitted
                    .iter()
                    .filter(|p| p.cmd_id < fence_cmd_id && p.fence_id.is_none())
                    .map(|p| p.cmd_id)
                    .collect();
                result.push((fence_cmd_id, before));
            }
        }
        result
    }
}

impl Default for NvmeLiteModel {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_submit_and_complete() {
        let mut model = NvmeLiteModel::new();

        let (id0, _, _) = model.submit(Command::WRITE {
            lba: 0,
            len: 4,
            pattern: 42,
        });
        let (id1, _, _) = model.submit(Command::READ { lba: 0, len: 4 });

        assert_eq!(model.pending_count(), 2);
        assert_eq!(model.get_pending_canonical(), vec![0, 1]);

        let result = model.complete(id0, None).unwrap();
        assert_eq!(result.status, Status::OK);
        assert_eq!(model.pending_count(), 1);

        let result = model.complete(id1, None).unwrap();
        assert_eq!(result.status, Status::OK);
        assert_eq!(model.pending_count(), 0);
    }

    #[test]
    fn test_write_read_consistency() {
        let mut model = NvmeLiteModel::new();

        model.submit(Command::WRITE {
            lba: 0,
            len: 4,
            pattern: 123,
        });
        model.complete(0, None);

        model.submit(Command::READ { lba: 0, len: 4 });
        let result = model.complete(1, None).unwrap();

        // Hash of [123, 123, 123, 123]
        let expected_hash = 123_u32
            .wrapping_mul(31)
            .wrapping_add(123)
            .wrapping_mul(31)
            .wrapping_add(123)
            .wrapping_mul(31)
            .wrapping_add(123);
        assert_eq!(result.output, expected_hash);
    }

    #[test]
    fn test_reset() {
        let mut model = NvmeLiteModel::new();

        model.submit(Command::WRITE {
            lba: 0,
            len: 4,
            pattern: 42,
        });
        model.submit(Command::READ { lba: 0, len: 4 });

        let lost = model.reset();
        assert_eq!(lost, 2);
        assert_eq!(model.pending_count(), 0);
        assert!(model.had_reset());
    }
}
