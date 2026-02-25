#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define CFG_LAYOUT_MAX_KEYS 128

typedef struct cfg_layout {
  uint16_t keycodes[CFG_LAYOUT_MAX_KEYS];
  size_t key_count;
} cfg_layout_t;

// Register layout serializer and default
void cfg_layouts_register(void);

// Get or set the in-memory layout using cfgmod
esp_err_t cfg_layout_get(cfg_layout_t *out_layout);
esp_err_t cfg_layout_set(const cfg_layout_t *layout);
