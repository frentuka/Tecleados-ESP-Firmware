/**
 * @file cfgmod.h
 * @brief Shim — stubs for cfgmod API used by cfg_layouts.c and statusmod.c.
 *
 * Provides controllable mock behavior for NVS read/write so tests can
 * inject layer data or simulate empty storage.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cJSON.h"
#include "mocks/mock_esp.h"

/* ---- Kind enum (mirrors production) ---- */

#ifndef _TH_CFGMOD_KIND_T
#define _TH_CFGMOD_KIND_T
typedef enum {
    CFGMOD_KIND_LAYOUT = 0,
    CFGMOD_KIND_MACRO,
    CFGMOD_KIND_CONNECTION,
    CFGMOD_KIND_SYSTEM,
    CFGMOD_KIND_PHYSICAL,
    CFGMOD_KIND_CKEY,
    CFGMOD_KIND_MAX
} cfgmod_kind_t;
#endif

/* ---- Callback signatures ---- */

typedef void   (*cfgmod_default_fn)(void *out_struct);
typedef bool   (*cfgmod_deserialize_fn)(cJSON *root, void *out_struct);
typedef cJSON *(*cfgmod_serialize_fn)(const void *in_struct);
typedef void   (*cfgmod_on_update_fn)(const char *key);

/* ---- Mock NVS storage ---- */

#define MOCK_NVS_MAX_ENTRIES 16
#define MOCK_NVS_MAX_SIZE   4096

typedef struct {
    cfgmod_kind_t kind;
    char          key[16];
    uint8_t       data[MOCK_NVS_MAX_SIZE];
    size_t        len;
    bool          valid;
} mock_nvs_entry_t;

static mock_nvs_entry_t _mock_nvs_store[MOCK_NVS_MAX_ENTRIES];
static int _mock_nvs_count = 0;

static inline void mock_nvs_reset(void) {
    memset(_mock_nvs_store, 0, sizeof(_mock_nvs_store));
    _mock_nvs_count = 0;
}

static inline void mock_nvs_inject(cfgmod_kind_t kind, const char *key,
                                    const void *data, size_t len) {
    if (_mock_nvs_count >= MOCK_NVS_MAX_ENTRIES) return;
    mock_nvs_entry_t *e = &_mock_nvs_store[_mock_nvs_count++];
    e->kind = kind;
    strncpy(e->key, key, 15);
    e->key[15] = '\0';
    if (len <= MOCK_NVS_MAX_SIZE) {
        memcpy(e->data, data, len);
        e->len = len;
    }
    e->valid = true;
}

/* ---- Stubbed cfgmod API ---- */

static inline esp_err_t cfgmod_register_kind(cfgmod_kind_t kind,
                                              cfgmod_default_fn def_fn,
                                              cfgmod_deserialize_fn des_fn,
                                              cfgmod_serialize_fn ser_fn,
                                              cfgmod_on_update_fn update_fn,
                                              size_t struct_size) {
    (void)kind; (void)def_fn; (void)des_fn; (void)ser_fn;
    (void)update_fn; (void)struct_size;
    return ESP_OK;
}

static inline esp_err_t cfgmod_get_config(cfgmod_kind_t kind, const char *key,
                                           void *out_struct) {
    for (int i = 0; i < _mock_nvs_count; i++) {
        if (_mock_nvs_store[i].valid &&
            _mock_nvs_store[i].kind == kind &&
            strcmp(_mock_nvs_store[i].key, key) == 0) {
            memcpy(out_struct, _mock_nvs_store[i].data, _mock_nvs_store[i].len);
            return ESP_OK;
        }
    }
    return ESP_ERR_NVS_NOT_FOUND;
}

static inline esp_err_t cfgmod_set_config(cfgmod_kind_t kind, const char *key,
                                           const void *in_struct) {
    /* Find existing or add new */
    for (int i = 0; i < _mock_nvs_count; i++) {
        if (_mock_nvs_store[i].valid &&
            _mock_nvs_store[i].kind == kind &&
            strcmp(_mock_nvs_store[i].key, key) == 0) {
            /* Size is unknown here — use MOCK_NVS_MAX_SIZE as upper bound.
               Tests should use mock_nvs_inject for controlled setups. */
            return ESP_OK;
        }
    }
    return ESP_OK;
}

static inline esp_err_t cfgmod_read_storage(cfgmod_kind_t kind, const char *key,
                                             void *out_buf, size_t *inout_len) {
    for (int i = 0; i < _mock_nvs_count; i++) {
        if (_mock_nvs_store[i].valid &&
            _mock_nvs_store[i].kind == kind &&
            strcmp(_mock_nvs_store[i].key, key) == 0) {
            size_t copy_len = _mock_nvs_store[i].len;
            if (copy_len > *inout_len) copy_len = *inout_len;
            memcpy(out_buf, _mock_nvs_store[i].data, copy_len);
            *inout_len = _mock_nvs_store[i].len;
            return ESP_OK;
        }
    }
    return ESP_ERR_NVS_NOT_FOUND;
}

static inline esp_err_t cfgmod_write_storage(cfgmod_kind_t kind, const char *key,
                                              const void *data, size_t len) {
    mock_nvs_inject(kind, key, data, len);
    return ESP_OK;
}

static inline bool cfg_is_init(void) { return true; }
