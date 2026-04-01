/**
 * @file mock_nvs.h
 * @brief Mock NVS (Non-Volatile Storage) for host testing.
 *
 * Provides an in-memory key-value store that mimics the ESP-IDF NVS API.
 * Supports blob read/write/erase per namespace+key pair.
 */
#pragma once

#include "mock_esp.h"
#include <string.h>
#include <stdlib.h>

/* ---- Types ---- */

typedef uint32_t nvs_handle_t;

typedef enum {
    NVS_READONLY  = 0,
    NVS_READWRITE = 1
} nvs_open_mode_t;

/* ---- In-memory NVS store ---- */

#define MOCK_NVS_MAX_ENTRIES 256
#define MOCK_NVS_MAX_KEY_LEN 16
#define MOCK_NVS_MAX_NS_LEN 16
#define MOCK_NVS_MAX_BLOB_SIZE 4096

typedef struct {
    char     ns[MOCK_NVS_MAX_NS_LEN];
    char     key[MOCK_NVS_MAX_KEY_LEN];
    uint8_t *data;
    size_t   len;
    bool     used;
} mock_nvs_entry_t;

static mock_nvs_entry_t _mock_nvs[MOCK_NVS_MAX_ENTRIES];
static char _mock_nvs_open_ns[MOCK_NVS_MAX_NS_LEN] = "";
static nvs_open_mode_t _mock_nvs_open_mode = NVS_READONLY;
static bool _mock_nvs_initialized = false;

/* ---- Lookup helper ---- */

static inline mock_nvs_entry_t *_nvs_find(const char *ns, const char *key) {
    for (int i = 0; i < MOCK_NVS_MAX_ENTRIES; i++) {
        if (_mock_nvs[i].used &&
            strcmp(_mock_nvs[i].ns, ns) == 0 &&
            strcmp(_mock_nvs[i].key, key) == 0) {
            return &_mock_nvs[i];
        }
    }
    return NULL;
}

static inline mock_nvs_entry_t *_nvs_alloc(const char *ns, const char *key) {
    mock_nvs_entry_t *e = _nvs_find(ns, key);
    if (e) return e;
    for (int i = 0; i < MOCK_NVS_MAX_ENTRIES; i++) {
        if (!_mock_nvs[i].used) {
            _mock_nvs[i].used = true;
            strncpy(_mock_nvs[i].ns, ns, MOCK_NVS_MAX_NS_LEN - 1);
            strncpy(_mock_nvs[i].key, key, MOCK_NVS_MAX_KEY_LEN - 1);
            return &_mock_nvs[i];
        }
    }
    return NULL;
}

/* ---- NVS API ---- */

static inline esp_err_t nvs_flash_init(void) {
    _mock_nvs_initialized = true;
    return ESP_OK;
}

static inline esp_err_t nvs_flash_erase(void) {
    for (int i = 0; i < MOCK_NVS_MAX_ENTRIES; i++) {
        if (_mock_nvs[i].data) { free(_mock_nvs[i].data); _mock_nvs[i].data = NULL; }
        _mock_nvs[i].used = false;
        _mock_nvs[i].len = 0;
    }
    return ESP_OK;
}

static inline esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *out) {
    strncpy(_mock_nvs_open_ns, ns, MOCK_NVS_MAX_NS_LEN - 1);
    _mock_nvs_open_mode = mode;
    *out = 1;
    return ESP_OK;
}

static inline void nvs_close(nvs_handle_t handle) {
    (void)handle;
}

static inline esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key,
                                      void *out, size_t *length) {
    (void)handle;
    mock_nvs_entry_t *e = _nvs_find(_mock_nvs_open_ns, key);
    if (!e || !e->data) return ESP_ERR_NVS_NOT_FOUND;

    if (out == NULL) {
        /* Size query */
        *length = e->len;
        return ESP_OK;
    }
    if (*length < e->len) return ESP_ERR_INVALID_SIZE;
    memcpy(out, e->data, e->len);
    *length = e->len;
    return ESP_OK;
}

static inline esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key,
                                      const void *data, size_t len) {
    (void)handle;
    if (_mock_nvs_open_mode != NVS_READWRITE) return ESP_FAIL;
    mock_nvs_entry_t *e = _nvs_alloc(_mock_nvs_open_ns, key);
    if (!e) return ESP_ERR_NO_MEM;
    if (e->data) free(e->data);
    e->data = (uint8_t *)malloc(len);
    if (!e->data) return ESP_ERR_NO_MEM;
    memcpy(e->data, data, len);
    e->len = len;
    return ESP_OK;
}

static inline esp_err_t nvs_commit(nvs_handle_t handle) {
    (void)handle;
    return ESP_OK;
}

static inline esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
    (void)handle;
    mock_nvs_entry_t *e = _nvs_find(_mock_nvs_open_ns, key);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    if (e->data) { free(e->data); e->data = NULL; }
    e->used = false;
    e->len = 0;
    return ESP_OK;
}

/* ---- Test helpers ---- */

static inline void mock_nvs_reset(void) {
    for (int i = 0; i < MOCK_NVS_MAX_ENTRIES; i++) {
        if (_mock_nvs[i].data) { free(_mock_nvs[i].data); _mock_nvs[i].data = NULL; }
        _mock_nvs[i].used = false;
        _mock_nvs[i].len = 0;
    }
    _mock_nvs_initialized = false;
    memset(_mock_nvs_open_ns, 0, sizeof(_mock_nvs_open_ns));
}

/** Inject a blob into the mock NVS (for test setup). */
static inline void mock_nvs_inject(const char *ns, const char *key,
                                    const void *data, size_t len) {
    char prev_ns[MOCK_NVS_MAX_NS_LEN];
    nvs_open_mode_t prev_mode = _mock_nvs_open_mode;
    strncpy(prev_ns, _mock_nvs_open_ns, MOCK_NVS_MAX_NS_LEN);

    strncpy(_mock_nvs_open_ns, ns, MOCK_NVS_MAX_NS_LEN - 1);
    _mock_nvs_open_mode = NVS_READWRITE;
    nvs_set_blob(1, key, data, len);

    strncpy(_mock_nvs_open_ns, prev_ns, MOCK_NVS_MAX_NS_LEN);
    _mock_nvs_open_mode = prev_mode;
}
