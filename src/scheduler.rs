//! Scheduler: Completion ordering policies and bound_k logic
//!
//! The scheduler determines which pending command completes next.
//! - bound_k limits reordering: only the first k+1 pending commands are candidates
//! - Policies select among candidates: FIFO, RANDOM, ADVERSARIAL, BATCHED

use serde::{Deserialize, Serialize};

#[derive(Clone, Debug)]
struct SplitMix64 {
    state: u64,
}

impl SplitMix64 {
    fn seed_from_u64(seed: u64) -> Self {
        Self { state: seed }
    }

    fn next_u64(&mut self) -> u64 {
        // splitmix64
        self.state = self.state.wrapping_add(0x9E3779B97F4A7C15);
        let mut z = self.state;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58476D1CE4E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D049BB133111EB);
        z ^ (z >> 31)
    }

    fn next_bit(&mut self) -> u64 {
        self.next_u64() & 1
    }

    fn gen_index(&mut self, len: usize) -> usize {
        // entspricht grob dem C-Pattern "u64 % len"
        (self.next_u64() as usize) % len
    }
}

/// Scheduling policy
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "UPPERCASE")]
pub enum Policy {
    /// Complete oldest pending first (smallest cmd_id)
    FIFO,
    /// Random selection among candidates
    RANDOM,
    /// Worst-case: select largest cmd_id among candidates (max reordering)
    ADVERSARIAL,
    /// Batch completions: collect n decisions, then emit all at once
    BATCHED,
}

impl std::fmt::Display for Policy {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Policy::FIFO => write!(f, "FIFO"),
            Policy::RANDOM => write!(f, "RANDOM"),
            Policy::ADVERSARIAL => write!(f, "ADVERSARIAL"),
            Policy::BATCHED => write!(f, "BATCHED"),
        }
    }
}

impl std::str::FromStr for Policy {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_uppercase().as_str() {
            "FIFO" => Ok(Policy::FIFO),
            "RANDOM" => Ok(Policy::RANDOM),
            "ADVERSARIAL" => Ok(Policy::ADVERSARIAL),
            "BATCHED" => Ok(Policy::BATCHED),
            _ => Err(format!("Unknown policy: {}", s)),
        }
    }
}

/// Bound k value - can be finite or infinite
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(untagged)]
pub enum BoundK {
    Finite(u32),
    Infinite,
}

impl BoundK {
    /// Parse from string: "inf" or a number
    pub fn parse(s: &str) -> Result<Self, String> {
        if s.to_lowercase() == "inf" {
            Ok(BoundK::Infinite)
        } else {
            s.parse::<u32>()
                .map(BoundK::Finite)
                .map_err(|_| format!("Invalid bound_k: {}", s))
        }
    }

    /// Get the actual bound value (usize::MAX for infinite)
    #[allow(dead_code)]
    pub fn value(&self) -> usize {
        match self {
            BoundK::Finite(k) => *k as usize,
            BoundK::Infinite => usize::MAX,
        }
    }
}

impl std::fmt::Display for BoundK {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            BoundK::Finite(k) => write!(f, "{}", k),
            BoundK::Infinite => write!(f, "inf"),
        }
    }
}

/// A scheduling decision
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Decision {
    /// Index in the candidate list (for schedule serialization)
    pub pick_index: usize,
    /// The actual cmd_id selected
    pub cmd_id: u32,
}

/// Scheduler state
pub struct Scheduler {
    policy: Policy,
    bound_k: BoundK,
    rng: SplitMix64,
    #[allow(dead_code)]
    batch_size: usize,
    #[allow(dead_code)]
    batch_buffer: Vec<Decision>,
    decisions: Vec<Decision>,
}

impl Scheduler {
    /// Create a new scheduler
    pub fn new(policy: Policy, bound_k: BoundK, schedule_seed: u64) -> Self {
        Self {
            policy,
            bound_k,
            rng: SplitMix64::seed_from_u64(schedule_seed),
            batch_size: 4, // Fixed batch size for BATCHED policy
            batch_buffer: Vec::new(),
            decisions: Vec::new(),
        }
    }

    /// Get next random bit (0 or 1) for submit/complete decision
    pub fn next_bit(&mut self) -> u64 {
        self.rng.next_bit()
    }

    /// Get candidates from pending list based on bound_k
    /// pending must be in canonical order (sorted by cmd_id)
    pub fn get_candidates<'a>(&self, pending: &'a [u32]) -> &'a [u32] {
        if pending.is_empty() {
            return pending;
        }

        let max_idx = match self.bound_k {
            BoundK::Finite(k) => std::cmp::min(k as usize, pending.len() - 1),
            BoundK::Infinite => pending.len() - 1,
        };

        &pending[0..=max_idx]
    }

    /// Pick the next command to complete
    /// Returns None if no pending commands
    pub fn pick_next(&mut self, pending: &[u32]) -> Option<Decision> {
        let candidates = self.get_candidates(pending);
        if candidates.is_empty() {
            return None;
        }

        let (pick_index, cmd_id) = match self.policy {
            Policy::FIFO => {
                // Always pick first (smallest cmd_id)
                (0, candidates[0])
            }
            Policy::RANDOM => {
                // Random selection
                let idx = self.rng.gen_index(candidates.len());
                (idx, candidates[idx])
            }
            Policy::ADVERSARIAL => {
                // Pick last (largest cmd_id = maximum reordering)
                let idx = candidates.len() - 1;
                (idx, candidates[idx])
            }
            Policy::BATCHED => {
                // For batched, use RANDOM selection within candidates (matches C DUT)
                // The actual batching/burst logic is handled by the runner
                let idx = self.rng.gen_index(candidates.len());

                (idx, candidates[idx])
            }
        };

        let decision = Decision { pick_index, cmd_id };
        self.decisions.push(decision.clone());
        Some(decision)
    }

    /// Get all decisions made (for schedule serialization)
    #[allow(dead_code)]
    pub fn get_decisions(&self) -> &[Decision] {
        &self.decisions
    }

    /// Get policy
    #[allow(dead_code)]
    pub fn policy(&self) -> Policy {
        self.policy
    }

    /// Get bound_k
    #[allow(dead_code)]
    pub fn bound_k(&self) -> BoundK {
        self.bound_k
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_bound_k_candidates() {
        // bound_k = 0: only first element
        let sched = Scheduler::new(Policy::FIFO, BoundK::Finite(0), 0);
        let pending = vec![0, 1, 2, 3, 4];
        assert_eq!(sched.get_candidates(&pending), &[0]);

        // bound_k = 2: first 3 elements
        let sched = Scheduler::new(Policy::FIFO, BoundK::Finite(2), 0);
        assert_eq!(sched.get_candidates(&pending), &[0, 1, 2]);

        // bound_k = inf: all elements
        let sched = Scheduler::new(Policy::FIFO, BoundK::Infinite, 0);
        assert_eq!(sched.get_candidates(&pending), &[0, 1, 2, 3, 4]);
    }

    #[test]
    fn test_fifo_policy() {
        let mut sched = Scheduler::new(Policy::FIFO, BoundK::Infinite, 0);
        let pending = vec![2, 5, 7];
        let decision = sched.pick_next(&pending).unwrap();
        assert_eq!(decision.cmd_id, 2);
        assert_eq!(decision.pick_index, 0);
    }

    #[test]
    fn test_adversarial_policy() {
        let mut sched = Scheduler::new(Policy::ADVERSARIAL, BoundK::Infinite, 0);
        let pending = vec![2, 5, 7];
        let decision = sched.pick_next(&pending).unwrap();
        assert_eq!(decision.cmd_id, 7);
        assert_eq!(decision.pick_index, 2);
    }

    #[test]
    fn test_random_determinism() {
        let mut sched1 = Scheduler::new(Policy::RANDOM, BoundK::Infinite, 42);
        let mut sched2 = Scheduler::new(Policy::RANDOM, BoundK::Infinite, 42);
        let pending = vec![0, 1, 2, 3, 4];

        // Same seed should give same results
        for _ in 0..10 {
            let d1 = sched1.pick_next(&pending).unwrap();
            let d2 = sched2.pick_next(&pending).unwrap();
            assert_eq!(d1.cmd_id, d2.cmd_id);
        }
    }

    #[test]
    fn test_bound_k_with_adversarial() {
        // With bound_k=1 and adversarial, should pick index 1 (second element)
        let mut sched = Scheduler::new(Policy::ADVERSARIAL, BoundK::Finite(1), 0);
        let pending = vec![0, 5, 10, 15];
        let decision = sched.pick_next(&pending).unwrap();
        assert_eq!(decision.cmd_id, 5); // candidates are [0, 5], adversarial picks 5
        assert_eq!(decision.pick_index, 1);
    }
}
