#pragma once

#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"
#include "kb_matrix.h"

// One layer of the keymap (6 rows × 18 cols)
typedef struct cfg_layer {
  uint16_t keys[KB_MATRIX_ROW_COUNT][KB_MATRIX_COL_COUNT];
} cfg_layer_t;

#define CFG_LAYOUT_MAX_COUNT    16
#define CFG_LAYOUT_NAME_LEN     24

// Persistent index: tracks which layout slots are populated + their names
typedef struct __attribute__((packed)) {
    uint16_t active_mask;
    char     names[CFG_LAYOUT_MAX_COUNT][CFG_LAYOUT_NAME_LEN];
} cfg_layout_index_t;

// ── Registration & Init ──
void        cfg_layouts_register(void);
esp_err_t   cfg_layout_load_all(void);

// ── Hot-path lookup (called from kb_manager scan loop) ──
uint16_t    cfg_layout_get_action_code(uint8_t row, uint8_t col, uint8_t layer);

// ── Per-layer CRUD ──
esp_err_t   cfg_layout_get_layer(uint8_t layer, cfg_layer_t *out);
esp_err_t   cfg_layout_set_layer(uint8_t layer, const cfg_layer_t *in);

// ── Dynamic management ──
esp_err_t   cfg_layout_create(const char *name, uint8_t *out_id);
esp_err_t   cfg_layout_delete(uint8_t id);
esp_err_t   cfg_layout_rename(uint8_t id, const char *new_name);

// ── Index accessors ──
uint8_t     cfg_layout_get_count(void);                  // Number of active layouts
bool        cfg_layout_exists(uint8_t id);               // Check if slot is populated
const cfg_layout_index_t *cfg_layout_get_index(void);    // Read-only pointer to in-memory index

// ── Serialization ──
cJSON *layouts_serialize_outline(void);
cJSON *layouts_serialize_single(uint8_t id);
cJSON *layouts_serialize_limits(void);
