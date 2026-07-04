#pragma once

#include "cJSON.h"
#include "cfgmod.h"
#include "esp_err.h"

#define CFG_COMBOS_MAX_COUNT  32
#define CFG_COMBO_MAX_KEYS     8

typedef struct {
    uint8_t row;
    uint8_t col;
} cfg_combo_key_t;

typedef struct {
    uint16_t         id;
    char             name[32];
    cfg_combo_key_t  keys[CFG_COMBO_MAX_KEYS];
    uint8_t          key_count;        // How many keys in this combo (2-8)
    uint16_t         action;           // Action code to fire when combo triggers
    uint8_t          active_layers;    // Bitmask: bit 0=layer 0, bit 1=layer 1, etc.
    bool             strict_order;     // Keys must be pressed in the defined order
    bool             cancel_keys;      // Default true: release individual keys when combo fires
    bool             delayed_press;    // Default false: suppress keys during timeout window
    uint16_t         delay_ms;         // Default 50: suppression window in ms (only when delayed_press=true)
    bool             release_on_first_key; // Default true: release combo action when first key is released
} cfg_combo_t;

typedef struct {
    uint32_t active_mask;  // Bit N = 1 if combo N exists (N 0..31)
} cfg_combo_index_t;

/* cfgmod handler callbacks */
void   combos_default(void *out_struct);
bool   combos_deserialize(cJSON *root, void *out_struct);
cJSON *combos_serialize(const void *in_struct);

/* High-level helpers */
cJSON    *combos_serialize_outline(const cfg_combo_index_t *idx);
cJSON    *combos_serialize_single(uint16_t id, const cfg_combo_index_t *idx);
cJSON    *combos_serialize_limits(void);
esp_err_t combos_upsert_single(cJSON *combo_json, cfg_combo_index_t *idx);
esp_err_t combos_delete_single(uint16_t id, cfg_combo_index_t *idx);
esp_err_t combos_load_all(cfg_combo_t *out_arr, size_t *out_count);

void cfg_combos_register(cfgmod_on_update_fn update_fn);
