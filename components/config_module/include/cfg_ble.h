#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "nimble/ble.h"

#define CFG_BLE_MAX_PROFILES 9

typedef struct {
    uint8_t addr_type; // e.g. BLE_ADDR_PUBLIC or BLE_ADDR_RANDOM
    uint8_t val[6];    // 6-byte MAC address
    bool is_valid;
} cfg_ble_profile_t;

typedef struct {
    cfg_ble_profile_t profiles[CFG_BLE_MAX_PROFILES];
    uint8_t selected_profile; // 0 to 8
    bool ble_routing_enabled; // True if BLE_TG is ON
} cfg_ble_state_t;

extern cfg_ble_state_t g_cfg_ble_state;

/**
 * @brief Initialize BLE configuration and load it from storage if present.
 */
void cfg_ble_init(void);

/**
 * @brief Get the currently configured BLE state.
 */
const cfg_ble_state_t *cfg_ble_get_state(void);

/**
 * @brief Updates the BLE configuration and saves it to NVS.
 */
void cfg_ble_save_state(const cfg_ble_state_t *state);
