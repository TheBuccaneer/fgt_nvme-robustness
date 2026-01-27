#ifndef SEED_H
#define SEED_H

#include <stdint.h>
#include <stddef.h>

/**
 * Command types for NVMe-lite model
 */
typedef enum {
    CMD_WRITE,
    CMD_READ,
    CMD_FENCE,
    CMD_WRITE_VISIBLE
} CommandType;

/**
 * A single command in the workload
 */
typedef struct {
    CommandType type;
    uint64_t lba;       /* Logical block address (for WRITE/READ) */
    uint32_t len;       /* Length in words (for WRITE/READ) */
    uint32_t pattern;   /* Write pattern (for WRITE only) */
} Command;

/**
 * A seed containing commands to execute
 */
typedef struct {
    char seed_id[256];
    Command *commands;
    size_t n_commands;
} Seed;

/** Get command type as string */
const char* command_type_name(CommandType type);

/** Load seed from JSON file. Returns 0 on success, -1 on error. */
int seed_load(const char *path, Seed *seed);

/** Free seed resources */
void seed_free(Seed *seed);

#endif /* SEED_H */
