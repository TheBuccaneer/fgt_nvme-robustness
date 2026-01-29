#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "rng.h"
#include <stdint.h>
#include <stddef.h>

/**
 * Scheduling policies
 */
typedef enum {
    POLICY_FIFO,
    POLICY_RANDOM,
    POLICY_ADVERSARIAL,
    POLICY_BATCHED
} Policy;

/**
 * Bound k value - can be finite or infinite
 */
typedef struct {
    int is_infinite;
    uint32_t value;
} BoundK;

/**
 * A scheduling decision
 */
typedef struct {
    size_t pick_index;  /* Index in candidate list */
    uint32_t cmd_id;    /* The selected cmd_id */
} Decision;

/**
 * Scheduler state
 */
typedef struct {
    Policy policy;
    BoundK bound_k;
    Rng rng;
    size_t batch_size;
} Scheduler;

/* Policy string conversion */
const char* policy_to_string(Policy p);
int policy_parse(const char *s, Policy *out);

/* BoundK helpers */
BoundK bound_k_finite(uint32_t k);
BoundK bound_k_infinite(void);
const char* bound_k_to_string(BoundK k, char *buf, size_t buflen);
int bound_k_parse(const char *s, BoundK *out);

/* Scheduler functions */
void scheduler_init(Scheduler *sched, Policy policy, BoundK bound_k, uint64_t schedule_seed);

/** Get next random bit for submit/complete decision */
uint64_t scheduler_next_bit(Scheduler *sched);

/**
 * Get candidates from pending list based on bound_k.
 * pending must be sorted by cmd_id (canonical order).
 * Returns number of candidates (writes to out_count).
 */
size_t scheduler_get_candidates_count(Scheduler *sched, size_t pending_count);

/**
 * Pick next command to complete.
 * pending: array of cmd_ids in canonical order
 * pending_count: number of pending commands
 * out_decision: receives the decision if return value is 1
 * Returns 1 if a decision was made, 0 if no pending commands
 */
int scheduler_pick_next(Scheduler *sched, const uint32_t *pending, size_t pending_count, Decision *out_decision);

#endif /* SCHEDULER_H */
