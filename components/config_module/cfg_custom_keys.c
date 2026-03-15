#include "cfg_custom_keys.h"

#include "cfgmod.h"
#include "cJSON.h"
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

/* ---------- Deserialize helpers ---------- */

static bool deserialize_pr(cJSON *pr_obj, cfg_ckey_pr_t *pr) {
    if (!cJSON_IsObject(pr_obj)) return false;

    cJSON *pa = cJSON_GetObjectItem(pr_obj, "pressAction");
    cJSON *ra = cJSON_GetObjectItem(pr_obj, "releaseAction");
    cJSON *pd = cJSON_GetObjectItem(pr_obj, "pressDuration");
    cJSON *rd = cJSON_GetObjectItem(pr_obj, "releaseDuration");
    cJSON *wf = cJSON_GetObjectItem(pr_obj, "waitForFinish");

    pr->press_action                = cJSON_IsNumber(pa) ? (uint32_t)pa->valuedouble   : 0;
    pr->release_action              = cJSON_IsNumber(ra) ? (uint32_t)ra->valuedouble   : 0;
    pr->press_tap_release_delay_ms  = cJSON_IsNumber(pd) ? (uint32_t)pd->valuedouble   : 20;
    pr->release_tap_release_delay_ms= cJSON_IsNumber(rd) ? (uint32_t)rd->valuedouble   : 20;
    pr->wait_for_finish             = cJSON_IsTrue(wf);
    return true;
}

static bool deserialize_ma(cJSON *ma_obj, cfg_ckey_ma_t *ma) {
    if (!cJSON_IsObject(ma_obj)) return false;

    cJSON *ta  = cJSON_GetObjectItem(ma_obj, "tapAction");
    cJSON *dta = cJSON_GetObjectItem(ma_obj, "doubleTapAction");
    cJSON *ha  = cJSON_GetObjectItem(ma_obj, "holdAction");
    cJSON *dtt = cJSON_GetObjectItem(ma_obj, "doubleTapThreshold");
    cJSON *ht  = cJSON_GetObjectItem(ma_obj, "holdThreshold");
    cJSON *td  = cJSON_GetObjectItem(ma_obj, "tapDuration");
    cJSON *dtd = cJSON_GetObjectItem(ma_obj, "doubleTapDuration");
    cJSON *hd  = cJSON_GetObjectItem(ma_obj, "holdDuration");

    ma->tap_action                  = cJSON_IsNumber(ta)  ? (uint32_t)ta->valuedouble  : 0;
    ma->double_tap_action           = cJSON_IsNumber(dta) ? (uint32_t)dta->valuedouble : 0;
    ma->hold_action                 = cJSON_IsNumber(ha)  ? (uint32_t)ha->valuedouble  : 0;
    ma->double_tap_threshold_ms     = cJSON_IsNumber(dtt) ? (uint32_t)dtt->valuedouble : 300;
    ma->hold_threshold_ms           = cJSON_IsNumber(ht)  ? (uint32_t)ht->valuedouble  : 500;
    ma->tap_release_delay_ms        = cJSON_IsNumber(td)  ? (uint32_t)td->valuedouble  : 20;
    ma->double_tap_release_delay_ms = cJSON_IsNumber(dtd) ? (uint32_t)dtd->valuedouble : 20;
    ma->hold_release_delay_ms       = cJSON_IsNumber(hd)  ? (uint32_t)hd->valuedouble  : 20;
    return true;
}

bool ckeys_deserialize(cJSON *root, void *out_struct) {
    cfg_custom_key_t *ck = (cfg_custom_key_t *)out_struct;
    memset(ck, 0, sizeof(cfg_custom_key_t));

    /* The JSON may arrive as the object itself, or wrapped in an array. */
    cJSON *item = root;
    if (cJSON_IsArray(root) && cJSON_GetArraySize(root) > 0) {
        item = cJSON_GetArrayItem(root, 0);
    }

    cJSON *jid   = cJSON_GetObjectItem(item, "id");
    cJSON *jname = cJSON_GetObjectItem(item, "name");
    cJSON *jmode = cJSON_GetObjectItem(item, "mode");

    if (!cJSON_IsNumber(jid)) return false;
    ck->id = (uint16_t)jid->valueint;
    if (ck->id >= CFG_CKEYS_MAX_COUNT) return false;

    if (cJSON_IsString(jname)) {
        strncpy(ck->name, jname->valuestring, sizeof(ck->name) - 1);
    }

    ck->mode = cJSON_IsNumber(jmode) ? (cfg_ckey_mode_t)jmode->valueint : CKEY_MODE_PRESS_RELEASE;

    if (ck->mode == CKEY_MODE_PRESS_RELEASE) {
        cJSON *jpr = cJSON_GetObjectItem(item, "pr");
        deserialize_pr(jpr, &ck->rules.pr);
    } else {
        cJSON *jma = cJSON_GetObjectItem(item, "ma");
        deserialize_ma(jma, &ck->rules.ma);
    }

    return true;
}

/* ---------- Serialize helpers ---------- */

static cJSON *serialize_pr(const cfg_ckey_pr_t *pr) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "pressAction",     (double)pr->press_action);
    cJSON_AddNumberToObject(obj, "releaseAction",   (double)pr->release_action);
    cJSON_AddNumberToObject(obj, "pressDuration",   (double)pr->press_tap_release_delay_ms);
    cJSON_AddNumberToObject(obj, "releaseDuration", (double)pr->release_tap_release_delay_ms);
    cJSON_AddBoolToObject(obj, "waitForFinish",     pr->wait_for_finish);
    return obj;
}

static cJSON *serialize_ma(const cfg_ckey_ma_t *ma) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "tapAction",          (double)ma->tap_action);
    cJSON_AddNumberToObject(obj, "doubleTapAction",    (double)ma->double_tap_action);
    cJSON_AddNumberToObject(obj, "holdAction",         (double)ma->hold_action);
    cJSON_AddNumberToObject(obj, "doubleTapThreshold", (double)ma->double_tap_threshold_ms);
    cJSON_AddNumberToObject(obj, "holdThreshold",      (double)ma->hold_threshold_ms);
    cJSON_AddNumberToObject(obj, "tapDuration",        (double)ma->tap_release_delay_ms);
    cJSON_AddNumberToObject(obj, "doubleTapDuration",  (double)ma->double_tap_release_delay_ms);
    cJSON_AddNumberToObject(obj, "holdDuration",       (double)ma->hold_release_delay_ms);
    return obj;
}

static cJSON *serialize_single_ckey_to_json(const cfg_custom_key_t *ck) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "id",   ck->id);
    cJSON_AddStringToObject(obj, "name", ck->name);
    cJSON_AddNumberToObject(obj, "mode", (int)ck->mode);

    if (ck->mode == CKEY_MODE_PRESS_RELEASE) {
        cJSON_AddItemToObject(obj, "pr", serialize_pr(&ck->rules.pr));
    } else {
        cJSON_AddItemToObject(obj, "ma", serialize_ma(&ck->rules.ma));
    }
    return obj;
}

cJSON *ckeys_serialize(const void *in_struct) {
    /* Not used by the monolithic cfgmod pipeline — individual handlers do it. */
    (void)in_struct;
    return NULL;
}

/* ============================================================
   High-level helpers (outline, single, upsert, delete, load)
   ============================================================ */

cJSON *ckeys_serialize_outline(const cfg_ckey_index_t *idx) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON *arr = cJSON_CreateArray();

    for (uint16_t i = 0; i < CFG_CKEYS_MAX_COUNT; i++) {
        if (!(idx->active_mask & (1U << i))) continue;

        char key[12];
        snprintf(key, sizeof(key), "ck_%u", i);

        cfg_custom_key_t temp;
        size_t len = sizeof(temp);
        if (cfgmod_read_storage(CFGMOD_KIND_CKEY, key, &temp, &len) == ESP_OK && len == sizeof(temp)) {
            cJSON *entry = cJSON_CreateObject();
            cJSON_AddNumberToObject(entry, "id",   temp.id);
            cJSON_AddStringToObject(entry, "name", temp.name);
            cJSON_AddNumberToObject(entry, "mode", (int)temp.mode);
            cJSON_AddItemToArray(arr, entry);
        }
    }

    cJSON_AddItemToObject(root, "customKeys", arr);
    return root;
}

cJSON *ckeys_serialize_single(uint16_t id, const cfg_ckey_index_t *idx) {
    if (id < CFG_CKEYS_MAX_COUNT && (idx->active_mask & (1U << id))) {
        char key[12];
        snprintf(key, sizeof(key), "ck_%u", id);

        cfg_custom_key_t temp;
        size_t len = sizeof(temp);
        if (cfgmod_read_storage(CFGMOD_KIND_CKEY, key, &temp, &len) == ESP_OK && len == sizeof(temp)) {
            return serialize_single_ckey_to_json(&temp);
        }
    }

    /* Return an empty shell so the frontend knows the ID doesn't exist */
    cJSON *empty = cJSON_CreateObject();
    cJSON_AddNumberToObject(empty, "id",   id);
    cJSON_AddStringToObject(empty, "name", "");
    cJSON_AddNumberToObject(empty, "mode", 0);
    return empty;
}

esp_err_t ckeys_upsert_single(cJSON *ckey_json, cfg_ckey_index_t *idx) {
    cfg_custom_key_t temp;
    if (!ckeys_deserialize(ckey_json, &temp)) return ESP_ERR_INVALID_ARG;
    if (temp.id >= CFG_CKEYS_MAX_COUNT)        return ESP_ERR_INVALID_ARG;

    char key[12];
    snprintf(key, sizeof(key), "ck_%u", temp.id);

    esp_err_t err = cfgmod_write_storage(CFGMOD_KIND_CKEY, key, &temp, sizeof(temp));
    if (err == ESP_OK) {
        idx->active_mask |= (1U << temp.id);
        cfgmod_write_storage(CFGMOD_KIND_CKEY, "ck_idx", idx, sizeof(cfg_ckey_index_t));
        ESP_LOGI(TAG, "Upserted custom key %u ('%s')", temp.id, temp.name);
    }
    return err;
}

esp_err_t ckeys_delete_single(uint16_t id, cfg_ckey_index_t *idx) {
    if (id >= CFG_CKEYS_MAX_COUNT) return ESP_ERR_INVALID_ARG;

    idx->active_mask &= ~(1U << id);
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

    ESP_LOGI(TAG, "ckeys_load_all: active_mask=0x%08X", (unsigned)idx.active_mask);

    for (uint16_t i = 0; i < CFG_CKEYS_MAX_COUNT; i++) {
        if (!(idx.active_mask & (1U << i))) continue;

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

/* ============================================================
   Registration
   ============================================================ */

void cfg_custom_keys_register(cfgmod_on_update_fn update_fn) {
    /* Minimal registration: gives cfgmod_read/write_storage a valid kind slot.
       The actual GET/SET command handling is done in custom cfgmod.c blocks,
       identical to the macro pattern. */
    cfgmod_register_kind(CFGMOD_KIND_CKEY,
                         ckeys_default,
                         ckeys_deserialize,
                         ckeys_serialize,
                         update_fn,
                         sizeof(cfg_custom_key_t));
}
