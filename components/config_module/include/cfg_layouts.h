#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define CFG_LAYOUT_MAX_KEYS 128

typedef struct cfg_layout {
	uint16_t keycodes[CFG_LAYOUT_MAX_KEYS];
	size_t key_count;
} cfg_layout_t;

// Initialize the in-memory layout cache.
esp_err_t cfg_layout_init(void);

// Get or set the in-memory layout.
// Copy the current in-memory layout into out_layout.
esp_err_t cfg_layout_get(cfg_layout_t *out_layout);
// Replace the in-memory layout with the provided layout.
esp_err_t cfg_layout_set(const cfg_layout_t *layout);

// Load/store layout using cfgmod storage helpers.
// Load layout from persistent storage into memory.
esp_err_t cfg_layout_load_from_storage(void);
// Save current in-memory layout to persistent storage.
esp_err_t cfg_layout_save_to_storage(void);
