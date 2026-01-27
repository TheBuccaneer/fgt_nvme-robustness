#ifndef LOGGING_H
#define LOGGING_H

#include "model.h"
#include "scheduler.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/**
 * Fault mode for a run
 */
typedef enum {
    FAULT_NONE,
    FAULT_TIMEOUT,
    FAULT_RESET
} FaultMode;

/**
 * Submit window - controls max pending commands
 */
typedef struct {
    int is_infinite;
    size_t value;
} SubmitWindow;

/** Fault mode string conversion */
const char* fault_mode_to_string(FaultMode fm);
int fault_mode_parse(const char *s, FaultMode *out);

/** Submit window helpers */
SubmitWindow submit_window_finite(size_t n);
SubmitWindow submit_window_infinite(void);
const char* submit_window_to_string(SubmitWindow sw, char *buf, size_t buflen);
int submit_window_parse(const char *s, SubmitWindow *out);
size_t submit_window_value(SubmitWindow sw);

/**
 * Logger state - writes to file
 */
typedef struct {
    FILE *file;
    char **lines;
    size_t line_count;
    size_t line_capacity;
} Logger;

/** Initialize logger */
void logger_init(Logger *log);

/** Free logger resources */
void logger_free(Logger *log);

/** Write the run header with submit_window */
void logger_write_header(Logger *log,
                         const char *run_id,
                         const char *seed_id,
                         uint64_t schedule_seed,
                         Policy policy,
                         BoundK bound_k,
                         FaultMode fault_mode,
                         size_t n_cmds,
                         SubmitWindow submit_window,
                         const char *scheduler_version,
                         const char *git_commit);

/** Log SUBMIT event */
void logger_log_submit(Logger *log, uint32_t cmd_id, const char *cmd_type);

/** Log FENCE event */
void logger_log_fence(Logger *log, uint32_t fence_id);

/** Log COMPLETE event */
void logger_log_complete(Logger *log, uint32_t cmd_id, Status status, uint32_t output);

/** Log RESET event */
void logger_log_reset(Logger *log, const char *reason, uint32_t pending_before);

/** Log RUN_END event */
void logger_log_run_end(Logger *log, uint32_t pending_left, uint32_t pending_peak);

/** Write log to file */
int logger_write_to_file(Logger *log, const char *path);

#endif /* LOGGING_H */
