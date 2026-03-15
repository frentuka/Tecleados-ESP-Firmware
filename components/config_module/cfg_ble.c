#include "cfg_ble.h"

#include <string.h>

#include "esp_log.h"
#include "cJSON.h"
#include "cfgmod.h"
#include "cfg_storage_keys.h"

static const char *TAG = "cfg_ble";

cfg_ble_state_t g_cfg_ble_state;

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
    ESP_LOGI(TAG, "BLE configs updated from NVS");
}

void cfg_ble_init(void) {
    ble_default(&g_cfg_ble_state);
    
    cfgmod_register_kind(CFGMOD_KIND_CONNECTION, ble_default, ble_deserialize, 
                         ble_serialize, on_ble_updated, sizeof(cfg_ble_state_t));
                         
    // Load initial from NVS if available
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
