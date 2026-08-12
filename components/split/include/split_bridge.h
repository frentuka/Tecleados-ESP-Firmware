#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_event.h"

#include "splitmod.h"   // split_role_t

/* =========================================================================
 * Split bridge — glue between the split link and the rest of the firmware.
 *
 *   * Applies BLE and keyboard-manager routing for the current role.
 *   * Cleans up routing on disconnect so either half runs standalone.
 *   * Forwards BLE configurator commands between halves (slave → master).
 *   * Pushes the master's live BLE state to the slave after every change.
 * ========================================================================= */

/* ---- Role routing ------------------------------------------------------ */

/** @brief Apply keyboard-manager + BLE routing for the given role. */
void split_bridge_apply_routing_for_role(split_role_t role);

/** @brief Reset routing to standalone mode (peer lost / unpaired). */
void split_bridge_reset_routing_standalone(void);

/* ---- BLE proxy --------------------------------------------------------- */

/** @brief Execute a BLE configurator command locally (master / standalone). */
void split_bridge_execute_ble_cmd(uint8_t cmd, uint8_t arg);

/** @brief Master-only: send current BLE state to the slave. No-op otherwise. */
void split_bridge_send_ble_status_to_slave(void);

/** @brief BLE_EVENTS handler: master broadcasts status on every change. */
void split_bridge_on_ble_event(void *arg, esp_event_base_t base,
                                int32_t event_id, void *data);

#include "comm_transport.h"
/** @brief MODULE_BLE callback: run locally or forward to master. */
bool split_bridge_ble_usb_callback(comm_transport_t source, uint8_t *data, uint16_t data_len);
