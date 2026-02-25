#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>


#define CFG_MACRO_MAX_EVENTS 32

// Types of macro events (e.g. key press, delay)
typedef enum {
  MACRO_EVT_NONE = 0,
  MACRO_EVT_KEY_PRESS,
  MACRO_EVT_KEY_RELEASE,
  MACRO_EVT_DELAY_MS
} cfg_macro_event_type_t;

typedef struct {
  cfg_macro_event_type_t type;
  uint32_t value; // Keycode or delay in ms
} cfg_macro_event_t;

typedef struct {
  cfg_macro_event_t events[CFG_MACRO_MAX_EVENTS];
  size_t event_count;
} cfg_macro_t;

// Registers the macro serializer with cfgmod
void cfg_macros_register(void);
