/**
 * Minimal JSON parser implementation
 */

#include "mini_json.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

static char* parse_string_val(const char **pp) {
    const char *p = *pp;
    if (*p != '"') return NULL;
    p++;
    const char *start = p;
    while (*p && *p != '"') {
        if (*p == '\\' && *(p+1)) p += 2;
        else p++;
    }
    size_t len = p - start;
    char *s = malloc(len + 1);
    if (!s) return NULL;
    
    /* Copy with basic escape handling */
    size_t j = 0;
    for (const char *r = start; r < p; r++) {
        if (*r == '\\' && r+1 < p) {
            r++;
            switch (*r) {
                case 'n': s[j++] = '\n'; break;
                case 't': s[j++] = '\t'; break;
                case 'r': s[j++] = '\r'; break;
                case '"': s[j++] = '"'; break;
                case '\\': s[j++] = '\\'; break;
                default: s[j++] = *r; break;
            }
        } else {
            s[j++] = *r;
        }
    }
    s[j] = '\0';
    
    if (*p == '"') p++;
    *pp = p;
    return s;
}

static JsonValue* parse_value(const char **pp);

static JsonValue* parse_array(const char **pp) {
    const char *p = *pp;
    if (*p != '[') return NULL;
    p++;
    
    JsonValue *arr = calloc(1, sizeof(JsonValue));
    if (!arr) return NULL;
    arr->type = JSON_ARRAY;
    
    /* Count items first */
    size_t capacity = 8;
    arr->data.array.items = malloc(capacity * sizeof(JsonValue));
    if (!arr->data.array.items) { free(arr); return NULL; }
    arr->data.array.count = 0;
    
    p = skip_ws(p);
    while (*p && *p != ']') {
        if (arr->data.array.count >= capacity) {
            capacity *= 2;
            JsonValue *new_items = realloc(arr->data.array.items, capacity * sizeof(JsonValue));
            if (!new_items) { json_free(arr); return NULL; }
            arr->data.array.items = new_items;
        }
        
        JsonValue *item = parse_value(&p);
        if (!item) { json_free(arr); return NULL; }
        arr->data.array.items[arr->data.array.count++] = *item;
        free(item);
        
        p = skip_ws(p);
        if (*p == ',') { p++; p = skip_ws(p); }
    }
    if (*p == ']') p++;
    *pp = p;
    return arr;
}

static JsonValue* parse_object(const char **pp) {
    const char *p = *pp;
    if (*p != '{') return NULL;
    p++;
    
    JsonValue *obj = calloc(1, sizeof(JsonValue));
    if (!obj) return NULL;
    obj->type = JSON_OBJECT;
    
    size_t capacity = 8;
    obj->data.object.keys = malloc(capacity * sizeof(char*));
    obj->data.object.values = malloc(capacity * sizeof(JsonValue));
    if (!obj->data.object.keys || !obj->data.object.values) { 
        free(obj->data.object.keys);
        free(obj->data.object.values);
        free(obj); 
        return NULL; 
    }
    obj->data.object.count = 0;
    
    p = skip_ws(p);
    while (*p && *p != '}') {
        if (obj->data.object.count >= capacity) {
            capacity *= 2;
            char **new_keys = realloc(obj->data.object.keys, capacity * sizeof(char*));
            JsonValue *new_vals = realloc(obj->data.object.values, capacity * sizeof(JsonValue));
            if (!new_keys || !new_vals) { json_free(obj); return NULL; }
            obj->data.object.keys = new_keys;
            obj->data.object.values = new_vals;
        }
        
        char *key = parse_string_val(&p);
        if (!key) { json_free(obj); return NULL; }
        
        p = skip_ws(p);
        if (*p != ':') { free(key); json_free(obj); return NULL; }
        p++;
        p = skip_ws(p);
        
        JsonValue *val = parse_value(&p);
        if (!val) { free(key); json_free(obj); return NULL; }
        
        obj->data.object.keys[obj->data.object.count] = key;
        obj->data.object.values[obj->data.object.count] = *val;
        free(val);
        obj->data.object.count++;
        
        p = skip_ws(p);
        if (*p == ',') { p++; p = skip_ws(p); }
    }
    if (*p == '}') p++;
    *pp = p;
    return obj;
}

static JsonValue* parse_value(const char **pp) {
    const char *p = skip_ws(*pp);
    
    if (*p == '"') {
        char *s = parse_string_val(&p);
        if (!s) return NULL;
        JsonValue *val = calloc(1, sizeof(JsonValue));
        if (!val) { free(s); return NULL; }
        val->type = JSON_STRING;
        val->data.str_val = s;
        *pp = p;
        return val;
    }
    
    if (*p == '[') {
        JsonValue *arr = parse_array(&p);
        *pp = p;
        return arr;
    }
    
    if (*p == '{') {
        JsonValue *obj = parse_object(&p);
        *pp = p;
        return obj;
    }
    
    if (strncmp(p, "true", 4) == 0) {
        JsonValue *val = calloc(1, sizeof(JsonValue));
        if (!val) return NULL;
        val->type = JSON_BOOL;
        val->data.bool_val = 1;
        *pp = p + 4;
        return val;
    }
    
    if (strncmp(p, "false", 5) == 0) {
        JsonValue *val = calloc(1, sizeof(JsonValue));
        if (!val) return NULL;
        val->type = JSON_BOOL;
        val->data.bool_val = 0;
        *pp = p + 5;
        return val;
    }
    
    if (strncmp(p, "null", 4) == 0) {
        JsonValue *val = calloc(1, sizeof(JsonValue));
        if (!val) return NULL;
        val->type = JSON_NULL;
        *pp = p + 4;
        return val;
    }
    
    /* Try number */
    if (*p == '-' || isdigit((unsigned char)*p)) {
        char *end;
        double num = strtod(p, &end);
        if (end == p) return NULL;
        JsonValue *val = calloc(1, sizeof(JsonValue));
        if (!val) return NULL;
        val->type = JSON_NUMBER;
        val->data.num_val = num;
        *pp = end;
        return val;
    }
    
    return NULL;
}

JsonValue* json_parse(const char *json_str) {
    const char *p = json_str;
    return parse_value(&p);
}

void json_free(JsonValue *val) {
    if (!val) return;
    
    switch (val->type) {
        case JSON_STRING:
            free(val->data.str_val);
            break;
        case JSON_ARRAY:
            for (size_t i = 0; i < val->data.array.count; i++) {
                JsonValue item = val->data.array.items[i];
                /* Need to free contents but not the struct itself */
                if (item.type == JSON_STRING) free(item.data.str_val);
                else if (item.type == JSON_ARRAY) {
                    JsonValue tmp = item;
                    tmp.type = JSON_ARRAY;
                    /* Recursive free of array contents */
                    for (size_t j = 0; j < tmp.data.array.count; j++) {
                        JsonValue *inner = &tmp.data.array.items[j];
                        if (inner->type == JSON_STRING) free(inner->data.str_val);
                    }
                    free(tmp.data.array.items);
                }
                else if (item.type == JSON_OBJECT) {
                    for (size_t j = 0; j < item.data.object.count; j++) {
                        free(item.data.object.keys[j]);
                        JsonValue *v = &item.data.object.values[j];
                        if (v->type == JSON_STRING) free(v->data.str_val);
                    }
                    free(item.data.object.keys);
                    free(item.data.object.values);
                }
            }
            free(val->data.array.items);
            break;
        case JSON_OBJECT:
            for (size_t i = 0; i < val->data.object.count; i++) {
                free(val->data.object.keys[i]);
                JsonValue *v = &val->data.object.values[i];
                if (v->type == JSON_STRING) free(v->data.str_val);
                else if (v->type == JSON_ARRAY) {
                    for (size_t j = 0; j < v->data.array.count; j++) {
                        JsonValue *item = &v->data.array.items[j];
                        if (item->type == JSON_STRING) free(item->data.str_val);
                        else if (item->type == JSON_OBJECT) {
                            for (size_t k = 0; k < item->data.object.count; k++) {
                                free(item->data.object.keys[k]);
                                JsonValue *inner = &item->data.object.values[k];
                                if (inner->type == JSON_STRING) free(inner->data.str_val);
                            }
                            free(item->data.object.keys);
                            free(item->data.object.values);
                        }
                    }
                    free(v->data.array.items);
                }
            }
            free(val->data.object.keys);
            free(val->data.object.values);
            break;
        default:
            break;
    }
    free(val);
}

JsonValue* json_get(JsonValue *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    for (size_t i = 0; i < obj->data.object.count; i++) {
        if (strcmp(obj->data.object.keys[i], key) == 0) {
            return &obj->data.object.values[i];
        }
    }
    return NULL;
}

const char* json_string(JsonValue *val) {
    if (!val || val->type != JSON_STRING) return NULL;
    return val->data.str_val;
}

double json_number(JsonValue *val) {
    if (!val || val->type != JSON_NUMBER) return 0;
    return val->data.num_val;
}

size_t json_array_len(JsonValue *val) {
    if (!val || val->type != JSON_ARRAY) return 0;
    return val->data.array.count;
}

JsonValue* json_array_get(JsonValue *val, size_t idx) {
    if (!val || val->type != JSON_ARRAY) return NULL;
    if (idx >= val->data.array.count) return NULL;
    return &val->data.array.items[idx];
}
