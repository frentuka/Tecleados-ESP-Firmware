#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "kb_matrix.h"

// One layer of the keymap (5 rows × 15 cols)
typedef struct cfg_layer {
  uint16_t keys[KB_MATRIX_ROW_COUNT][KB_MATRIX_COL_COUNT];
} cfg_layer_t;

// Register layout serializer and default
void cfg_layouts_register(void);

// Load all layers from NVS into cache (called once at startup)
esp_err_t cfg_layout_load_all(void);

// Fast action-code lookup from cached layout (with transparent fallback)
uint16_t cfg_layout_get_action_code(uint8_t row, uint8_t col, uint8_t layer);

// Per-layer get/set (set updates cache + NVS)
esp_err_t cfg_layout_get_layer(uint8_t layer, cfg_layer_t *out);
esp_err_t cfg_layout_set_layer(uint8_t layer, const cfg_layer_t *in);
