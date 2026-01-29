#include "scheduler.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

const char* policy_to_string(Policy p) {
    switch (p) {
        case POLICY_FIFO:        return "FIFO";
        case POLICY_RANDOM:      return "RANDOM";
        case POLICY_ADVERSARIAL: return "ADVERSARIAL";
        case POLICY_BATCHED:     return "BATCHED";
        default:                 return "UNKNOWN";
    }
}

int policy_parse(const char *s, Policy *out) {
    if (!s || !out) return -1;
    
    /* Convert to uppercase for comparison */
    char buf[32];
    size_t i;
    for (i = 0; i < sizeof(buf) - 1 && s[i]; i++) {
        buf[i] = toupper((unsigned char)s[i]);
    }
    buf[i] = '\0';
    
    if (strcmp(buf, "FIFO") == 0) {
        *out = POLICY_FIFO;
        return 0;
    }
    if (strcmp(buf, "RANDOM") == 0) {
        *out = POLICY_RANDOM;
        return 0;
    }
    if (strcmp(buf, "ADVERSARIAL") == 0) {
        *out = POLICY_ADVERSARIAL;
        return 0;
    }
    if (strcmp(buf, "BATCHED") == 0) {
        *out = POLICY_BATCHED;
        return 0;
    }
    return -1;
}

BoundK bound_k_finite(uint32_t k) {
    BoundK bk;
    bk.is_infinite = 0;
    bk.value = k;
    return bk;
}

BoundK bound_k_infinite(void) {
    BoundK bk;
    bk.is_infinite = 1;
    bk.value = 0;
    return bk;
}

const char* bound_k_to_string(BoundK k, char *buf, size_t buflen) {
    if (k.is_infinite) {
        snprintf(buf, buflen, "inf");
    } else {
        snprintf(buf, buflen, "%u", k.value);
    }
    return buf;
}

int bound_k_parse(const char *s, BoundK *out) {
    if (!s || !out) return -1;
    
    /* Check for "inf" (case insensitive) */
    char buf[16];
    size_t i;
    for (i = 0; i < sizeof(buf) - 1 && s[i]; i++) {
        buf[i] = tolower((unsigned char)s[i]);
    }
    buf[i] = '\0';
    
    if (strcmp(buf, "inf") == 0) {
        *out = bound_k_infinite();
        return 0;
    }
    
    /* Try parsing as number */
    char *end;
    unsigned long val = strtoul(s, &end, 10);
    if (end == s || *end != '\0') return -1;
    
    *out = bound_k_finite((uint32_t)val);
    return 0;
}

void scheduler_init(Scheduler *sched, Policy policy, BoundK bound_k, uint64_t schedule_seed) {
    sched->policy = policy;
    sched->bound_k = bound_k;
    rng_init(&sched->rng, schedule_seed);
    sched->batch_size = 4;  /* Fixed batch size for BATCHED policy */
}

uint64_t scheduler_next_bit(Scheduler *sched) {
    return rng_next_bit(&sched->rng);
}

size_t scheduler_get_candidates_count(Scheduler *sched, size_t pending_count) {
    if (pending_count == 0) return 0;
    
    if (sched->bound_k.is_infinite) {
        return pending_count;
    }
    
    size_t k = sched->bound_k.value;
    #if INJECT_BUG_ID == 3
    size_t max_idx = (k + 1 < pending_count) ? (k + 1) : (pending_count - 1);  // Bug: erlaubt k+1 statt k
    return max_idx + 1;
    #else
    size_t max_idx = (k < pending_count - 1) ? k : pending_count - 1;
    return max_idx + 1;
    #endif
}

int scheduler_pick_next(Scheduler *sched, const uint32_t *pending, size_t pending_count, Decision *out_decision) {
    if (pending_count == 0) return 0;
    
    size_t n_candidates = scheduler_get_candidates_count(sched, pending_count);
    if (n_candidates == 0) return 0;
    
    size_t pick_index;
    
    switch (sched->policy) {
        case POLICY_FIFO:
            /* Always pick first (smallest cmd_id) */
            pick_index = 0;
            break;
            
        case POLICY_RANDOM:
            /* Random selection among candidates */
            pick_index = (size_t)rng_range(&sched->rng, n_candidates);
            break;
            
        case POLICY_ADVERSARIAL:
            /* Pick last (largest cmd_id = maximum reordering) */
            pick_index = n_candidates - 1;
            break;
            
        case POLICY_BATCHED:
            /* For batched, use RANDOM selection within candidates (not FIFO) */
            pick_index = (size_t)rng_range(&sched->rng, n_candidates);
            break;
            
        default:
            pick_index = 0;
            break;
    }
    
    out_decision->pick_index = pick_index;
    out_decision->cmd_id = pending[pick_index];
    return 1;
}
