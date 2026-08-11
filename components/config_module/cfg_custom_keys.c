#include "cfg_custom_keys.h"

#include "cfgmod.h"

#include "esp_log.h"
#include <string.h>

static const char *TAG = "cfg_ckeys";

/* ============================================================
   cfgmod handler callbacks
   ============================================================ */

void ckeys_default(void *out_struct) {
    cfg_custom_key_t *ck = (cfg_custom_key_t *)out_struct;
    memset(ck, 0, sizeof(cfg_custom_key_t));
}



/* ============================================================
   High-level helpers (outline, single, upsert, delete, load)
   ============================================================ */



esp_err_t ckeys_delete_single(uint16_t id, cfg_ckey_index_t *idx) {
    if (id >= CFG_CKEYS_MAX_COUNT) return ESP_ERR_INVALID_ARG;

    idx->mask[id / 8] &= ~(1u << (id % 8));
    esp_err_t err = cfgmod_write_storage(CFGMOD_KIND_CKEY, "ck_idx", idx, sizeof(cfg_ckey_index_t));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Deleted custom key %u", id);
    }
    return err;
}

esp_err_t ckeys_load_all(cfg_custom_key_t *out_arr, size_t *out_count) {
    if (!out_arr || !out_count) return ESP_ERR_INVALID_ARG;
    *out_count = 0;

    cfg_ckey_index_t idx = {0};
    size_t idx_len = sizeof(idx);
    if (cfgmod_read_storage(CFGMOD_KIND_CKEY, "ck_idx", &idx, &idx_len) != ESP_OK) {
        ESP_LOGI(TAG, "ckeys_load_all: no ck_idx found in NVS");
        return ESP_OK; /* No index yet — empty list */
    }

    for (uint16_t i = 0; i < CFG_CKEYS_MAX_COUNT; i++) {
        if (!((idx.mask[i / 8] >> (i % 8)) & 1u)) continue;

        char nvs_key[12];
        snprintf(nvs_key, sizeof(nvs_key), "ck_%u", i);
        size_t len = sizeof(cfg_custom_key_t);
        esp_err_t err = cfgmod_read_storage(CFGMOD_KIND_CKEY, nvs_key, &out_arr[*out_count], &len);
        if (err == ESP_OK && len == sizeof(cfg_custom_key_t)) {
            ESP_LOGI(TAG, "  loaded ckey_%u ('%s')", i, out_arr[*out_count].name);
            (*out_count)++;
        } else {
            ESP_LOGW(TAG, "  failed to load ckey_%u or size mismatch (err=0x%X, len=%u)", i, (unsigned)err, (unsigned)len);
        }
    }
    return ESP_OK;
}

esp_err_t ckeys_validate(void *in_struct) {
    cfg_custom_key_t *ck = (cfg_custom_key_t *)in_struct;
    ck->name[sizeof(ck->name) - 1] = '\0';
    if (ck->mode > CKEY_MODE_MULTI_ACTION) {
        ck->mode = CKEY_MODE_PRESS_RELEASE;
    }
    return ESP_OK;
}

/* ============================================================
   Registration
   ============================================================ */

void cfg_custom_keys_register(cfgmod_on_update_fn update_fn) {
    cfgmod_register_validate(CFGMOD_KIND_CKEY, ckeys_validate);
    /* Minimal registration: gives cfgmod_read/write_storage a valid kind slot.
       The actual GET/SET command handling is done in custom cfgmod.c blocks,
       identical to the macro pattern. */
    cfgmod_register_kind(CFGMOD_KIND_CKEY,
                         ckeys_default,
                         update_fn,
                         sizeof(cfg_custom_key_t));
}
