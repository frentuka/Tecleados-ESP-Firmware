#include "cfg_ble.h"

#include <string.h>

#include "esp_log.h"
#include "cJSON.h"
#include "cfgmod.h"
#include "event_bus.h"
#include "host/ble_store.h"
#include "host/ble_hs.h"
#include "blemod.h"

static const char *TAG = "cfg_ble";

#define BOND_SYNC_VERSION 3

struct bond_sync_header {
    uint8_t version;
    uint8_t num_sections;
    uint16_t sync_version;
} __attribute__((packed));

struct bond_sync_section {
    uint8_t type;         // BLE_STORE_OBJ_TYPE_*
    uint16_t record_count;
    uint16_t record_size;
} __attribute__((packed));

static cfg_ble_state_t g_cfg_ble_state;

static void ble_default(void *dest) {
    if (!dest) return;
    cfg_ble_state_t *st = (cfg_ble_state_t *)dest;
    memset(st, 0, sizeof(cfg_ble_state_t));
    st->ble_routing_enabled = true; // default ON
    st->selected_profile = 0;       // default profile 0
    st->sync_version = 0;
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

    cJSON *j_sync = cJSON_GetObjectItemCaseSensitive(json, "sync_version");
    if (cJSON_IsNumber(j_sync)) {
        st->sync_version = (uint16_t)j_sync->valueint;
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
    cJSON_AddNumberToObject(json, "sync_version", st->sync_version);
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

void cfg_ble_set_selected_profile(uint8_t index) {
    if (index < CFG_BLE_MAX_PROFILES) {
        g_cfg_ble_state.selected_profile = index;
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
    new_state.sync_version++; // SUCCESS WINS: Increment to dominate stale syncs
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
 * Bond sync serialization format (Version 2):
 *   [struct bond_sync_header]
 *   [struct bond_sync_section 1]
 *   [Data for section 1]
 *   ...
 *   [struct bond_sync_section N]
 *   [Data for section N]
 */

struct collect_ctx {
    uint8_t *buf;
    size_t offset;
    size_t limit;
    int count;
};

static int collect_cb(int obj_type, union ble_store_value *val, void *cookie) {
    struct collect_ctx *ctx = (struct collect_ctx *)cookie;
    size_t sz = 0;
    void *src = NULL;

    switch (obj_type) {
        case BLE_STORE_OBJ_TYPE_OUR_SEC:
        case BLE_STORE_OBJ_TYPE_PEER_SEC:
            sz = sizeof(struct ble_store_value_sec);
            src = &val->sec;
            break;
        case BLE_STORE_OBJ_TYPE_CCCD:
            sz = sizeof(struct ble_store_value_cccd);
            src = &val->cccd;
            break;
        case BLE_STORE_OBJ_TYPE_LOCAL_IRK:
            sz = sizeof(struct ble_store_value_local_irk);
            src = &val->local_irk;
            break;
        default: return 0;
    }

    if (ctx->buf && ctx->offset + sz <= ctx->limit) {
        memcpy(ctx->buf + ctx->offset, src, sz);
        ctx->offset += sz;
        ctx->count++;

        if (obj_type == BLE_STORE_OBJ_TYPE_PEER_SEC || obj_type == BLE_STORE_OBJ_TYPE_OUR_SEC) {
            const uint8_t *pa = val->sec.peer_addr.val;
            ESP_LOGI(TAG, "BOND_READ sec type=%d peer=%02X:%02X:%02X:%02X:%02X:%02X irk=%d",
                     obj_type, pa[0], pa[1], pa[2], pa[3], pa[4], pa[5], val->sec.irk_present);
        } else if (obj_type == BLE_STORE_OBJ_TYPE_CCCD) {
            const uint8_t *pa = val->cccd.peer_addr.val;
            ESP_LOGI(TAG, "BOND_READ cccd peer=%02X:%02X:%02X:%02X:%02X:%02X handle=%d flags=%d",
                     pa[0], pa[1], pa[2], pa[3], pa[4], pa[5], val->cccd.chr_val_handle, val->cccd.flags);
        }

        return 0; // continue
    } else if (!ctx->buf) {
        ctx->offset += sz;
        ctx->count++;
        return 0;
    }
    return 1; // stop (should not happen if size calculation is correct)
}

esp_err_t cfg_ble_bond_read_all(void *out_buf, size_t *inout_len) {
    if (!inout_len) return ESP_ERR_INVALID_ARG;
    if (!ble_hs_synced()) {
        ESP_LOGE(TAG, "Bond Read: Stack not synced! Cannot read from volatile store.");
        return ESP_ERR_INVALID_STATE;
    }

    int types[] = {
        BLE_STORE_OBJ_TYPE_OUR_SEC,
        BLE_STORE_OBJ_TYPE_PEER_SEC,
        BLE_STORE_OBJ_TYPE_CCCD,
        BLE_STORE_OBJ_TYPE_LOCAL_IRK
    };
    const int num_types = sizeof(types) / sizeof(types[0]);

    // Pass 1: Calculate needed size
    size_t required = sizeof(struct bond_sync_header) + (num_types * sizeof(struct bond_sync_section));
    struct collect_ctx counts[num_types];

    for (int i = 0; i < num_types; i++) {
        counts[i] = (struct collect_ctx){ .buf = NULL, .offset = 0, .limit = 0, .count = 0 };
        ble_store_iterate(types[i], collect_cb, &counts[i]);
        required += counts[i].offset;
    }

    if (!out_buf) {
        *inout_len = required;
        return ESP_OK;
    }
    if (*inout_len < required) {
        *inout_len = required;
        return ESP_ERR_INVALID_SIZE;
    }

    // Pass 2: Serialize
    uint8_t *p = (uint8_t *)out_buf;
    struct bond_sync_header *hdr = (struct bond_sync_header *)p;
    hdr->version = BOND_SYNC_VERSION;
    hdr->num_sections = num_types;
    hdr->sync_version = g_cfg_ble_state.sync_version;
    p += sizeof(*hdr);

    for (int i = 0; i < num_types; i++) {
        struct bond_sync_section *sec = (struct bond_sync_section *)p;
        sec->type = (uint8_t)types[i];
        sec->record_count = (uint16_t)counts[i].count;
        
        switch (types[i]) {
            case BLE_STORE_OBJ_TYPE_OUR_SEC:
            case BLE_STORE_OBJ_TYPE_PEER_SEC:
                sec->record_size = sizeof(struct ble_store_value_sec); break;
            case BLE_STORE_OBJ_TYPE_CCCD:
                sec->record_size = sizeof(struct ble_store_value_cccd); break;
            case BLE_STORE_OBJ_TYPE_LOCAL_IRK:
                sec->record_size = sizeof(struct ble_store_value_local_irk); break;
            default: sec->record_size = 0; break;
        }

        p += sizeof(*sec);
        struct collect_ctx write_ctx = { .buf = p, .offset = 0, .limit = counts[i].offset, .count = 0 };
        ble_store_iterate(types[i], collect_cb, &write_ctx);
        p += counts[i].offset;
    }

    *inout_len = required;
    ESP_LOGI(TAG, "Bond read_all V2: %u bytes (%d sections)", (unsigned)required, (int)num_types);
    return ESP_OK;
}

esp_err_t cfg_ble_bond_write_all(const void *data, size_t len) {
    if (!data || len < sizeof(struct bond_sync_header)) return ESP_ERR_INVALID_ARG;

    const uint8_t *p = (const uint8_t *)data;
    struct bond_sync_header *hdr = (struct bond_sync_header *)p;
    
    if (hdr->version != BOND_SYNC_VERSION) {
        ESP_LOGE(TAG, "Bond write_all version mismatch: got %d, expected %d", hdr->version, BOND_SYNC_VERSION);
        return ESP_ERR_INVALID_VERSION;
    }

    // --- SAFETY GATE ---
    // We must NOT call ble_store_write_* or ble_store_clear if the host is not synced.
    // Doing so triggers privacy/resolving list updates that access uninitialized
    // internal NimBLE structures, causing a LoadProhibited panic.
    if (!ble_hs_synced()) {
        ESP_LOGW(TAG, "Bond Sync: Stack not synced. Deferring hardware-dependent writes.");
        return ESP_ERR_INVALID_STATE; 
    }
    
    if (hdr->sync_version < g_cfg_ble_state.sync_version) {
        ESP_LOGW(TAG, "Bond Sync: Rejecting stale bonds (incoming ver %u < own %u)", 
                 hdr->sync_version, g_cfg_ble_state.sync_version);
        return ESP_OK; // Ignore silently
    }

    p += sizeof(*hdr);
    size_t rem = len - sizeof(*hdr);

    // 1. Clear current store to ensure the sync is a clean mirror.
    // Use ble_store_clear if available, or iterate/delete.
    // In NimBLE, ble_store_clear() usually wipes everything.
    ble_store_clear();

    // 2. Unpack sections
    for (int i = 0; i < hdr->num_sections; i++) {
        if (rem < sizeof(struct bond_sync_section)) break;
        struct bond_sync_section *sec = (struct bond_sync_section *)p;
        p += sizeof(*sec); rem -= sizeof(*sec);

        size_t sec_data_len = (size_t)sec->record_count * sec->record_size;
        if (rem < sec_data_len) {
            ESP_LOGE(TAG, "Bond sync: section %d data truncated", i);
            break;
        }

        const uint8_t *rec_data = p;
        for (int r = 0; r < sec->record_count; r++) {
            union ble_store_value val;
            int rc = -1;

            switch (sec->type) {
                case BLE_STORE_OBJ_TYPE_OUR_SEC:
                    memcpy(&val.sec, rec_data, sizeof(val.sec));
                    rc = ble_store_write_our_sec(&val.sec);
                    {
                        const uint8_t *pa = val.sec.peer_addr.val;
                        ESP_LOGI(TAG, "BOND_WRITE our_sec peer=%02X:%02X:%02X:%02X:%02X:%02X rc=%d",
                                 pa[0], pa[1], pa[2], pa[3], pa[4], pa[5], rc);
                    }
                    break;
                case BLE_STORE_OBJ_TYPE_PEER_SEC:
                    memcpy(&val.sec, rec_data, sizeof(val.sec));
                    rc = ble_store_write_peer_sec(&val.sec);
                    {
                        const uint8_t *pa = val.sec.peer_addr.val;
                        ESP_LOGI(TAG, "BOND_WRITE peer_sec peer=%02X:%02X:%02X:%02X:%02X:%02X irk=%d rc=%d",
                                 pa[0], pa[1], pa[2], pa[3], pa[4], pa[5], val.sec.irk_present, rc);
                    }
                    break;
                case BLE_STORE_OBJ_TYPE_CCCD:
                    memcpy(&val.cccd, rec_data, sizeof(val.cccd));
                    rc = ble_store_write_cccd(&val.cccd);
                    {
                        const uint8_t *pa = val.cccd.peer_addr.val;
                        ESP_LOGI(TAG, "BOND_WRITE cccd peer=%02X:%02X:%02X:%02X:%02X:%02X handle=%d rc=%d",
                                 pa[0], pa[1], pa[2], pa[3], pa[4], pa[5], val.cccd.chr_val_handle, rc);
                    }
                    break;
                case BLE_STORE_OBJ_TYPE_LOCAL_IRK:
                    memcpy(&val.local_irk, rec_data, sizeof(val.local_irk));
                    rc = ble_store_write_local_irk(&val.local_irk);
                    ESP_LOGI(TAG, "BOND_WRITE local_irk rc=%d", rc);
                    break;
            }

            if (rc != 0 && rc != -1) {
                // rc=530 (0x212) is HCI Error 0x12 (Invalid Params). 
                // This often happens during sync when NimBLE tries to update 
                // the resolving list while transitioning. It's harmless since 
                // we call ble_hid_reinit_bonds() at the end.
                if (rc == 530) {
                    ESP_LOGI(TAG, "Bond write partial (HCI 0x12) for type %d (ignored, pending re-warm)", sec->type);
                } else {
                    ESP_LOGW(TAG, "Failed to write bond record type %d, rc=%d", sec->type, rc);
                }
            }
            rec_data += sec->record_size;
        }
        
        p += sec_data_len; rem -= sec_data_len;
        ESP_LOGI(TAG, "Bond write_all: applied %d records of type %d", (int)sec->record_count, (int)sec->type);
    }

    // Warm the resolving list
    ble_hid_reinit_bonds();

    return ESP_OK;
}
