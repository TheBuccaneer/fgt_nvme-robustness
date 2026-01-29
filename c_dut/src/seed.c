#include "seed.h"
#include "../vendor/mini_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* command_type_name(CommandType type) {
    switch (type) {
        case CMD_WRITE: return "WRITE";
        case CMD_READ:  return "READ";
        case CMD_FENCE: return "FENCE";
        case CMD_WRITE_VISIBLE: return "WRITE_VISIBLE";
        default:        return "UNKNOWN";
    }
}

static char* read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *buf = malloc(len + 1);
    if (!buf) { fclose(f); return NULL; }
    
    size_t read = fread(buf, 1, len, f);
    buf[read] = '\0';
    fclose(f);
    return buf;
}

int seed_load(const char *path, Seed *seed) {
    char *content = read_file(path);
    if (!content) {
        fprintf(stderr, "Error: Cannot read file %s\n", path);
        return -1;
    }
    
    JsonValue *root = json_parse(content);
    free(content);
    
    if (!root) {
        fprintf(stderr, "Error: Failed to parse JSON in %s\n", path);
        return -1;
    }
    
    /* Get seed_id */
    JsonValue *id_val = json_get(root, "seed_id");
    const char *seed_id = json_string(id_val);
    if (!seed_id) {
        fprintf(stderr, "Error: Missing seed_id in %s\n", path);
        json_free(root);
        return -1;
    }
    strncpy(seed->seed_id, seed_id, sizeof(seed->seed_id) - 1);
    seed->seed_id[sizeof(seed->seed_id) - 1] = '\0';
    
    /* Get commands array */
    JsonValue *cmds_val = json_get(root, "commands");
    if (!cmds_val || cmds_val->type != JSON_ARRAY) {
        fprintf(stderr, "Error: Missing commands array in %s\n", path);
        json_free(root);
        return -1;
    }
    
    size_t n_cmds = json_array_len(cmds_val);
    seed->commands = malloc(n_cmds * sizeof(Command));
    if (!seed->commands) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        json_free(root);
        return -1;
    }
    seed->n_commands = n_cmds;
    
    for (size_t i = 0; i < n_cmds; i++) {
        JsonValue *cmd_obj = json_array_get(cmds_val, i);
        if (!cmd_obj) {
            fprintf(stderr, "Error: Invalid command at index %zu\n", i);
            free(seed->commands);
            json_free(root);
            return -1;
        }
        
        JsonValue *type_val = json_get(cmd_obj, "type");
        const char *type_str = json_string(type_val);
        if (!type_str) {
            fprintf(stderr, "Error: Missing type in command %zu\n", i);
            free(seed->commands);
            json_free(root);
            return -1;
        }
        
        Command *cmd = &seed->commands[i];
        memset(cmd, 0, sizeof(Command));
        
        if (strcmp(type_str, "WRITE") == 0) {
            cmd->type = CMD_WRITE;
            JsonValue *lba_v = json_get(cmd_obj, "lba");
            JsonValue *len_v = json_get(cmd_obj, "len");
            JsonValue *pat_v = json_get(cmd_obj, "pattern");
            cmd->lba = lba_v ? (uint64_t)json_number(lba_v) : 0;
            cmd->len = len_v ? (uint32_t)json_number(len_v) : 0;
            cmd->pattern = pat_v ? (uint32_t)json_number(pat_v) : 0;
        }
        else if (strcmp(type_str, "READ") == 0) {
            cmd->type = CMD_READ;
            JsonValue *lba_v = json_get(cmd_obj, "lba");
            JsonValue *len_v = json_get(cmd_obj, "len");
            cmd->lba = lba_v ? (uint64_t)json_number(lba_v) : 0;
            cmd->len = len_v ? (uint32_t)json_number(len_v) : 0;
        }
        else if (strcmp(type_str, "FENCE") == 0) {
            cmd->type = CMD_FENCE;
        }
        else if (strcmp(type_str, "WRITE_VISIBLE") == 0) {
            cmd->type = CMD_WRITE_VISIBLE;
            JsonValue *lba_v = json_get(cmd_obj, "lba");
            JsonValue *len_v = json_get(cmd_obj, "len");
            cmd->lba = lba_v ? (uint64_t)json_number(lba_v) : 0;
            cmd->len = len_v ? (uint32_t)json_number(len_v) : 0;
            }
        else {
            fprintf(stderr, "Error: Unknown command type '%s'\n", type_str);
            free(seed->commands);
            json_free(root);
            return -1;
        }
    }
    
    json_free(root);
    return 0;
}

void seed_free(Seed *seed) {
    if (seed->commands) {
        free(seed->commands);
        seed->commands = NULL;
    }
    seed->n_commands = 0;
}
