#include "cfg_combos.h"
#include "cfgmod.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "cfg_cmb";

/* =========================================================================
 * High-level Accessors
 * ========================================================================= */

void combos_default(void *out_struct) {
    memset(out_struct, 0, sizeof(cfg_combo_t));
}

esp_err_t combos_delete_single(uint16_t id, cfg_combo_index_t *idx) {
    if (!idx || id >= CFG_COMBOS_MAX_COUNT) return ESP_ERR_INVALID_ARG;

    if ((idx->active_mask & (1U << id)) != 0) {
        idx->active_mask &= ~(1U << id);
        return cfgmod_write_storage(CFGMOD_KIND_COMBO, "cmb_idx", idx, sizeof(cfg_combo_index_t));
    }
    return ESP_OK;
}

esp_err_t combos_load_all(cfg_combo_t *out_arr, size_t *out_count) {
    if (!out_arr || !out_count) return ESP_ERR_INVALID_ARG;

    cfg_combo_index_t idx = { .active_mask = 0 };
    size_t loaded_len = sizeof(cfg_combo_index_t);
    esp_err_t err = cfgmod_read_storage(CFGMOD_KIND_COMBO, "cmb_idx", &idx, &loaded_len);
    if (err != ESP_OK || loaded_len != sizeof(cfg_combo_index_t)) {
        idx.active_mask = 0;
    }

    *out_count = 0;
    if (idx.active_mask == 0) return ESP_OK;

    for (int i = 0; i < CFG_COMBOS_MAX_COUNT; i++) {
        if ((idx.active_mask & (1U << i)) == 0) continue;

        char key[16];
        snprintf(key, sizeof(key), "cmb_%d", i);

        loaded_len = sizeof(cfg_combo_t);
        err = cfgmod_read_storage(CFGMOD_KIND_COMBO, key, &out_arr[*out_count], &loaded_len);
        if (err == ESP_OK && loaded_len == sizeof(cfg_combo_t)) {
            (*out_count)++;
        } else {
            ESP_LOGW(TAG, "Failed to load active combo %d (err=0x%X)", i, err);
        }
    }

    return ESP_OK;
}

esp_err_t combos_validate(void *in_struct) {
    cfg_combo_t *c = (cfg_combo_t *)in_struct;
    c->name[sizeof(c->name) - 1] = '\0';
    if (c->key_count > CFG_COMBO_MAX_KEYS) {
        c->key_count = CFG_COMBO_MAX_KEYS;
    }
    return ESP_OK;
}

/* =========================================================================
 * Registration
 * ========================================================================= */

void cfg_combos_register(cfgmod_on_update_fn update_fn) {
    cfgmod_register_validate(CFGMOD_KIND_COMBO, combos_validate);
    cfgmod_register_kind(
        CFGMOD_KIND_COMBO,
        combos_default,
        update_fn,
        sizeof(cfg_combo_t)
    );
}
