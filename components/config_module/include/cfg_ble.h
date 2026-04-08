#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "nimble/ble.h"

#define CFG_BLE_MAX_PROFILES 9

typedef struct {
    uint8_t addr_type; // e.g. BLE_ADDR_PUBLIC or BLE_ADDR_RANDOM
    uint8_t val[6];    // 6-byte MAC address
    bool is_valid;
    uint8_t addr_nonce; // Incremented on each re-pair to rotate MAC
} cfg_ble_profile_t;

typedef struct {
    cfg_ble_profile_t profiles[CFG_BLE_MAX_PROFILES];
    uint8_t selected_profile; // 0 to 8
    bool ble_routing_enabled; // True if BLE_TG is ON
    uint16_t sync_version;    // Version counter to resolve split-brain conflicts
} cfg_ble_state_t;

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

/**
 * @brief Updates the selected profile in-memory only.
 */
void cfg_ble_set_selected_profile(uint8_t index);


/**
 * @brief Reload the in-memory BLE config from NVS.
 *
 * Use this when NVS may have been updated externally (e.g. by the split config
 * sync while this device was operating as a slave) without going through the
 * normal cfgmod_set_config path that would have triggered on_ble_updated.
 */
void cfg_ble_reload(void);

/**
 * @brief Serializes the entire nimble_bond NVS namespace into a single dynamically allocated buffer.
 */
esp_err_t cfg_ble_bond_read_all(void *out_buf, size_t *inout_len);

/**
 * @brief Clears the nimble_bond NVS namespace and deserializes the payload into individual elements.
 */
esp_err_t cfg_ble_bond_write_all(const void *data, size_t len);
