#include "model.h"
#include <string.h>
#include <stdlib.h>

const char* status_to_string(Status s) {
    switch (s) {
        case STATUS_OK:      return "OK";
        case STATUS_ERR:     return "ERR";
        case STATUS_TIMEOUT: return "TIMEOUT";
        default:             return "UNKNOWN";
    }
}

void model_init(NvmeLiteModel *model) {
    memset(model->host_storage, 0, sizeof(model->host_storage));
    memset(model->dev_storage, 0, sizeof(model->dev_storage));
    memset(model->pending_valid, 0, sizeof(model->pending_valid));
    #if INJECT_BUG_ID == 4
    model->pending_count = (model->next_cmd_id > 0 && model->pending_valid[0]) ? 1 : 0;
#else
    model->pending_count = 0;
#endif
    model->next_cmd_id = 0;
    model->current_fence_id = 0;
    model->pending_peak = 0;
    model->had_reset = 0;
    model->commands_lost_to_reset = 0;
}

void model_submit(NvmeLiteModel *model, const Command *cmd,
                  uint32_t *out_cmd_id, int *out_is_fence, uint32_t *out_fence_id) {
    uint32_t cmd_id = model->next_cmd_id++;
    *out_cmd_id = cmd_id;
    
    /* Check if fence */
    int is_fence = (cmd->type == CMD_FENCE);
    *out_is_fence = is_fence;
    
    uint32_t fence_id = 0;
    if (is_fence) {
        fence_id = model->current_fence_id++;
        *out_fence_id = fence_id;
    }
    
    /* Store pending command */
    if (cmd_id < MAX_PENDING) {
        PendingCommand *pc = &model->pending[cmd_id];
        pc->cmd_id = cmd_id;
        pc->command = *cmd;
        pc->has_fence_id = is_fence;
        pc->fence_id = fence_id;
        model->pending_valid[cmd_id] = 1;
        model->pending_count++;
        
        /* Update peak */
        if (model->pending_count > model->pending_peak) {
            model->pending_peak = (uint32_t)model->pending_count;
        }
    }
}

/* Comparison function for qsort */
static int compare_u32(const void *a, const void *b) {
    uint32_t va = *(const uint32_t*)a;
    uint32_t vb = *(const uint32_t*)b;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

size_t model_get_pending_canonical(NvmeLiteModel *model, uint32_t *out_pending, size_t max_pending) {
    size_t count = 0;
    for (uint32_t i = 0; i < model->next_cmd_id && count < max_pending; i++) {
        if (i < MAX_PENDING && model->pending_valid[i]) {
            out_pending[count++] = i;
        }
    }
    /* Sort by cmd_id (already sorted since we iterate in order, but be explicit) */
    qsort(out_pending, count, sizeof(uint32_t), compare_u32);
    return count;
}

size_t model_pending_count(NvmeLiteModel *model) {
    return model->pending_count;
}

uint32_t model_pending_peak(NvmeLiteModel *model) {
    return model->pending_peak;
}

/**
 * Execute a command and return (status, output)
 */
static void execute_command(NvmeLiteModel *model, const Command *cmd, Status *out_status, uint32_t *out_output) {
    switch (cmd->type) {
        case CMD_WRITE: {
            size_t start = (size_t)cmd->lba;
            size_t end = start + cmd->len;
            
            if (end > STORAGE_SIZE) {
                *out_status = STATUS_ERR;
                *out_output = 0;
                return;
            }
            /* WRITE: only updates host_storage, NOT dev_storage (visibility gap) */
            for (size_t i = start; i < end; i++) {
                 model->host_storage[i] = cmd->pattern;
#if INJECT_BUG_ID == 101
                model->dev_storage[i] = cmd->pattern;  // Bug: WRITE becomes immediately visible
#endif
            }
            *out_status = STATUS_OK;
            *out_output = 0;
            break;
        }
        
        case CMD_READ: {
            size_t start = (size_t)cmd->lba;
            size_t end = start + cmd->len;
            
            if (end > STORAGE_SIZE) {
                *out_status = STATUS_ERR;
                *out_output = 0;
                return;
            }
            
            /* Compute hash of read data (same algorithm as Rust) */
            uint32_t hash = 0;
            for (size_t i = start; i < end; i++) {
                hash = hash * 31 + model->dev_storage[i];  /* wrapping mul/add */
            }
            *out_status = STATUS_OK;
            *out_output = hash;
            break;
        }

        case CMD_FENCE:
            #if INJECT_BUG_ID == 2
            *out_status = STATUS_ERR;  // Bug: fence returns ERR instead of OK
            #else
            *out_status = STATUS_OK;
            #endif
            *out_output = 0;
            break;

        case CMD_WRITE_VISIBLE: {
            size_t start = (size_t)cmd->lba;
            size_t end = start + cmd->len;

               if (end > STORAGE_SIZE) {
                    *out_status = STATUS_ERR;
                    *out_output = 0;
                    return;
                    }

                    #if INJECT_BUG_ID == 102
            size_t end2 = end;
            if (end2 > start) end2--;  // Bug: flushes len-1
            for (size_t i = start; i < end2; i++) {
                model->dev_storage[i] = model->host_storage[i];
            }
#elif INJECT_BUG_ID == 103
            for (size_t i = start; i < end; i += 2) {  // Bug: partial flush (every other word)
                model->dev_storage[i] = model->host_storage[i];
            }
#else
            for (size_t i = start; i < end; i++) {
                model->dev_storage[i] = model->host_storage[i];
            }
#endif
                        *out_status = STATUS_OK;
                        *out_output = 0;
                        break;
                        }

            
        default:
            *out_status = STATUS_ERR;
            *out_output = 0;
            break;
    }
}

int model_complete(NvmeLiteModel *model, uint32_t cmd_id, 
                   const Status *force_status, CommandResult *out_result) {
    // NACHHER:
    #if INJECT_BUG_ID == 5
    // Bug: complete cmd_id+1 instead (wenn vorhanden)
    uint32_t actual_id = (cmd_id + 1 < MAX_PENDING && model->pending_valid[cmd_id + 1]) ? cmd_id + 1 : cmd_id;
    if (actual_id >= MAX_PENDING || !model->pending_valid[actual_id]) {
        return 0;
    }
    cmd_id = actual_id;
    #else
    if (cmd_id >= MAX_PENDING || !model->pending_valid[cmd_id]) {
        return 0;
    }
    #endif
    
    PendingCommand *pc = &model->pending[cmd_id];
    
    Status status;
    uint32_t output;
    
    if (force_status) {
        status = *force_status;
        output = 0;
    } else {
        execute_command(model, &pc->command, &status, &output);
    }
    
    out_result->cmd_id = cmd_id;
    out_result->status = status;
    out_result->output = output;
    
    /* Remove from pending */
    model->pending_valid[cmd_id] = 0;
    model->pending_count--;
    
    return 1;
}

uint32_t model_reset(NvmeLiteModel *model) {
    uint32_t pending_before = (uint32_t)model->pending_count;
    #if INJECT_BUG_ID == 4
    model->commands_lost_to_reset = (pending_before > 0) ? (pending_before - 1) : 0;  // Bug: lost undercount
#else
    model->commands_lost_to_reset = pending_before;
#endif
    
    /* Clear all pending */
    for (uint32_t i = 0; i < model->next_cmd_id && i < MAX_PENDING; i++) {
        #if INJECT_BUG_ID == 4
        if (i > 0) model->pending_valid[i] = 0;  // Bug: lässt pending_valid[0] gesetzt
        #else
        model->pending_valid[i] = 0;
        #endif
    }
#if INJECT_BUG_ID == 4
    model->pending_count = (model->next_cmd_id > 0 && model->pending_valid[0]) ? 1 : 0;
#else
    model->pending_count = 0;
#endif
    model->had_reset = 1;
    
    return pending_before;
}

int model_had_reset(NvmeLiteModel *model) {
    return model->had_reset;
}

uint32_t model_commands_lost(NvmeLiteModel *model) {
    return model->commands_lost_to_reset;
}
