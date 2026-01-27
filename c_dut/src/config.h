#ifndef CONFIG_H
#define CONFIG_H

#include "logging.h"
#include "scheduler.h"
#include <stdint.h>
#include <stddef.h>

/** Maximum array sizes */
#define MAX_SEEDS 256
#define MAX_POLICIES 8
#define MAX_BOUNDS 16
#define MAX_FAULTS 8

/**
 * Experiment configuration loaded from YAML
 */
typedef struct {
    char *seeds[MAX_SEEDS];
    size_t n_seeds;
    
    Policy policies[MAX_POLICIES];
    size_t n_policies;
    
    BoundK bounds[MAX_BOUNDS];
    size_t n_bounds;
    
    FaultMode faults[MAX_FAULTS];
    size_t n_faults;
    
    uint64_t schedule_seed_start;
    uint64_t schedule_seed_end;
    
    char scheduler_version[64];
    char git_commit[128];
} ExperimentConfig;

/**
 * Load configuration from YAML file.
 * Returns 0 on success, -1 on error.
 */
int config_load(const char *path, ExperimentConfig *config);

/**
 * Free configuration resources.
 */
void config_free(ExperimentConfig *config);

/**
 * Count total number of runs in matrix.
 */
size_t config_total_runs(const ExperimentConfig *config);

/**
 * Parse a schedule seed range string like "0-99" or "42" or "0-9,42,100-120".
 * out_start: receives start value
 * out_end: receives end value
 * Returns 0 on success, -1 on error.
 * Note: For simplicity, only supports single range "start-end" or single value.
 */
int parse_schedule_seed_range(const char *s, uint64_t *out_start, uint64_t *out_end);

#endif /* CONFIG_H */
