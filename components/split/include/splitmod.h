#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* =========================================================================
 * Types
 * ========================================================================= */

typedef enum split_role : uint8_t {
    SPLIT_ROLE_NONE   = 0,   // Split not active / standalone
    SPLIT_ROLE_MASTER = 1,   // This half produces HID reports
    SPLIT_ROLE_SLAVE  = 2,   // This half forwards key states to master
} split_role_t;

typedef enum split_state : uint8_t {
    SPLIT_STATE_DISABLED     = 0,  // Split feature turned off
    SPLIT_STATE_IDLE         = 1,  // Enabled but not connected
    SPLIT_STATE_PAIRING      = 2,  // In pairing discovery mode
    SPLIT_STATE_CONNECTING   = 3,  // Paired, negotiating roles
    SPLIT_STATE_CONNECTED    = 4,  // Fully operational
    SPLIT_STATE_DISCONNECTED = 5,  // Was connected, link lost — reconnecting
} split_state_t;

typedef struct split_status {
    split_state_t state;
    split_role_t  role;
    uint8_t       peer_mac[6];     // Paired peer MAC (zeroed if unpaired)
    int8_t        rssi;            // Last known RSSI (dBm), 0 if unknown
    uint16_t      latency_us;      // Last measured round-trip latency
} split_status_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/**
 * @brief Initialize the split keyboard module.
 *
 * Sets up ESP-NOW transport, loads pairing data from NVS, and creates the
 * split management task. If split is enabled and pairing data exists, it
 * begins attempting to connect to the paired peer.
 *
 * Must be called after event_bus_init() and cfg_init().
 */
esp_err_t splitmod_init(void);

/**
 * @brief Deinitialize the split module and release resources.
 */
esp_err_t splitmod_deinit(void);

/* =========================================================================
 * Pairing
 * ========================================================================= */

/**
 * @brief Enter pairing mode. Broadcasts discovery beacons and listens for
 *        a peer. Times out after timeout_ms (0 = default 30 s).
 */
esp_err_t splitmod_start_pairing(uint32_t timeout_ms);

/**
 * @brief Cancel an in-progress pairing attempt.
 */
esp_err_t splitmod_cancel_pairing(void);

/**
 * @brief Erase pairing data and disconnect from peer.
 */
esp_err_t splitmod_unpair(void);

/* =========================================================================
 * Role
 * ========================================================================= */

/**
 * @brief Request a role swap with the connected peer.
 *        Returns ESP_ERR_INVALID_STATE if not connected.
 */
esp_err_t splitmod_request_role_swap(void);

/* =========================================================================
 * Status
 * ========================================================================= */

/**
 * @brief Get current split module status (lock-free snapshot).
 */
split_status_t splitmod_get_status(void);

/**
 * @brief Check if split mode is enabled in config.
 */
bool splitmod_is_enabled(void);

/**
 * @brief Check if the peer is currently connected.
 */
bool splitmod_is_connected(void);

/**
 * @brief Check if the split link is currently stale (no heartbeat for >1.5s).
 */
bool splitmod_is_link_stale(void);

/**
 * @brief Get the current role of this device.
 */
split_role_t splitmod_get_role(void);
