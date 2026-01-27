//! Seed loading and command definitions
//!
//! Commands are loaded from JSON files and represent the workload to execute.

use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};
use std::path::Path;

/// A single command in the workload
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type")]
#[allow(non_camel_case_types)]
pub enum Command {
    /// Write data to the device
    WRITE { lba: u64, len: u32, pattern: u32 },
    /// Read data from the device
    READ { lba: u64, len: u32 },
    /// Fence - ordering barrier
    FENCE,
    /// Make recent WRITEs visible to the device (host->device flush)
    WRITE_VISIBLE { lba: u64, len: u32 },
}

impl Command {
    /// Get the command type as a string for logging
    pub fn type_name(&self) -> &'static str {
        match self {
            Command::WRITE { .. } => "WRITE",
            Command::READ { .. } => "READ",
            Command::FENCE => "FENCE",
            Command::WRITE_VISIBLE { .. } => "WRITE_VISIBLE",
        }
    }
}

/// A seed file containing commands to execute
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Seed {
    pub seed_id: String,
    pub commands: Vec<Command>,
}

impl Seed {
    /// Load a seed from a JSON file
    pub fn load(path: &Path) -> Result<Self> {
        let content = std::fs::read_to_string(path)
            .with_context(|| format!("Failed to read seed file: {}", path.display()))?;
        let seed: Seed = serde_json::from_str(&content)
            .with_context(|| format!("Failed to parse seed file: {}", path.display()))?;
        Ok(seed)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_seed() {
        let json = r#"{
            "seed_id": "test_001",
            "commands": [
                {"type": "WRITE", "lba": 0, "len": 4, "pattern": 123},
                {"type": "READ", "lba": 0, "len": 4},
                {"type": "FENCE"}
            ]
        }"#;
        let seed: Seed = serde_json::from_str(json).unwrap();
        assert_eq!(seed.seed_id, "test_001");
        assert_eq!(seed.commands.len(), 3);
    }
}
