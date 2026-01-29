#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/**
 * Simple line-based YAML parser
 * Only handles the specific format used by main.yaml:
 * - key: value
 * - key:
 *   - item1
 *   - item2
 */

static char* read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *buf = malloc(len + 1);
    if (!buf) { fclose(f); return NULL; }
    
    size_t read_len = fread(buf, 1, len, f);
    buf[read_len] = '\0';
    fclose(f);
    return buf;
}

static char* trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

static char* strip_quotes(char *s) {
    size_t len = strlen(s);
    if (len >= 2 && s[0] == '"' && s[len-1] == '"') {
        s[len-1] = '\0';
        return s + 1;
    }
    return s;
}

int parse_schedule_seed_range(const char *s, uint64_t *out_start, uint64_t *out_end) {
    /* Try "start-end" format */
    const char *dash = strchr(s, '-');
    if (dash && dash != s) {
        char start_buf[32];
        size_t start_len = dash - s;
        if (start_len >= sizeof(start_buf)) return -1;
        strncpy(start_buf, s, start_len);
        start_buf[start_len] = '\0';
        
        char *end_ptr;
        *out_start = strtoull(start_buf, &end_ptr, 10);
        if (*end_ptr != '\0') return -1;
        
        *out_end = strtoull(dash + 1, &end_ptr, 10);
        if (*end_ptr != '\0') return -1;
        
        return 0;
    }
    
    /* Try single value */
    char *end_ptr;
    uint64_t val = strtoull(s, &end_ptr, 10);
    if (*end_ptr != '\0') return -1;
    
    *out_start = val;
    *out_end = val;
    return 0;
}

typedef enum {
    SECTION_NONE,
    SECTION_SEEDS,
    SECTION_POLICIES,
    SECTION_BOUNDS,
    SECTION_FAULTS
} YamlSection;

int config_load(const char *path, ExperimentConfig *config) {
    memset(config, 0, sizeof(*config));
    strcpy(config->scheduler_version, "v1.0");
    
    char *content = read_file(path);
    if (!content) {
        fprintf(stderr, "Error: Cannot read config file %s\n", path);
        return -1;
    }
    
    YamlSection current_section = SECTION_NONE;
    
    char *line = strtok(content, "\n");
    while (line) {
        char *trimmed = trim(line);
        
        /* Skip empty lines and comments */
        if (*trimmed == '\0' || *trimmed == '#') {
            line = strtok(NULL, "\n");
            continue;
        }
        
        /* Check for list item */
        if (trimmed[0] == '-' && trimmed[1] == ' ') {
            char *value = trim(trimmed + 2);
            value = strip_quotes(value);
            
            switch (current_section) {
                case SECTION_SEEDS:
                    if (config->n_seeds < MAX_SEEDS) {
                        config->seeds[config->n_seeds++] = strdup(value);
                    }
                    break;
                    
                case SECTION_POLICIES:
                    if (config->n_policies < MAX_POLICIES) {
                        Policy p;
                        if (policy_parse(value, &p) == 0) {
                            config->policies[config->n_policies++] = p;
                        }
                    }
                    break;
                    
                case SECTION_BOUNDS:
                    if (config->n_bounds < MAX_BOUNDS) {
                        BoundK bk;
                        if (bound_k_parse(value, &bk) == 0) {
                            config->bounds[config->n_bounds++] = bk;
                        }
                    }
                    break;
                    
                case SECTION_FAULTS:
                    if (config->n_faults < MAX_FAULTS) {
                        FaultMode fm;
                        if (fault_mode_parse(value, &fm) == 0) {
                            config->faults[config->n_faults++] = fm;
                        }
                    }
                    break;
                    
                default:
                    break;
            }
        }
        /* Check for key: value or key: (section start) */
        else {
            char *colon = strchr(trimmed, ':');
            if (colon) {
                *colon = '\0';
                char *key = trim(trimmed);
                char *value = trim(colon + 1);
                
                if (*value == '\0') {
                    /* Section start */
                    if (strcmp(key, "seeds") == 0) {
                        current_section = SECTION_SEEDS;
                    } else if (strcmp(key, "policies") == 0) {
                        current_section = SECTION_POLICIES;
                    } else if (strcmp(key, "bounds") == 0) {
                        current_section = SECTION_BOUNDS;
                    } else if (strcmp(key, "faults") == 0) {
                        current_section = SECTION_FAULTS;
                    } else {
                        current_section = SECTION_NONE;
                    }
                } else {
                    /* Key: value pair */
                    current_section = SECTION_NONE;
                    value = strip_quotes(value);
                    
                    if (strcmp(key, "schedule_seeds") == 0) {
                        parse_schedule_seed_range(value, 
                            &config->schedule_seed_start, 
                            &config->schedule_seed_end);
                    } else if (strcmp(key, "scheduler_version") == 0) {
                        strncpy(config->scheduler_version, value, sizeof(config->scheduler_version) - 1);
                    } else if (strcmp(key, "git_commit") == 0) {
                        if (strcmp(value, "auto") == 0) {
                            /* Try to get git commit */
                            FILE *fp = popen("git rev-parse HEAD 2>/dev/null", "r");
                            if (fp) {
                                if (fgets(config->git_commit, sizeof(config->git_commit), fp)) {
                                    /* Remove trailing newline */
                                    char *nl = strchr(config->git_commit, '\n');
                                    if (nl) *nl = '\0';
                                }
                                pclose(fp);
                            }
                        } else {
                            strncpy(config->git_commit, value, sizeof(config->git_commit) - 1);
                        }
                    }
                }
            }
        }
        
        line = strtok(NULL, "\n");
    }
    
    free(content);
    return 0;
}

void config_free(ExperimentConfig *config) {
    for (size_t i = 0; i < config->n_seeds; i++) {
        free(config->seeds[i]);
    }
    memset(config, 0, sizeof(*config));
}

size_t config_total_runs(const ExperimentConfig *config) {
    size_t n_schedule_seeds = config->schedule_seed_end - config->schedule_seed_start + 1;
    return config->n_seeds * config->n_policies * config->n_bounds * 
           config->n_faults * n_schedule_seeds;
}
