#include "cfg_ble.h"

#include <string.h>

#include "esp_log.h"
#include "cJSON.h"
#include "cfgmod.h"
#include "event_bus.h"
#include "host/ble_store.h"
#include "blemod.h"

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

void cfg_ble_reload(void) {
    // Force re-read from NVS.  Call this when the in-memory cache may be stale
    // (e.g. after config sync has written updated data to NVS while BLE was
    // suspended as a split slave).
    on_ble_updated("ble_cfg");
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
 * Bond sync serialization format:
 *   [uint8_t our_count]
 *   [struct ble_store_value_sec  our_secs[our_count]]
 *   [uint8_t peer_count]
 *   [struct ble_store_value_sec  peer_secs[peer_count]]
 *
 * Uses NimBLE's public store API so both the in-memory cache and NVS are kept
 * in sync.  Raw NVS access would only update NVS; the in-memory cache (which
 * ble_store_config_read searches) would remain stale and reconnection would
 * fail without re-pairing.
 */
esp_err_t cfg_ble_bond_read_all(void *out_buf, size_t *inout_len) {
    if (!inout_len) return ESP_ERR_INVALID_ARG;

    // Heap-allocate: sizeof(ble_store_value_sec) ~88 bytes × 9 × 2 = ~1.5 kB
    // which would overflow the split task stack if placed there.
    const size_t sec_buf_size = CFG_BLE_MAX_PROFILES * sizeof(struct ble_store_value_sec);
    struct ble_store_value_sec *our_secs  = malloc(sec_buf_size);
    struct ble_store_value_sec *peer_secs = malloc(sec_buf_size);
    if (!our_secs || !peer_secs) {
        free(our_secs);
        free(peer_secs);
        return ESP_ERR_NO_MEM;
    }

    int our_count = 0, peer_count = 0;
    struct ble_store_key_sec key;

    memset(&key, 0, sizeof(key));
    key.peer_addr = *BLE_ADDR_ANY;
    for (int i = 0; i < CFG_BLE_MAX_PROFILES; i++) {
        key.idx = (uint8_t)i;
        if (ble_store_read_our_sec(&key, &our_secs[our_count]) != 0) break;
        our_count++;
    }

    memset(&key, 0, sizeof(key));
    key.peer_addr = *BLE_ADDR_ANY;
    for (int i = 0; i < CFG_BLE_MAX_PROFILES; i++) {
        key.idx = (uint8_t)i;
        if (ble_store_read_peer_sec(&key, &peer_secs[peer_count]) != 0) break;
        peer_count++;
    }

    for (int i = 0; i < our_count; i++) {
        const uint8_t *pa = our_secs[i].peer_addr.val;
        ESP_LOGI(TAG, "BOND_READ our[%d] peer=%02X:%02X:%02X:%02X:%02X:%02X sc=%d ltk_ok=%d",
                 i, pa[0], pa[1], pa[2], pa[3], pa[4], pa[5],
                 our_secs[i].sc, our_secs[i].ltk_present);
    }
    for (int i = 0; i < peer_count; i++) {
        const uint8_t *pa = peer_secs[i].peer_addr.val;
        ESP_LOGI(TAG, "BOND_READ peer[%d] peer=%02X:%02X:%02X:%02X:%02X:%02X sc=%d ltk_ok=%d",
                 i, pa[0], pa[1], pa[2], pa[3], pa[4], pa[5],
                 peer_secs[i].sc, peer_secs[i].ltk_present);
    }

    size_t req = 1 + (size_t)our_count  * sizeof(struct ble_store_value_sec)
               + 1 + (size_t)peer_count * sizeof(struct ble_store_value_sec);

    if (out_buf && *inout_len >= req) {
        uint8_t *p = (uint8_t *)out_buf;
        *p++ = (uint8_t)our_count;
        memcpy(p, our_secs, (size_t)our_count * sizeof(struct ble_store_value_sec));
        p += (size_t)our_count * sizeof(struct ble_store_value_sec);
        *p++ = (uint8_t)peer_count;
        memcpy(p, peer_secs, (size_t)peer_count * sizeof(struct ble_store_value_sec));
    }

    free(our_secs);
    free(peer_secs);

    *inout_len = req;
    ESP_LOGI(TAG, "Bond read_all: %d our_secs + %d peer_secs = %u bytes",
             our_count, peer_count, (unsigned)req);
    return ESP_OK;
}

esp_err_t cfg_ble_bond_write_all(const void *data, size_t len) {
    if (!data || len < 2) return ESP_ERR_INVALID_ARG;

    const uint8_t *p = (const uint8_t *)data;
    size_t rem = len;

    uint8_t our_count = *p++; rem--;
    size_t our_size = (size_t)our_count * sizeof(struct ble_store_value_sec);
    if (rem < our_size + 1) return ESP_ERR_INVALID_SIZE;
    const struct ble_store_value_sec *our_secs = (const struct ble_store_value_sec *)p;
    p += our_size; rem -= our_size;

    uint8_t peer_count = *p++; rem--;
    size_t peer_size = (size_t)peer_count * sizeof(struct ble_store_value_sec);
    if (rem < peer_size) return ESP_ERR_INVALID_SIZE;
    const struct ble_store_value_sec *peer_secs = (const struct ble_store_value_sec *)p;

    // Write through NimBLE's public API — updates both RAM cache and NVS.
    for (int i = 0; i < our_count; i++) {
        const uint8_t *pa = our_secs[i].peer_addr.val;
        ESP_LOGI(TAG, "BOND_WRITE our[%d] peer=%02X:%02X:%02X:%02X:%02X:%02X sc=%d ltk_ok=%d",
                 i, pa[0], pa[1], pa[2], pa[3], pa[4], pa[5],
                 our_secs[i].sc, our_secs[i].ltk_present);
        int rc = ble_store_write_our_sec(&our_secs[i]);
        if (rc != 0) ESP_LOGW(TAG, "ble_store_write_our_sec[%d] rc=%d", i, rc);
    }
    for (int i = 0; i < peer_count; i++) {
        const uint8_t *pa = peer_secs[i].peer_addr.val;
        ESP_LOGI(TAG, "BOND_WRITE peer[%d] peer=%02X:%02X:%02X:%02X:%02X:%02X sc=%d ltk_ok=%d",
                 i, pa[0], pa[1], pa[2], pa[3], pa[4], pa[5],
                 peer_secs[i].sc, peer_secs[i].ltk_present);
        int rc = ble_store_write_peer_sec(&peer_secs[i]);
        if (rc != 0) ESP_LOGW(TAG, "ble_store_write_peer_sec[%d] rc=%d", i, rc);
    }

    ESP_LOGI(TAG, "Bond write_all: wrote %d our_secs + %d peer_secs", our_count, peer_count);

    // Pre-warm the BLE controller's resolving list so that the next role swap can
    // start advertising immediately with a fully-populated resolving list.
    // ble_hid_reinit_bonds() is a no-op if we are currently the active master.
    ble_hid_reinit_bonds();

    return ESP_OK;
}
