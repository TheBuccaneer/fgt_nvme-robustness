#define _POSIX_C_SOURCE 200809L
#include "logging.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

const char* fault_mode_to_string(FaultMode fm) {
    switch (fm) {
        case FAULT_NONE:    return "NONE";
        case FAULT_TIMEOUT: return "TIMEOUT";
        case FAULT_RESET:   return "RESET";
        default:            return "UNKNOWN";
    }
}

int fault_mode_parse(const char *s, FaultMode *out) {
    if (!s || !out) return -1;
    
    char buf[32];
    size_t i;
    for (i = 0; i < sizeof(buf) - 1 && s[i]; i++) {
        buf[i] = toupper((unsigned char)s[i]);
    }
    buf[i] = '\0';
    
    if (strcmp(buf, "NONE") == 0) {
        *out = FAULT_NONE;
        return 0;
    }
    if (strcmp(buf, "TIMEOUT") == 0) {
        *out = FAULT_TIMEOUT;
        return 0;
    }
    if (strcmp(buf, "RESET") == 0) {
        *out = FAULT_RESET;
        return 0;
    }
    return -1;
}

SubmitWindow submit_window_finite(size_t n) {
    SubmitWindow sw;
    sw.is_infinite = 0;
    sw.value = n;
    return sw;
}

SubmitWindow submit_window_infinite(void) {
    SubmitWindow sw;
    sw.is_infinite = 1;
    sw.value = 0;
    return sw;
}

const char* submit_window_to_string(SubmitWindow sw, char *buf, size_t buflen) {
    if (sw.is_infinite) {
        snprintf(buf, buflen, "inf");
    } else {
        snprintf(buf, buflen, "%zu", sw.value);
    }
    return buf;
}

int submit_window_parse(const char *s, SubmitWindow *out) {
    if (!s || !out) return -1;
    
    char buf[32];
    size_t i;
    for (i = 0; i < sizeof(buf) - 1 && s[i]; i++) {
        buf[i] = tolower((unsigned char)s[i]);
    }
    buf[i] = '\0';
    
    if (strcmp(buf, "inf") == 0) {
        *out = submit_window_infinite();
        return 0;
    }
    
    char *end;
    unsigned long val = strtoul(s, &end, 10);
    if (end == s || *end != '\0') return -1;
    
    *out = submit_window_finite((size_t)val);
    return 0;
}

size_t submit_window_value(SubmitWindow sw) {
    if (sw.is_infinite) {
        return (size_t)-1;  /* SIZE_MAX */
    }
    return sw.value;
}

void logger_init(Logger *log) {
    log->file = NULL;
    log->lines = NULL;
    log->line_count = 0;
    log->line_capacity = 0;
}

void logger_free(Logger *log) {
    if (log->lines) {
        for (size_t i = 0; i < log->line_count; i++) {
            free(log->lines[i]);
        }
        free(log->lines);
    }
    log->lines = NULL;
    log->line_count = 0;
    log->line_capacity = 0;
}

static void logger_add_line(Logger *log, const char *line) {
    if (log->line_count >= log->line_capacity) {
        size_t new_cap = log->line_capacity == 0 ? 64 : log->line_capacity * 2;
        char **new_lines = realloc(log->lines, new_cap * sizeof(char*));
        if (!new_lines) return;
        log->lines = new_lines;
        log->line_capacity = new_cap;
    }
    log->lines[log->line_count++] = strdup(line);
}

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
                         const char *git_commit) {
    char buf[1024];
    char bk_str[32];
    char sw_str[32];
    
    bound_k_to_string(bound_k, bk_str, sizeof(bk_str));
    submit_window_to_string(submit_window, sw_str, sizeof(sw_str));
    
    snprintf(buf, sizeof(buf),
             "RUN_HEADER(run_id=%s, seed_id=%s, schedule_seed=%llu, policy=%s, bound_k=%s, fault_mode=%s, n_cmds=%zu, submit_window=%s, scheduler_version=%s, git_commit=%s)",
             run_id,
             seed_id,
             (unsigned long long)schedule_seed,
             policy_to_string(policy),
             bk_str,
             fault_mode_to_string(fault_mode),
             n_cmds,
             sw_str,
             scheduler_version,
             git_commit);
    
    logger_add_line(log, buf);
}

void logger_log_submit(Logger *log, uint32_t cmd_id, const char *cmd_type) {
    char buf[256];
    snprintf(buf, sizeof(buf), "SUBMIT(cmd_id=%u, cmd_type=%s)", cmd_id, cmd_type);
    logger_add_line(log, buf);
}

void logger_log_fence(Logger *log, uint32_t fence_id) {
    char buf[256];
    snprintf(buf, sizeof(buf), "FENCE(fence_id=%u)", fence_id);
    logger_add_line(log, buf);
}

void logger_log_complete(Logger *log, uint32_t cmd_id, Status status, uint32_t output) {
    char buf[256];
    snprintf(buf, sizeof(buf), "COMPLETE(cmd_id=%u, status=%s, out=%u)", 
             cmd_id, status_to_string(status), output);
    logger_add_line(log, buf);
}

void logger_log_reset(Logger *log, const char *reason, uint32_t pending_before) {
    char buf[256];
    snprintf(buf, sizeof(buf), "RESET(reason=%s, pending_before=%u)", reason, pending_before);
    logger_add_line(log, buf);
}

void logger_log_run_end(Logger *log, uint32_t pending_left, uint32_t pending_peak) {
    char buf[256];
    snprintf(buf, sizeof(buf), "RUN_END(pending_left=%u, pending_peak=%u)", pending_left, pending_peak);
    logger_add_line(log, buf);
}

int logger_write_to_file(Logger *log, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    
    for (size_t i = 0; i < log->line_count; i++) {
        fprintf(f, "%s\n", log->lines[i]);
    }
    
    fclose(f);
    return 0;
}
