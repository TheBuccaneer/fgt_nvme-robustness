#ifndef RUNNER_H
#define RUNNER_H

#include "seed.h"
#include "scheduler.h"
#include "logging.h"
#include <stdint.h>

/**
 * Configuration for a single run
 */
typedef struct {
    const char *seed_id;
    uint64_t schedule_seed;
    Policy policy;
    BoundK bound_k;
    FaultMode fault_mode;
    SubmitWindow submit_window;
    const char *scheduler_version;
    const char *git_commit;
} RunConfig;

/**
 * Result of a run
 */
typedef struct {
    char run_id[512];
    uint32_t pending_left;
    uint32_t pending_peak;
    int had_reset;
    uint32_t commands_lost;
} RunResult;

/**
 * Generate run_id from config.
 */
void run_config_make_run_id(const RunConfig *config, char *buf, size_t buflen);

/**
 * Execute a single run.
 * seed: loaded seed
 * config: run configuration
 * out_log_path: path for output log file
 * out_result: receives run result
 * Returns 0 on success, -1 on error.
 */
int execute_run(const Seed *seed, const RunConfig *config, 
                const char *out_log_path, RunResult *out_result);

#endif /* RUNNER_H */
