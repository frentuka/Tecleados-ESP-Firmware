#pragma once


#include "cfgmod.h"
#include "esp_err.h"

#define CFG_COMBOS_MAX_COUNT  32
#define CFG_COMBO_MAX_KEYS     8

typedef struct {
    uint8_t row;
    uint8_t col;
} cfg_combo_key_t;

typedef struct {
    uint16_t         action;
    uint16_t         delay_ms;
    uint16_t         id;
    uint8_t          key_count;
    uint8_t          active_layers;
    uint8_t          strict_order;
    uint8_t          cancel_keys;
    uint8_t          delayed_press;
    uint8_t          release_on_first_key;
    char             name[32];
    cfg_combo_key_t  keys[CFG_COMBO_MAX_KEYS];
    uint8_t          reserved[4];
} cfg_combo_t;

_Static_assert(sizeof(cfg_combo_t) == 64, "cfg_combo_t size mismatch");
_Static_assert(offsetof(cfg_combo_t, keys) == 44, "offset mismatch");

typedef struct {
    uint32_t active_mask;  // Bit N = 1 if combo N exists (N 0..31)
} cfg_combo_index_t;

/* cfgmod handler callbacks */
void   combos_default(void *out_struct);

/* High-level helpers */
esp_err_t combos_delete_single(uint16_t id, cfg_combo_index_t *idx);
esp_err_t combos_load_all(cfg_combo_t *out_arr, size_t *out_count);

void cfg_combos_register(cfgmod_on_update_fn update_fn);
