#pragma once

#include "cJSON.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#define CFG_MACRO_MAX_EVENTS 128
#define CFG_MACROS_MAX_COUNT 16

// Types of macro events (e.g. key press, delay)
typedef enum {
  MACRO_EVT_NONE = 0,
  MACRO_EVT_KEY_PRESS,
  MACRO_EVT_KEY_RELEASE,
  MACRO_EVT_DELAY_MS,
  MACRO_EVT_KEY_TAP
} cfg_macro_event_type_t;

// Macro execution modes
typedef enum {
  MACRO_EXEC_ONCE_STACK_ONCE = 0,   // Default: queue at most 1 extra execution
  MACRO_EXEC_ONCE_NO_STACK,         // Ignore presses while running
  MACRO_EXEC_ONCE_STACK_N,          // Queue up to stack_max extra executions
  MACRO_EXEC_HOLD_REPEAT,           // Repeat while held, finish current on release
  MACRO_EXEC_HOLD_REPEAT_CANCEL,    // Repeat while held, abort on release
  MACRO_EXEC_TOGGLE_REPEAT,         // Toggle repeat, finish current on stop
  MACRO_EXEC_TOGGLE_REPEAT_CANCEL,  // Toggle repeat, abort on stop
  MACRO_EXEC_BURST_N,               // Single press fires repeat_count times
  MACRO_EXEC_MODE_COUNT             // Sentinel
} cfg_macro_exec_mode_t;

typedef struct {
  cfg_macro_event_type_t type;
  uint32_t value; // Keycode or delay in ms
} cfg_macro_event_t;

typedef struct {
  uint16_t id;
  char name[32];
  cfg_macro_event_t events[CFG_MACRO_MAX_EVENTS];
  size_t event_count;
  uint8_t exec_mode;     // cfg_macro_exec_mode_t value
  uint8_t stack_max;     // For MACRO_EXEC_ONCE_STACK_N (default 1)
  uint8_t repeat_count;  // For MACRO_EXEC_BURST_N (default 1)
} cfg_macro_t;

typedef struct {
  cfg_macro_t macros[CFG_MACROS_MAX_COUNT];
  size_t count;
} cfg_macro_list_t;

// Registers the macro serializer with cfgmod
void cfg_macros_register(void);

// Handler functions for external use (e.g. by kb_macro.c re-registration)
void macros_default(void *out_struct);
bool macros_deserialize(cJSON *root, void *out_struct);
cJSON *macros_serialize(const void *in_struct);

// Serialize an outline of all macros (IDs and Names only)
cJSON *macros_serialize_outline(const cfg_macro_list_t *list);

// Serialize a specific macro by its ID
cJSON *macros_serialize_single(uint16_t id, const cfg_macro_list_t *list);

// Serialize compile-time limits as JSON: { "maxEvents": N, "maxMacros": M }
cJSON *macros_serialize_limits(void);

// Insert or replace a single macro (by ID) in the list
void macros_upsert_single(cJSON *macro_json, cfg_macro_list_t *list);

// Remove a macro by ID from the list
void macros_delete_single(uint16_t id, cfg_macro_list_t *list);
