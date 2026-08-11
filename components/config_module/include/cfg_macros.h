#pragma once


#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#define CFG_MACRO_MAX_EVENTS 256
#define CFG_MACROS_MAX_COUNT 64

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
  uint32_t value; // Keycode or delay in ms
  uint32_t delay_ms; // Optional inline sleep after this event
  uint32_t press_duration_ms; // Duration for MACRO_EVT_KEY_TAP
  uint32_t type; // cfg_macro_event_type_t
} cfg_macro_event_t;

typedef struct {
  uint16_t id;
  uint16_t event_count;
  uint8_t  exec_mode;     // cfg_macro_exec_mode_t value
  uint8_t  stack_max;     // For MACRO_EXEC_ONCE_STACK_N (default 1)
  uint8_t  repeat_count;  // For MACRO_EXEC_BURST_N (default 1)
  uint8_t  reserved[1];
  char     name[32];
  cfg_macro_event_t events[CFG_MACRO_MAX_EVENTS];
} cfg_macro_t;

_Static_assert(sizeof(cfg_macro_event_t) == 16, "cfg_macro_event_t size mismatch");
_Static_assert(sizeof(cfg_macro_t) == 4136, "cfg_macro_t size mismatch");
_Static_assert(offsetof(cfg_macro_t, events) == 40, "offset mismatch");

typedef struct {
  cfg_macro_t macros[CFG_MACROS_MAX_COUNT];
  size_t count;
} cfg_macro_list_t;

// Registers the macro serializer with cfgmod
void cfg_macros_register(void);

// Handler functions for external use (e.g. by kb_macro.c re-registration)
void macros_default(void *out_struct);

typedef struct {
  uint64_t active_mask; // Bit N is 1 if macro N exists (N 0..63)
} cfg_macro_index_t;
// Remove a macro by ID directly from NVS and index
esp_err_t macros_delete_single(uint16_t id, cfg_macro_index_t *idx);
// Full wrapper for deleting a macro (used by cfgmod)
esp_err_t macros_delete(uint16_t id);
// Full wrapper for saving a macro (used by cfgmod)
esp_err_t macros_set(const void *in_struct);
// Load all active macros from NVS into a list
esp_err_t macros_load_all(cfg_macro_list_t *out_list);
