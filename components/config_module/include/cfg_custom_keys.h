#pragma once


#include "cfgmod.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CFG_CKEYS_MAX_COUNT 120

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
 *
 * If press_sustain is true, the press action is held for as long as the
 * physical key is held instead of using a fixed tap duration.
 */
typedef struct {
    uint32_t press_action;
    uint32_t release_action;
    uint32_t press_tap_release_delay_ms;
    uint32_t release_tap_release_delay_ms;
    uint8_t  wait_for_finish;
    uint8_t  press_sustain;
    uint8_t  reserved[2];
} cfg_ckey_pr_t;

/**
 * @brief MultiAction mode rule set.
 *
 * Tap, double-tap and hold actions are determined by timing thresholds.
 * The *_release_delay_ms fields control the virtual tap width of each resolved action.
 *
 * If hold_sustain is true, the hold action is held for as long as the
 * physical key is held instead of using a fixed tap duration.
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
    uint8_t  hold_sustain;
    uint8_t  reserved[3];
} cfg_ckey_ma_t;

/**
 * @brief Full Custom Key configuration struct — stored individually in NVS.
 */
typedef struct {
    union {
        cfg_ckey_pr_t pr;
        cfg_ckey_ma_t ma;
    } rules;
    uint16_t        id;
    uint8_t         mode;
    uint8_t         reserved[1];
    char            name[32];
} cfg_custom_key_t;

_Static_assert(sizeof(cfg_ckey_pr_t) == 20, "cfg_ckey_pr_t size mismatch");
_Static_assert(sizeof(cfg_ckey_ma_t) == 36, "cfg_ckey_ma_t size mismatch");
_Static_assert(sizeof(cfg_custom_key_t) == 72, "cfg_custom_key_t size mismatch");
_Static_assert(offsetof(cfg_custom_key_t, name) == 40, "offset mismatch");

/**
 * @brief Lightweight index stored in NVS to track which Custom Key IDs exist.
 * Bit N in mask = 1 means Custom Key N is present. 15 bytes = 120 bits exactly.
 */
typedef struct {
    uint8_t mask[15];
} cfg_ckey_index_t;

/* ---- cfgmod handler callbacks ---- */
void   ckeys_default(void *out_struct);

/* ---- High-level helpers ---- */



/** Remove a single custom key by ID. Updates idx in NVS. */
esp_err_t ckeys_delete_single(uint16_t id, cfg_ckey_index_t *idx);

/**
 * Load all active custom keys from NVS into caller-supplied array.
 * @param out_arr   Array of at least CFG_CKEYS_MAX_COUNT elements.
 * @param out_count Number of entries written.
 */
esp_err_t ckeys_load_all(cfg_custom_key_t *out_arr, size_t *out_count);

/** Register the Custom Keys kind with cfgmod. Call once from cfg_init(). */
void cfg_custom_keys_register(cfgmod_on_update_fn update_fn);
