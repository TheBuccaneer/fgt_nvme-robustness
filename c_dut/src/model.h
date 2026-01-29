#ifndef MODEL_H
#define MODEL_H

#include "seed.h"
#include <stdint.h>
#include <stddef.h>

/** Storage size in u32 words */
#define STORAGE_SIZE 1024

/** Maximum number of pending commands */
#define MAX_PENDING 4096

/**
 * Terminal status of a command
 */
typedef enum {
    STATUS_OK,
    STATUS_ERR,
    STATUS_TIMEOUT
} Status;

/**
 * A pending command waiting for completion
 */
typedef struct {
    uint32_t cmd_id;
    Command command;
    int has_fence_id;
    uint32_t fence_id;
} PendingCommand;

/**
 * Result of executing a command
 */
typedef struct {
    uint32_t cmd_id;
    Status status;
    uint32_t output;
} CommandResult;

/**
 * The NVMe-lite model state
 */
typedef struct {
    /* Dual storage for visibility model */
    uint32_t host_storage[STORAGE_SIZE];  /* Host-written values */
    uint32_t dev_storage[STORAGE_SIZE];   /* Device-visible values */
    
    /* All pending commands (indexed by cmd_id for fast lookup) */
    PendingCommand pending[MAX_PENDING];
    int pending_valid[MAX_PENDING];  /* 1 if slot is valid */
    size_t pending_count;
    
    /* Next command ID to assign */
    uint32_t next_cmd_id;
    
    /* Current fence ID */
    uint32_t current_fence_id;
    
    /* Peak number of pending commands */
    uint32_t pending_peak;
    
    /* Reset tracking */
    int had_reset;
    uint32_t commands_lost_to_reset;
} NvmeLiteModel;

/** Get status as string */
const char* status_to_string(Status s);

/** Initialize model */
void model_init(NvmeLiteModel *model);

/**
 * Submit a command to the model.
 * out_cmd_id: receives assigned cmd_id
 * out_is_fence: receives 1 if this is a fence command
 * out_fence_id: receives fence_id if this is a fence
 */
void model_submit(NvmeLiteModel *model, const Command *cmd,
                  uint32_t *out_cmd_id, int *out_is_fence, uint32_t *out_fence_id);

/**
 * Get pending command IDs in canonical order (sorted by cmd_id).
 * out_pending: buffer to receive cmd_ids
 * max_pending: size of buffer
 * Returns number of pending commands.
 */
size_t model_get_pending_canonical(NvmeLiteModel *model, uint32_t *out_pending, size_t max_pending);

/** Get current pending count */
size_t model_pending_count(NvmeLiteModel *model);

/** Get peak pending */
uint32_t model_pending_peak(NvmeLiteModel *model);

/**
 * Complete a command by cmd_id.
 * force_status: if not NULL, use this status instead of executing
 * out_result: receives the result
 * Returns 1 if command was completed, 0 if not found.
 */
int model_complete(NvmeLiteModel *model, uint32_t cmd_id, 
                   const Status *force_status, CommandResult *out_result);

/**
 * Perform a reset - clears all pending commands.
 * Returns number of commands that were pending.
 */
uint32_t model_reset(NvmeLiteModel *model);

/** Check if reset occurred */
int model_had_reset(NvmeLiteModel *model);

/** Get commands lost to reset */
uint32_t model_commands_lost(NvmeLiteModel *model);

#endif /* MODEL_H */
