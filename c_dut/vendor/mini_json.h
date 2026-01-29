/**
 * Minimal JSON parser for NVMe-lite seed files
 * 
 * Only supports the subset needed for seed JSON:
 * - Objects, arrays, strings, numbers
 * - No nested objects beyond command arrays
 */

#ifndef MINI_JSON_H
#define MINI_JSON_H

#include <stddef.h>

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;

struct JsonValue {
    JsonType type;
    union {
        int bool_val;
        double num_val;
        char *str_val;
        struct {
            JsonValue *items;
            size_t count;
        } array;
        struct {
            char **keys;
            JsonValue *values;
            size_t count;
        } object;
    } data;
};

/** Parse JSON string. Returns NULL on error. Caller must free with json_free. */
JsonValue* json_parse(const char *json_str);

/** Free JSON value tree */
void json_free(JsonValue *val);

/** Get object member by key. Returns NULL if not found. */
JsonValue* json_get(JsonValue *obj, const char *key);

/** Get string value (returns NULL if not a string) */
const char* json_string(JsonValue *val);

/** Get number value (returns 0 if not a number) */
double json_number(JsonValue *val);

/** Get array length (returns 0 if not an array) */
size_t json_array_len(JsonValue *val);

/** Get array item by index (returns NULL if out of bounds) */
JsonValue* json_array_get(JsonValue *val, size_t idx);

#endif /* MINI_JSON_H */
