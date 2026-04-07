#include "cfg_ble.h"

#include <string.h>

#include "esp_log.h"
#include "cJSON.h"
#include "cfgmod.h"
#include "event_bus.h"
#include "nvs.h"

static const char *TAG = "cfg_ble";

static cfg_ble_state_t g_cfg_ble_state;

static void ble_default(void *dest) {
    if (!dest) return;
    cfg_ble_state_t *st = (cfg_ble_state_t *)dest;
    memset(st, 0, sizeof(cfg_ble_state_t));
    st->ble_routing_enabled = true; // default ON
    st->selected_profile = 0;       // default profile 0
}

static bool ble_deserialize(cJSON *json, void *dest) {
    if (!json || !dest) return false;
    cfg_ble_state_t *st = (cfg_ble_state_t *)dest;
    ble_default(st); // Start clean

    cJSON *j_routing = cJSON_GetObjectItemCaseSensitive(json, "routing");
    if (cJSON_IsBool(j_routing)) {
        st->ble_routing_enabled = cJSON_IsTrue(j_routing);
    }

    cJSON *j_selected = cJSON_GetObjectItemCaseSensitive(json, "selected");
    if (cJSON_IsNumber(j_selected)) {
        st->selected_profile = (uint8_t)j_selected->valueint;
        if (st->selected_profile >= CFG_BLE_MAX_PROFILES) {
            st->selected_profile = 0;
        }
    }

    cJSON *j_profiles = cJSON_GetObjectItemCaseSensitive(json, "profiles");
    if (cJSON_IsArray(j_profiles)) {
        int count = cJSON_GetArraySize(j_profiles);
        if (count > CFG_BLE_MAX_PROFILES) count = CFG_BLE_MAX_PROFILES;

        for (int i = 0; i < count; i++) {
            cJSON *p = cJSON_GetArrayItem(j_profiles, i);
            if (!p) continue;

            cJSON *j_valid = cJSON_GetObjectItemCaseSensitive(p, "valid");
            if (cJSON_IsBool(j_valid) && cJSON_IsTrue(j_valid)) {
                st->profiles[i].is_valid = true;

                cJSON *j_type = cJSON_GetObjectItemCaseSensitive(p, "type");
                if (cJSON_IsNumber(j_type)) st->profiles[i].addr_type = j_type->valueint;

                cJSON *j_mac = cJSON_GetObjectItemCaseSensitive(p, "mac");
                if (cJSON_IsArray(j_mac) && cJSON_GetArraySize(j_mac) == 6) {
                    for (int j = 0; j < 6; j++) {
                        st->profiles[i].val[j] = (uint8_t)cJSON_GetArrayItem(j_mac, j)->valueint;
                    }
                }

                cJSON *j_nonce = cJSON_GetObjectItemCaseSensitive(p, "nonce");
                if (cJSON_IsNumber(j_nonce)) st->profiles[i].addr_nonce = (uint8_t)j_nonce->valueint;
            }
        }
    }

    return true;
}

static cJSON *ble_serialize(const void *src) {
    if (!src) return NULL;
    cJSON *json = cJSON_CreateObject();
    if (!json) return NULL;
    const cfg_ble_state_t *st = (const cfg_ble_state_t *)src;

    cJSON_AddBoolToObject(json, "routing", st->ble_routing_enabled);
    cJSON_AddNumberToObject(json, "selected", st->selected_profile);

    cJSON *j_profiles = cJSON_CreateArray();
    if (!j_profiles) {
        cJSON_Delete(json);
        return NULL;
    }

    for (int i = 0; i < CFG_BLE_MAX_PROFILES; i++) {
        cJSON *p = cJSON_CreateObject();
        if (!p) continue;

        if (st->profiles[i].is_valid) {
            cJSON_AddBoolToObject(p, "valid", true);
            cJSON_AddNumberToObject(p, "type", st->profiles[i].addr_type);

            cJSON *j_mac = cJSON_CreateArray();
            for (int j = 0; j < 6; j++) {
                cJSON_AddItemToArray(j_mac, cJSON_CreateNumber(st->profiles[i].val[j]));
            }
            cJSON_AddItemToObject(p, "mac", j_mac);

            cJSON_AddNumberToObject(p, "nonce", st->profiles[i].addr_nonce);
        } else {
            cJSON_AddBoolToObject(p, "valid", false);
        }

        cJSON_AddItemToArray(j_profiles, p);
    }

    cJSON_AddItemToObject(json, "profiles", j_profiles);
    return json;
}

static void on_ble_updated(const char *key) {
    esp_err_t err = cfgmod_get_config(CFGMOD_KIND_CONNECTION, key, &g_cfg_ble_state);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "BLE config reloaded: selected=%d, routing=%d",
                 g_cfg_ble_state.selected_profile, g_cfg_ble_state.ble_routing_enabled);
    } else {
        ESP_LOGE(TAG, "BLE config reload failed: 0x%X", (unsigned)err);
    }
}

static void cfg_ble_on_pairing_complete(void *arg, esp_event_base_t base,
                                        int32_t event_id, void *data) {
    (void)arg; (void)base; (void)event_id;
    const ble_pairing_result_t *r = (const ble_pairing_result_t *)data;
    if (r->profile_idx < 0 || r->profile_idx >= CFG_BLE_MAX_PROFILES) return;

    ESP_LOGI(TAG, "Saving pairing result for profile %d via event", r->profile_idx);
    cfg_ble_state_t new_state = g_cfg_ble_state;
    new_state.profiles[r->profile_idx].is_valid  = true;
    new_state.profiles[r->profile_idx].addr_type = r->addr_type;
    memcpy(new_state.profiles[r->profile_idx].val, r->addr, 6);
    new_state.selected_profile = r->profile_idx;
    cfg_ble_save_state(&new_state);

    // Trigger bond key sync to slave so the new Master has the LTK after a role swap.
    config_update_event_t ev = { .kind = (uint8_t)CFGMOD_KIND_BLE_BOND };
    strncpy(ev.key, "all", sizeof(ev.key));
    ev.key[sizeof(ev.key) - 1] = '\0';
    esp_event_post(CONFIG_EVENTS, CONFIG_EVENT_KIND_UPDATED, &ev, sizeof(ev), 0);
}

void cfg_ble_init(void) {
    ble_default(&g_cfg_ble_state);

    cfgmod_register_kind(CFGMOD_KIND_CONNECTION, ble_default, ble_deserialize,
                         ble_serialize, on_ble_updated, sizeof(cfg_ble_state_t));

    // Subscribe to pairing complete event — owns the credential save.
    esp_event_handler_register(BLE_EVENTS, BLE_EVENT_PAIRING_COMPLETE,
                               cfg_ble_on_pairing_complete, NULL);

    // Load initial from NVS if available.
    esp_err_t err = cfgmod_get_config(CFGMOD_KIND_CONNECTION, "ble_cfg", &g_cfg_ble_state);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Loaded BLE config, selected=%d, routing=%d",
                 g_cfg_ble_state.selected_profile, g_cfg_ble_state.ble_routing_enabled);
    } else {
        ESP_LOGI(TAG, "No BLE config found in NVS, using defaults");
    }
}

const cfg_ble_state_t *cfg_ble_get_state(void) {
    return &g_cfg_ble_state;
}

void cfg_ble_save_state(const cfg_ble_state_t *state) {
    if (!state) return;
    memcpy(&g_cfg_ble_state, state, sizeof(cfg_ble_state_t));
    esp_err_t err = cfgmod_set_config(CFGMOD_KIND_CONNECTION, "ble_cfg", &g_cfg_ble_state);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save BLE config: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Saved BLE config to NVS");
    }
}

/*
 * Bond sync serialization format (binary TLV):
 *   [uint16_t count]
 *   then for each entry:
 *     [uint8_t  key_len]          -- length including null terminator
 *     [char     key[key_len]]     -- NVS key (null-terminated)
 *     [uint16_t data_len]         -- blob size in bytes
 *     [uint8_t  data[data_len]]   -- raw NVS blob
 */
esp_err_t cfg_ble_bond_read_all(void *out_buf, size_t *inout_len) {
    if (!inout_len) return ESP_ERR_INVALID_ARG;

    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find("nvs", "nimble_bond", NVS_TYPE_BLOB, &it);
    if (err == ESP_ERR_NVS_NOT_FOUND || it == NULL) {
        // No bonds — write an empty packet (count=0).
        if (out_buf && *inout_len >= sizeof(uint16_t)) {
            uint16_t zero = 0;
            memcpy(out_buf, &zero, sizeof(uint16_t));
        }
        *inout_len = sizeof(uint16_t);
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    nvs_handle_t handle;
    if (nvs_open("nimble_bond", NVS_READONLY, &handle) != ESP_OK) {
        nvs_release_iterator(it);
        return ESP_FAIL;
    }

    size_t   req_size = sizeof(uint16_t);
    uint16_t count    = 0;
    uint8_t *ptr      = out_buf ? (uint8_t *)out_buf + sizeof(uint16_t) : NULL;

    while (it != NULL) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);

        size_t blob_len = 0;
        if (nvs_get_blob(handle, info.key, NULL, &blob_len) == ESP_OK) {
            uint8_t klen       = (uint8_t)(strlen(info.key) + 1);
            size_t  entry_size = sizeof(uint8_t) + klen + sizeof(uint16_t) + blob_len;

            req_size += entry_size;
            count++;

            if (ptr) {
                if (req_size > *inout_len) {
                    nvs_close(handle);
                    nvs_release_iterator(it);
                    return ESP_ERR_NO_MEM;
                }
                *ptr++ = klen;
                memcpy(ptr, info.key, klen);
                ptr += klen;

                uint16_t dlen = (uint16_t)blob_len;
                memcpy(ptr, &dlen, sizeof(uint16_t));
                ptr += sizeof(uint16_t);

                nvs_get_blob(handle, info.key, ptr, &blob_len);
                ptr += blob_len;
            }
        }

        err = nvs_entry_next(&it);
        if (err != ESP_OK) break;
    }

    if (out_buf && req_size <= *inout_len) {
        memcpy(out_buf, &count, sizeof(uint16_t));
    }

    nvs_close(handle);
    // it is already NULL or released by nvs_entry_next returning error
    *inout_len = req_size;
    ESP_LOGI(TAG, "Bond read_all: %u entries, %u bytes", (unsigned)count, (unsigned)req_size);
    return ESP_OK;
}

esp_err_t cfg_ble_bond_write_all(const void *data, size_t len) {
    if (!data || len < sizeof(uint16_t)) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    if (nvs_open("nimble_bond", NVS_READWRITE, &handle) != ESP_OK) {
        return ESP_FAIL;
    }

    nvs_erase_all(handle);

    const uint8_t *ptr = (const uint8_t *)data;
    uint16_t count;
    memcpy(&count, ptr, sizeof(uint16_t));
    ptr += sizeof(uint16_t);
    size_t rem = len - sizeof(uint16_t);

    for (uint16_t i = 0; i < count; i++) {
        if (rem < sizeof(uint8_t)) break;
        uint8_t klen = *ptr++;
        rem -= sizeof(uint8_t);

        if (rem < klen) break;
        const char *key = (const char *)ptr;
        ptr += klen;
        rem -= klen;

        if (rem < sizeof(uint16_t)) break;
        uint16_t dlen;
        memcpy(&dlen, ptr, sizeof(uint16_t));
        ptr += sizeof(uint16_t);
        rem -= sizeof(uint16_t);

        if (rem < dlen) break;
        nvs_set_blob(handle, key, ptr, dlen);
        ptr += dlen;
        rem -= dlen;
    }

    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "Bond write_all: restored %u entries to nimble_bond", (unsigned)count);
    return ESP_OK;
}
