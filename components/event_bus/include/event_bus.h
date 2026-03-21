#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_event.h"

/* =========================================================================
 * Event Bases
 * ========================================================================= */

ESP_EVENT_DECLARE_BASE(KB_EVENTS);
ESP_EVENT_DECLARE_BASE(BLE_EVENTS);
ESP_EVENT_DECLARE_BASE(CONFIG_EVENTS);

/* =========================================================================
 * KB_EVENTS IDs
 * ========================================================================= */

typedef enum {
    KB_EVENT_SYSTEM_ACTION = 0, // payload: kb_sys_action_event_t
    KB_EVENT_LED_STATE,         // payload: uint8_t (HID LED bitmask)
} kb_event_id_t;

/* HID LED bitmask constants (mirrors kb_state.h without creating a dependency) */
#define KB_LED_BIT_NUM_LOCK    (1 << 0)
#define KB_LED_BIT_CAPS_LOCK   (1 << 1)
#define KB_LED_BIT_SCROLL_LOCK (1 << 2)

/* =========================================================================
 * BLE_EVENTS IDs
 * ========================================================================= */

typedef enum {
    BLE_EVENT_ROUTING_CHANGED = 0,   // payload: bool active
    BLE_EVENT_PROFILE_CONNECTED,     // payload: int profile_idx
    BLE_EVENT_PROFILE_DISCONNECTED,  // payload: int profile_idx
    BLE_EVENT_PAIRING_STARTED,       // payload: int profile_idx
    BLE_EVENT_PAIRING_COMPLETE,      // payload: ble_pairing_result_t
    BLE_EVENT_PAIRING_FAILED,        // payload: int profile_idx
    BLE_EVENT_PAIRING_TIMEOUT,       // payload: int profile_idx
} ble_event_id_t;

/* =========================================================================
 * CONFIG_EVENTS IDs
 * ========================================================================= */

typedef enum {
    CONFIG_EVENT_KIND_UPDATED = 0,   // payload: config_update_event_t
} config_event_id_t;

/* =========================================================================
 * Payload structs
 * ========================================================================= */

/**
 * @brief Payload for KB_EVENT_SYSTEM_ACTION.
 * The `event` field is kb_action_ev_t stored as int to avoid including
 * kb_system_action.h in this header.
 */
typedef struct {
    uint16_t action_code;
    int      event;
} kb_sys_action_event_t;

/** @brief Payload for BLE_EVENT_PAIRING_COMPLETE. */
typedef struct {
    int     profile_idx;
    uint8_t addr_type;
    uint8_t addr[6];
} ble_pairing_result_t;

/**
 * @brief Payload for CONFIG_EVENT_KIND_UPDATED.
 * The `kind` field is cfgmod_kind_t stored as uint8_t to avoid including
 * cfgmod.h in this header.
 */
typedef struct {
    uint8_t kind;
    char    key[16]; // CFGMOD_MAX_KEY_LEN is 12; 16 is safe
} config_update_event_t;

/* =========================================================================
 * Init
 * ========================================================================= */

/**
 * @brief Create the default ESP event loop.
 * Must be called before any module calls esp_event_handler_register() or
 * esp_event_post(). Call this as the very first step in init_procedure().
 */
esp_err_t event_bus_init(void);
