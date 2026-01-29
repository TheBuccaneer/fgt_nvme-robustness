//! Configuration: Load experiment matrix from YAML
//!
//! Supports loading configs/main.yaml with:
//! - seeds, policies, bounds, faults, schedule_seeds

use crate::logging::FaultMode;
use crate::scheduler::{BoundK, Policy};
use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};
use std::path::Path;

/// Raw YAML configuration
#[derive(Debug, Deserialize, Serialize)]
pub struct RawConfig {
    pub seeds: Vec<String>,
    pub policies: Vec<String>,
    pub bounds: Vec<String>,
    pub faults: Vec<String>,
    pub schedule_seeds: String,
    pub scheduler_version: String,
    #[serde(default)]
    pub git_commit: String,
}

/// Parsed configuration ready for use
#[derive(Debug)]
pub struct ExperimentConfig {
    pub seeds: Vec<String>,
    pub policies: Vec<Policy>,
    pub bounds: Vec<BoundK>,
    pub faults: Vec<FaultMode>,
    pub schedule_seed_range: (u64, u64), // (start, end) inclusive
    pub scheduler_version: String,
    pub git_commit: String,
}

impl ExperimentConfig {
    /// Load and parse configuration from YAML file
    pub fn load(path: &Path) -> Result<Self> {
        let content = std::fs::read_to_string(path)
            .with_context(|| format!("Failed to read config: {}", path.display()))?;
        let raw: RawConfig = serde_yaml::from_str(&content)
            .with_context(|| format!("Failed to parse config: {}", path.display()))?;

        // Parse policies
        let policies: Result<Vec<Policy>, _> = raw.policies.iter().map(|s| s.parse()).collect();
        let policies = policies.map_err(|e| anyhow::anyhow!("Invalid policy: {}", e))?;

        // Parse bounds
        let bounds: Result<Vec<BoundK>, _> = raw.bounds.iter().map(|s| BoundK::parse(s)).collect();
        let bounds = bounds.map_err(|e| anyhow::anyhow!("Invalid bound: {}", e))?;

        // Parse faults
        let faults: Result<Vec<FaultMode>, _> = raw.faults.iter().map(|s| s.parse()).collect();
        let faults = faults.map_err(|e| anyhow::anyhow!("Invalid fault mode: {}", e))?;

        // Parse schedule_seeds range (format: "0-99" or "0")
        let schedule_seed_range = parse_range(&raw.schedule_seeds)?;

        // Get git commit if "auto"
        let git_commit = if raw.git_commit == "auto" {
            get_git_commit().unwrap_or_default()
        } else {
            raw.git_commit
        };

        Ok(ExperimentConfig {
            seeds: raw.seeds,
            policies,
            bounds,
            faults,
            schedule_seed_range,
            scheduler_version: raw.scheduler_version,
            git_commit,
        })
    }

    /// Generate all schedule seeds in range
    pub fn schedule_seeds(&self) -> impl Iterator<Item = u64> {
        self.schedule_seed_range.0..=self.schedule_seed_range.1
    }

    /// Count total number of runs
    pub fn total_runs(&self) -> usize {
        let n_seeds = self.seeds.len();
        let n_policies = self.policies.len();
        let n_bounds = self.bounds.len();
        let n_faults = self.faults.len();
        let n_schedules = (self.schedule_seed_range.1 - self.schedule_seed_range.0 + 1) as usize;

        n_seeds * n_policies * n_bounds * n_faults * n_schedules
    }
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

/// Try to get current git commit hash
fn get_git_commit() -> Option<String> {
    std::process::Command::new("git")
        .args(["rev-parse", "HEAD"])
        .output()
        .ok()
        .and_then(|output| {
            if output.status.success() {
                String::from_utf8(output.stdout)
                    .ok()
                    .map(|s| s.trim().to_string())
            } else {
                None
            }
        })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_range() {
        assert_eq!(parse_range("0-99").unwrap(), (0, 99));
        assert_eq!(parse_range("42").unwrap(), (42, 42));
        assert_eq!(parse_range("0-0").unwrap(), (0, 0));
    }

    #[test]
    fn test_parse_config() {
        let yaml = r#"
seeds:
  - "seeds/seed_001.json"
  - "seeds/seed_002.json"
policies:
  - FIFO
  - RANDOM
bounds:
  - "0"
  - "inf"
faults:
  - NONE
schedule_seeds: "0-9"
scheduler_version: "v1.0"
git_commit: ""
"#;
        let raw: RawConfig = serde_yaml::from_str(yaml).unwrap();
        assert_eq!(raw.seeds.len(), 2);
        assert_eq!(raw.policies.len(), 2);
    }
}
