#pragma once

#include "cJSON.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CFG_CKEYS_MAX_COUNT 32

/**
 * @brief Mode of a Custom Key.
 *
 * CKEY_MODE_PRESS_RELEASE: fires press_action on key-down, release_action on key-up.
 * CKEY_MODE_MULTI_ACTION:  fires tap/double-tap/hold actions based on timing.
 */
typedef enum {
    CKEY_MODE_PRESS_RELEASE = 0,
    CKEY_MODE_MULTI_ACTION  = 1
} cfg_ckey_mode_t;

/**
 * @brief PressRelease mode rule set.
 *
 * Each action is an action code (HID, System, Macro, CKey range).
 * The *_tap_release_delay_ms fields control how long the virtual key is
 * held before releasing it (i.e. the "tap width").  Set to 0 to skip the delay.
 */
typedef struct {
    uint32_t press_action;
    uint32_t release_action;
    uint32_t press_tap_release_delay_ms;
    uint32_t release_tap_release_delay_ms;
    bool     wait_for_finish;
} cfg_ckey_pr_t;

/**
 * @brief MultiAction mode rule set.
 *
 * Tap, double-tap and hold actions are determined by timing thresholds.
 * The *_release_delay_ms fields control the virtual tap width of each resolved action.
 */
typedef struct {
    uint32_t tap_action;
    uint32_t double_tap_action;
    uint32_t hold_action;
    uint32_t double_tap_threshold_ms;
    uint32_t hold_threshold_ms;
    uint32_t tap_release_delay_ms;
    uint32_t double_tap_release_delay_ms;
    uint32_t hold_release_delay_ms;
} cfg_ckey_ma_t;

/**
 * @brief Full Custom Key configuration struct — stored individually in NVS.
 */
typedef struct {
    uint16_t        id;
    char            name[32];
    cfg_ckey_mode_t mode;
    union {
        cfg_ckey_pr_t pr;
        cfg_ckey_ma_t ma;
    } rules;
} cfg_custom_key_t;

/**
 * @brief Lightweight index stored in NVS to track which Custom Key IDs exist.
 * Bit N in active_mask = 1 means Custom Key N is present.
 */
typedef struct {
    uint32_t active_mask;
} cfg_ckey_index_t;

/* ---- cfgmod handler callbacks ---- */
void   ckeys_default(void *out_struct);
bool   ckeys_deserialize(cJSON *root, void *out_struct);
cJSON *ckeys_serialize(const void *in_struct);

/* ---- High-level helpers ---- */

/** Serialize IDs + names + mode for every active custom key. */
cJSON *ckeys_serialize_outline(const cfg_ckey_index_t *idx);

/** Serialize a full single custom key by ID. */
cJSON *ckeys_serialize_single(uint16_t id, const cfg_ckey_index_t *idx);

/** Create or update a single custom key from JSON. Updates idx in NVS. */
esp_err_t ckeys_upsert_single(cJSON *ckey_json, cfg_ckey_index_t *idx);

/** Remove a single custom key by ID. Updates idx in NVS. */
esp_err_t ckeys_delete_single(uint16_t id, cfg_ckey_index_t *idx);

/**
 * Load all active custom keys from NVS into caller-supplied array.
 * @param out_arr   Array of at least CFG_CKEYS_MAX_COUNT elements.
 * @param out_count Number of entries written.
 */
esp_err_t ckeys_load_all(cfg_custom_key_t *out_arr, size_t *out_count);

#include "cfgmod.h"

/** Register the Custom Keys kind with cfgmod. Call once from cfg_init(). */
void cfg_custom_keys_register(cfgmod_on_update_fn update_fn);
