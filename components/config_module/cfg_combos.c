#include "cfg_combos.h"
#include "cfgmod.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "cfg_cmb";

/* =========================================================================
 * cJSON Serialization / Deserialization
 * ========================================================================= */

void combos_default(void *out_struct) {
    cfg_combo_t *out = (cfg_combo_t *)out_struct;
    memset(out, 0, sizeof(cfg_combo_t));
    out->id = 0;
    snprintf(out->name, sizeof(out->name), "New Combo");
    out->key_count = 0;
    out->action = 0;
    out->active_layers = 0x0F; // Active on layers 0-3 by default
    out->strict_order = false;
    out->cancel_keys = true;
    out->delayed_press = false;
    out->delay_ms = 50;
    out->release_on_first_key = true;
}

bool combos_deserialize(cJSON *root, void *out_struct) {
    if (!cJSON_IsObject(root)) return false;

    cfg_combo_t *out = (cfg_combo_t *)out_struct;
    combos_default(out);

    cJSON *item;

    item = cJSON_GetObjectItem(root, "id");
    if (cJSON_IsNumber(item)) out->id = item->valueint;

    item = cJSON_GetObjectItem(root, "name");
    if (cJSON_IsString(item) && item->valuestring) {
        snprintf(out->name, sizeof(out->name), "%s", item->valuestring);
    }

    item = cJSON_GetObjectItem(root, "action");
    if (cJSON_IsNumber(item)) out->action = item->valueint;

    item = cJSON_GetObjectItem(root, "strictOrder");
    if (cJSON_IsBool(item)) out->strict_order = cJSON_IsTrue(item);

    item = cJSON_GetObjectItem(root, "cancelKeys");
    if (cJSON_IsBool(item)) out->cancel_keys = cJSON_IsTrue(item);

    item = cJSON_GetObjectItem(root, "delayedPress");
    if (cJSON_IsBool(item)) out->delayed_press = cJSON_IsTrue(item);

    item = cJSON_GetObjectItem(root, "delayMs");
    if (cJSON_IsNumber(item)) out->delay_ms = item->valueint;

    item = cJSON_GetObjectItem(root, "releaseOnFirstKey");
    if (cJSON_IsBool(item)) out->release_on_first_key = cJSON_IsTrue(item);

    item = cJSON_GetObjectItem(root, "activeLayers");
    if (cJSON_IsArray(item)) {
        out->active_layers = 0;
        int arr_sz = cJSON_GetArraySize(item);
        for (int i = 0; i < arr_sz; i++) {
            cJSON *layer_num = cJSON_GetArrayItem(item, i);
            if (cJSON_IsNumber(layer_num)) {
                int l = layer_num->valueint;
                if (l >= 0 && l < 8) {
                    out->active_layers |= (1 << l);
                }
            }
        }
    }

    cJSON *keys_arr = cJSON_GetObjectItem(root, "keys");
    if (cJSON_IsArray(keys_arr)) {
        int keys_len = cJSON_GetArraySize(keys_arr);
        if (keys_len > CFG_COMBO_MAX_KEYS) keys_len = CFG_COMBO_MAX_KEYS;
        out->key_count = keys_len;

        for (int i = 0; i < keys_len; i++) {
            cJSON *k_obj = cJSON_GetArrayItem(keys_arr, i);
            if (cJSON_IsObject(k_obj)) {
                cJSON *r = cJSON_GetObjectItem(k_obj, "row");
                cJSON *c = cJSON_GetObjectItem(k_obj, "col");
                if (cJSON_IsNumber(r) && cJSON_IsNumber(c)) {
                    out->keys[i].row = r->valueint;
                    out->keys[i].col = c->valueint;
                }
            }
        }
    }

    return true;
}

cJSON *combos_serialize(const void *in_struct) {
    const cfg_combo_t *in = (const cfg_combo_t *)in_struct;
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddNumberToObject(root, "id", in->id);
    cJSON_AddStringToObject(root, "name", in->name);
    cJSON_AddNumberToObject(root, "action", in->action);
    cJSON_AddBoolToObject(root, "strictOrder", in->strict_order);
    cJSON_AddBoolToObject(root, "cancelKeys", in->cancel_keys);
    cJSON_AddBoolToObject(root, "delayedPress", in->delayed_press);
    cJSON_AddNumberToObject(root, "delayMs", in->delay_ms);
    cJSON_AddBoolToObject(root, "releaseOnFirstKey", in->release_on_first_key);

    cJSON *layers = cJSON_AddArrayToObject(root, "activeLayers");
    if (layers) {
        for (int i = 0; i < 8; i++) {
            if ((in->active_layers & (1 << i)) != 0) {
                cJSON_AddItemToArray(layers, cJSON_CreateNumber(i));
            }
        }
    }

    cJSON *keys_arr = cJSON_AddArrayToObject(root, "keys");
    if (keys_arr) {
        for (int i = 0; i < in->key_count; i++) {
            cJSON *k_obj = cJSON_CreateObject();
            if (k_obj) {
                cJSON_AddNumberToObject(k_obj, "row", in->keys[i].row);
                cJSON_AddNumberToObject(k_obj, "col", in->keys[i].col);
                cJSON_AddItemToArray(keys_arr, k_obj);
            }
        }
    }

    return root;
}

/* =========================================================================
 * High-level Accessors
 * ========================================================================= */

cJSON *combos_serialize_limits(void) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddNumberToObject(root, "maxCombos", CFG_COMBOS_MAX_COUNT);
    cJSON_AddNumberToObject(root, "maxKeys", CFG_COMBO_MAX_KEYS);
    return root;
}

cJSON *combos_serialize_outline(const cfg_combo_index_t *idx) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON *arr = cJSON_AddArrayToObject(root, "combos");
    if (!arr) {
        cJSON_Delete(root);
        return NULL;
    }

    if (!idx || idx->active_mask == 0) return root;

    for (int i = 0; i < CFG_COMBOS_MAX_COUNT; i++) {
        if ((idx->active_mask & (1U << i)) == 0) continue;

        char key[16];
        snprintf(key, sizeof(key), "cmb_%d", i);

        cfg_combo_t *cmb = heap_caps_malloc(sizeof(cfg_combo_t), MALLOC_CAP_SPIRAM);
        if (!cmb) cmb = malloc(sizeof(cfg_combo_t));
        if (!cmb) continue;

        size_t loaded_len = sizeof(cfg_combo_t);
        esp_err_t err = cfgmod_read_storage(CFGMOD_KIND_COMBO, key, cmb, &loaded_len);

        if (err == ESP_OK && loaded_len == sizeof(cfg_combo_t)) {
            // Outline only needs a subset
            cJSON *item = cJSON_CreateObject();
            if (item) {
                cJSON_AddNumberToObject(item, "id", cmb->id);
                cJSON_AddStringToObject(item, "name", cmb->name);
                cJSON_AddNumberToObject(item, "action", cmb->action);
                cJSON_AddBoolToObject(item, "strictOrder", cmb->strict_order);
                cJSON_AddBoolToObject(item, "cancelKeys", cmb->cancel_keys);
                cJSON_AddBoolToObject(item, "delayedPress", cmb->delayed_press);
                cJSON_AddBoolToObject(item, "releaseOnFirstKey", cmb->release_on_first_key);
                
                cJSON *keys_arr = cJSON_AddArrayToObject(item, "keys");
                if (keys_arr) {
                    for (int k = 0; k < cmb->key_count; k++) {
                        cJSON *k_obj = cJSON_CreateObject();
                        if (k_obj) {
                            cJSON_AddNumberToObject(k_obj, "row", cmb->keys[k].row);
                            cJSON_AddNumberToObject(k_obj, "col", cmb->keys[k].col);
                            cJSON_AddItemToArray(keys_arr, k_obj);
                        }
                    }
                }
                
                cJSON *layers = cJSON_AddArrayToObject(item, "activeLayers");
                if (layers) {
                    for (int l = 0; l < 8; l++) {
                        if ((cmb->active_layers & (1 << l)) != 0) {
                            cJSON_AddItemToArray(layers, cJSON_CreateNumber(l));
                        }
                    }
                }

                cJSON_AddItemToArray(arr, item);
            }
        }
        free(cmb);
    }
    return root;
}

cJSON *combos_serialize_single(uint16_t id, const cfg_combo_index_t *idx) {
    if (id >= CFG_COMBOS_MAX_COUNT) return NULL;
    if (!idx || (idx->active_mask & (1U << id)) == 0) return NULL;

    char key[16];
    snprintf(key, sizeof(key), "cmb_%d", id);

    cfg_combo_t *cmb = heap_caps_malloc(sizeof(cfg_combo_t), MALLOC_CAP_SPIRAM);
    if (!cmb) cmb = malloc(sizeof(cfg_combo_t));
    if (!cmb) return NULL;

    size_t loaded_len = sizeof(cfg_combo_t);
    esp_err_t err = cfgmod_read_storage(CFGMOD_KIND_COMBO, key, cmb, &loaded_len);

    cJSON *root = NULL;
    if (err == ESP_OK && loaded_len == sizeof(cfg_combo_t)) {
        root = combos_serialize(cmb);
    }

    free(cmb);
    return root;
}

esp_err_t combos_upsert_single(cJSON *combo_json, cfg_combo_index_t *idx) {
    if (!combo_json || !idx) return ESP_ERR_INVALID_ARG;

    cfg_combo_t *cmb = heap_caps_malloc(sizeof(cfg_combo_t), MALLOC_CAP_SPIRAM);
    if (!cmb) cmb = malloc(sizeof(cfg_combo_t));
    if (!cmb) return ESP_ERR_NO_MEM;

    if (!combos_deserialize(combo_json, cmb)) {
        free(cmb);
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t id = cmb->id;
    if (id >= CFG_COMBOS_MAX_COUNT) {
        free(cmb);
        return ESP_ERR_INVALID_ARG;
    }

    char key[16];
    snprintf(key, sizeof(key), "cmb_%d", id);
    esp_err_t err = cfgmod_write_storage(CFGMOD_KIND_COMBO, key, cmb, sizeof(cfg_combo_t));
    if (err == ESP_OK) {
        if ((idx->active_mask & (1U << id)) == 0) {
            idx->active_mask |= (1U << id);
            err = cfgmod_write_storage(CFGMOD_KIND_COMBO, "cmb_idx", idx, sizeof(cfg_combo_index_t));
        }
    }

    free(cmb);
    return err;
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

/* =========================================================================
 * Registration
 * ========================================================================= */

void cfg_combos_register(cfgmod_on_update_fn update_fn) {
    cfgmod_register_kind(
        CFGMOD_KIND_COMBO,
        combos_default,
        combos_deserialize,
        combos_serialize,
        update_fn,
        sizeof(cfg_combo_t)
    );
}
