/**
 * @file cJSON.h
 * @brief Shim — minimal cJSON stubs for host testing.
 *
 * Only the functions called by cfg_layouts.c are stubbed.
 * Serialization/deserialization are not exercised in tests,
 * so these return plausible no-op values.
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>

typedef struct cJSON {
    struct cJSON *next;
    struct cJSON *child;
    int type;
    int valueint;
    char *valuestring;
    char *string;
} cJSON;

#define cJSON_Object 6
#define cJSON_Array  5
#define cJSON_Number 3

static inline cJSON *cJSON_CreateObject(void) { return NULL; }
static inline cJSON *cJSON_CreateArray(void) { return NULL; }
static inline cJSON *cJSON_CreateNumber(double num) { (void)num; return NULL; }
static inline void   cJSON_Delete(cJSON *item) { (void)item; }
static inline void   cJSON_AddItemToObject(cJSON *obj, const char *str, cJSON *item) { (void)obj; (void)str; (void)item; }
static inline void   cJSON_AddItemToArray(cJSON *arr, cJSON *item) { (void)arr; (void)item; }
static inline cJSON *cJSON_GetObjectItem(const cJSON *obj, const char *str) { (void)obj; (void)str; return NULL; }
static inline bool   cJSON_IsArray(const cJSON *item) { (void)item; return false; }
static inline bool   cJSON_IsNumber(const cJSON *item) { (void)item; return false; }

/* cJSON_ArrayForEach — never iterates since child is always NULL */
#define cJSON_ArrayForEach(element, array) \
    for (element = (array != NULL) ? (array)->child : NULL; element != NULL; element = element->next)

/* cJSON PSRAM hooks — no-op in tests */
typedef struct {
    void *(*malloc_fn)(size_t sz);
    void (*free_fn)(void *ptr);
} cJSON_Hooks;

static inline void cJSON_InitHooks(cJSON_Hooks *hooks) { (void)hooks; }
