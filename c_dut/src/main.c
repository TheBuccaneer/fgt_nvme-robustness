/**
 * NVMe-lite DUT (Device Under Test) - C Implementation
 * 
 * Usage:
 *   nvme-lite-dut run-one --seed-file seeds/seed_001.json --schedule-seed 42 ...
 *   nvme-lite-dut run-matrix --config configs/main.yaml --out-dir out/logs
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "seed.h"
#include "config.h"
#include "runner.h"
#include "logging.h"
#include "scheduler.h"

/* Simple recursive mkdir */
static int mkdir_p(const char *path) {
    char tmp[512];
    char *p = NULL;
    size_t len;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

/* Get directory part of path */
static void get_parent_dir(const char *path, char *dir, size_t dir_size) {
    strncpy(dir, path, dir_size - 1);
    dir[dir_size - 1] = '\0';
    
    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
    } else {
        dir[0] = '\0';
    }
}

static void print_usage(const char *prog) {
    printf("NVMe-lite DUT (Device Under Test) - C Implementation\n\n");
    printf("Usage:\n");
    printf("  %s run-one [options]\n", prog);
    printf("  %s run-matrix [options]\n\n", prog);
    
    printf("run-one options:\n");
    printf("  --seed-file <path>        JSON seed file\n");
    printf("  --schedule-seed <N>       RNG seed for scheduling\n");
    printf("  --policy <POLICY>         FIFO | RANDOM | ADVERSARIAL | BATCHED\n");
    printf("  --bound-k <K>             0, 1, 2, ... or \"inf\"\n");
    printf("  --fault-mode <MODE>       NONE | TIMEOUT | RESET (default: NONE)\n");
    printf("  --submit-window <N|inf>   Max pending commands (default: inf)\n");
    printf("  --out-log <path>          Output log file\n");
    printf("  --scheduler-version <V>   Version string (default: v1.0)\n");
    printf("  --git-commit <hash>       Git commit (default: empty)\n\n");
    
    printf("run-matrix options:\n");
    printf("  --config <path>           YAML config file\n");
    printf("  --out-dir <path>          Output directory for logs\n");
    printf("  --schedule-seeds <range>  e.g. \"0-99\" or \"42\" (override config)\n");
    printf("  --submit-window <N|inf>   Max pending commands (default: inf)\n");
}

/* Find argument value */
static const char* get_arg(int argc, char **argv, const char *name) {
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], name) == 0) {
            return argv[i + 1];
        }
    }
    return NULL;
}

/* Check if argument exists */
static int has_arg(int argc, char **argv, const char *name) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int cmd_run_one(int argc, char **argv) {
    /* Parse arguments */
    const char *seed_file = get_arg(argc, argv, "--seed-file");
    const char *schedule_seed_str = get_arg(argc, argv, "--schedule-seed");
    const char *policy_str = get_arg(argc, argv, "--policy");
    const char *bound_k_str = get_arg(argc, argv, "--bound-k");
    const char *fault_mode_str = get_arg(argc, argv, "--fault-mode");
    const char *submit_window_str = get_arg(argc, argv, "--submit-window");
    const char *out_log = get_arg(argc, argv, "--out-log");
    const char *scheduler_version = get_arg(argc, argv, "--scheduler-version");
    const char *git_commit = get_arg(argc, argv, "--git-commit");
    
    /* Check required args */
    if (!seed_file || !schedule_seed_str || !policy_str || !bound_k_str || !out_log) {
        fprintf(stderr, "Error: Missing required arguments\n");
        fprintf(stderr, "Required: --seed-file, --schedule-seed, --policy, --bound-k, --out-log\n");
        return 1;
    }
    
    /* Parse values */
    uint64_t schedule_seed = strtoull(schedule_seed_str, NULL, 10);
    
    Policy policy;
    if (policy_parse(policy_str, &policy) != 0) {
        fprintf(stderr, "Error: Invalid policy '%s'\n", policy_str);
        return 1;
    }
    
    BoundK bound_k;
    if (bound_k_parse(bound_k_str, &bound_k) != 0) {
        fprintf(stderr, "Error: Invalid bound_k '%s'\n", bound_k_str);
        return 1;
    }
    
    FaultMode fault_mode = FAULT_NONE;
    if (fault_mode_str) {
        if (fault_mode_parse(fault_mode_str, &fault_mode) != 0) {
            fprintf(stderr, "Error: Invalid fault_mode '%s'\n", fault_mode_str);
            return 1;
        }
    }
    
    SubmitWindow submit_window = submit_window_infinite();
    if (submit_window_str) {
        if (submit_window_parse(submit_window_str, &submit_window) != 0) {
            fprintf(stderr, "Error: Invalid submit_window '%s'\n", submit_window_str);
            return 1;
        }
    }
    
    if (!scheduler_version) scheduler_version = "v1.0";
    if (!git_commit) git_commit = "";
    
    /* Load seed */
    Seed seed;
    if (seed_load(seed_file, &seed) != 0) {
        fprintf(stderr, "Error: Cannot load seed from '%s'\n", seed_file);
        return 1;
    }
    
    /* Create output directory if needed */
    char parent_dir[512];
    get_parent_dir(out_log, parent_dir, sizeof(parent_dir));
    if (parent_dir[0] != '\0') {
        mkdir_p(parent_dir);
    }
    
    /* Configure run */
    RunConfig config = {
        .seed_id = seed.seed_id,
        .schedule_seed = schedule_seed,
        .policy = policy,
        .bound_k = bound_k,
        .fault_mode = fault_mode,
        .submit_window = submit_window,
        .scheduler_version = scheduler_version,
        .git_commit = git_commit
    };
    
    /* Execute run */
    RunResult result;
    if (execute_run(&seed, &config, out_log, &result) != 0) {
        fprintf(stderr, "Error: Run failed\n");
        seed_free(&seed);
        return 1;
    }
    
    printf("Run completed: %s\n", result.run_id);
    printf("  pending_left: %u\n", result.pending_left);
    printf("  pending_peak: %u\n", result.pending_peak);
    if (result.had_reset) {
        printf("  commands_lost: %u\n", result.commands_lost);
    }
    
    seed_free(&seed);
    return 0;
}

static int cmd_run_matrix(int argc, char **argv) {
    /* Parse arguments */
    const char *config_path = get_arg(argc, argv, "--config");
    const char *out_dir = get_arg(argc, argv, "--out-dir");
    const char *schedule_seeds_override = get_arg(argc, argv, "--schedule-seeds");
    const char *submit_window_str = get_arg(argc, argv, "--submit-window");
    
    /* Check required args */
    if (!config_path || !out_dir) {
        fprintf(stderr, "Error: Missing required arguments\n");
        fprintf(stderr, "Required: --config, --out-dir\n");
        return 1;
    }
    
    /* Load config */
    ExperimentConfig exp_config;
    if (config_load(config_path, &exp_config) != 0) {
        fprintf(stderr, "Error: Cannot load config from '%s'\n", config_path);
        return 1;
    }
    
    /* Parse submit_window */
    SubmitWindow submit_window = submit_window_infinite();
    if (submit_window_str) {
        if (submit_window_parse(submit_window_str, &submit_window) != 0) {
            fprintf(stderr, "Error: Invalid submit_window '%s'\n", submit_window_str);
            config_free(&exp_config);
            return 1;
        }
    }
    
    /* Override schedule seeds if provided */
    if (schedule_seeds_override) {
        if (parse_schedule_seed_range(schedule_seeds_override, 
                                       &exp_config.schedule_seed_start,
                                       &exp_config.schedule_seed_end) != 0) {
            fprintf(stderr, "Error: Invalid schedule seeds range '%s'\n", schedule_seeds_override);
            config_free(&exp_config);
            return 1;
        }
    }
    
    /* Create output directory */
    mkdir_p(out_dir);
    
    size_t total = config_total_runs(&exp_config);
    char sw_str[32];
    submit_window_to_string(submit_window, sw_str, sizeof(sw_str));
    
    printf("Running %zu experiments...\n", total);
    printf("  Seeds: %zu\n", exp_config.n_seeds);
    printf("  Policies: %zu\n", exp_config.n_policies);
    printf("  Bounds: %zu\n", exp_config.n_bounds);
    printf("  Faults: %zu\n", exp_config.n_faults);
    printf("  Schedule seeds: %llu-%llu\n", 
           (unsigned long long)exp_config.schedule_seed_start,
           (unsigned long long)exp_config.schedule_seed_end);
    printf("  Submit window: %s\n", sw_str);
    
    size_t completed = 0;
    size_t errors = 0;
    
    /* Iterate through all combinations */
    for (size_t si = 0; si < exp_config.n_seeds; si++) {
        Seed seed;
        if (seed_load(exp_config.seeds[si], &seed) != 0) {
            fprintf(stderr, "Error loading seed %s\n", exp_config.seeds[si]);
            errors++;
            continue;
        }
        
        for (size_t pi = 0; pi < exp_config.n_policies; pi++) {
            for (size_t bi = 0; bi < exp_config.n_bounds; bi++) {
                for (size_t fi = 0; fi < exp_config.n_faults; fi++) {
                    for (uint64_t sched_seed = exp_config.schedule_seed_start;
                         sched_seed <= exp_config.schedule_seed_end;
                         sched_seed++) {
                        
                        RunConfig run_config = {
                            .seed_id = seed.seed_id,
                            .schedule_seed = sched_seed,
                            .policy = exp_config.policies[pi],
                            .bound_k = exp_config.bounds[bi],
                            .fault_mode = exp_config.faults[fi],
                            .submit_window = submit_window,
                            .scheduler_version = exp_config.scheduler_version,
                            .git_commit = exp_config.git_commit
                        };
                        
                        char run_id[512];
                        run_config_make_run_id(&run_config, run_id, sizeof(run_id));
                        
                        char log_path[1024];
                        snprintf(log_path, sizeof(log_path), "%s/%s.log", out_dir, run_id);
                        
                        RunResult result;
                        if (execute_run(&seed, &run_config, log_path, &result) == 0) {
                            completed++;
                            if (completed % 100 == 0) {
                                printf("Progress: %zu/%zu\n", completed, total);
                            }
                        } else {
                            fprintf(stderr, "Error in run %s\n", run_id);
                            errors++;
                        }
                    }
                }
            }
        }
        
        seed_free(&seed);
    }
    
    printf("\nCompleted: %zu/%zu\n", completed, total);
    if (errors > 0) {
        printf("Errors: %zu\n", errors);
    }
    
    config_free(&exp_config);
    return (errors > 0) ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    if (has_arg(argc, argv, "--help") || has_arg(argc, argv, "-h")) {
        print_usage(argv[0]);
        return 0;
    }
    
    const char *cmd = argv[1];
    
    if (strcmp(cmd, "run-one") == 0) {
        return cmd_run_one(argc, argv);
    }
    else if (strcmp(cmd, "run-matrix") == 0) {
        return cmd_run_matrix(argc, argv);
    }
    else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
}
