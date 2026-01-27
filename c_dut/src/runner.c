#include "runner.h"
#include "model.h"
#include <stdio.h>
#include <string.h>

void run_config_make_run_id(const RunConfig *config, char *buf, size_t buflen) {
    char bk_str[32];
    bound_k_to_string(config->bound_k, bk_str, sizeof(bk_str));
    
    snprintf(buf, buflen, "%s_%s_%s_%llu_%s",
             config->seed_id,
             policy_to_string(config->policy),
             bk_str,
             (unsigned long long)config->schedule_seed,
             fault_mode_to_string(config->fault_mode));
}

int execute_run(const Seed *seed, const RunConfig *config, 
                const char *out_log_path, RunResult *out_result) {
    NvmeLiteModel model;
    Scheduler scheduler;
    Logger logger;
    
    model_init(&model);
    scheduler_init(&scheduler, config->policy, config->bound_k, config->schedule_seed);
    logger_init(&logger);
    
    size_t n_cmds = seed->n_commands;
    char run_id[512];
    run_config_make_run_id(config, run_id, sizeof(run_id));
    
    size_t submit_window = submit_window_value(config->submit_window);
    
    /* Write header */
    logger_write_header(&logger,
                        run_id,
                        config->seed_id,
                        config->schedule_seed,
                        config->policy,
                        config->bound_k,
                        config->fault_mode,
                        n_cmds,
                        config->submit_window,
                        config->scheduler_version,
                        config->git_commit);
    
    /* Tracking */
    size_t next_cmd = 0;
    uint32_t pending_peak = 0;
    
    /* Fault injection tracking */
    size_t step_count = 0;
    size_t fault_step = (config->fault_mode != FAULT_NONE) ? n_cmds / 2 : (size_t)-1;
    int fault_injected = 0;
    int stop_submits = 0;  /* Set to 1 after TIMEOUT injection */
    
    /* BATCHED policy tracking */
    int batch_remaining = 0;
    const int batch_size = 4;
    
    /* Interleaved submit/complete loop */
    uint32_t pending_buf[MAX_PENDING];
    
    while (1) {
        size_t pending_count = model_pending_count(&model);

        #if INJECT_BUG_ID == 1
        int submit_ok = (pending_count <= submit_window) && (next_cmd < n_cmds) && !stop_submits;
        #else
        int submit_ok = (pending_count < submit_window) && (next_cmd < n_cmds) && !stop_submits;
        #endif

        int complete_ok = (pending_count > 0);
        
        if (!submit_ok && !complete_ok) {
            break;
        }
        
        /* Decide: submit or complete? */
        int do_complete;
        
        /* For BATCHED: if we're in a burst, force complete */
        if (config->policy == POLICY_BATCHED && batch_remaining > 0) {
            do_complete = 1;
        } else if (submit_ok && complete_ok) {
            /* Use RNG bit to decide */
            uint64_t bit = scheduler_next_bit(&scheduler);
            do_complete = (bit == 1);
        } else if (complete_ok) {
            do_complete = 1;
        } else {
            do_complete = 0;
        }
        
        if (do_complete) {
            /* Check fault injection first */
            if (!fault_injected && step_count >= fault_step) {
                if (config->fault_mode == FAULT_TIMEOUT) {
                    /* Get pending and timeout the first one */
                    size_t n_pending = model_get_pending_canonical(&model, pending_buf, MAX_PENDING);
                    if (n_pending > 0) {
                        uint32_t timeout_cmd_id = pending_buf[0];
                        Status timeout_status = STATUS_TIMEOUT;
                        CommandResult result;
                        if (model_complete(&model, timeout_cmd_id, &timeout_status, &result)) {
                            logger_log_complete(&logger, result.cmd_id, result.status, result.output);
                        }
                    }
                    fault_injected = 1;
                    stop_submits = 1;  /* No more SUBMITs after TIMEOUT */
                    step_count++;
                    continue;
                }
                else if (config->fault_mode == FAULT_RESET) {
                    uint32_t pending_before = model_reset(&model);
                    logger_log_reset(&logger, "INJECTED", pending_before);
                    fault_injected = 1;
                    break;
                }
            }
            
            /* Normal completion */
            size_t n_pending = model_get_pending_canonical(&model, pending_buf, MAX_PENDING);
            if (n_pending > 0) {
                /* BATCHED: start new burst if not in one */
                if (config->policy == POLICY_BATCHED && batch_remaining == 0) {
                    batch_remaining = (int)n_pending < batch_size ? (int)n_pending : batch_size;
                }
                
                Decision decision;
                if (scheduler_pick_next(&scheduler, pending_buf, n_pending, &decision)) {
                    CommandResult result;
                    if (model_complete(&model, decision.cmd_id, NULL, &result)) {
                        logger_log_complete(&logger, result.cmd_id, result.status, result.output);
                        /* Decrement batch counter for BATCHED policy */
                        if (config->policy == POLICY_BATCHED && batch_remaining > 0) {
                            batch_remaining--;
                        }
                    }
                }
            }
            step_count++;
        } else {
            /* Submit next command */
            const Command *cmd = &seed->commands[next_cmd];
            uint32_t cmd_id;
            int is_fence;
            uint32_t fence_id;
            
            model_submit(&model, cmd, &cmd_id, &is_fence, &fence_id);
            logger_log_submit(&logger, cmd_id, command_type_name(cmd->type));
            
            if (is_fence) {
                logger_log_fence(&logger, fence_id);
            }
            
            next_cmd++;
            
            uint32_t current = (uint32_t)model_pending_count(&model);
            if (current > pending_peak) {
                pending_peak = current;
            }
        }
    }
    
    /* Write run end */
    uint32_t pending_left = (uint32_t)model_pending_count(&model);
    uint32_t final_peak = pending_peak > model_pending_peak(&model) ? 
                          pending_peak : model_pending_peak(&model);
    
    logger_log_run_end(&logger, pending_left, final_peak);
    
    /* Write log to file */
    if (logger_write_to_file(&logger, out_log_path) != 0) {
        fprintf(stderr, "Error: Cannot write log to %s\n", out_log_path);
        logger_free(&logger);
        return -1;
    }
    
    /* Fill result */
    snprintf(out_result->run_id, sizeof(out_result->run_id), "%s", run_id);
    out_result->pending_left = pending_left;
    out_result->pending_peak = final_peak;
    out_result->had_reset = model_had_reset(&model);
    out_result->commands_lost = model_commands_lost(&model);
    
    logger_free(&logger);
    return 0;
}
