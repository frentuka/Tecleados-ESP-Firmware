#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "splitmod.h"   // split_role_t

/* =========================================================================
 * NVS persistence
 * ========================================================================= */

/**
 * @brief Persist the current role to NVS.  Call on every role assignment
 *        (MASTER or SLAVE).  SPLIT_ROLE_NONE is silently ignored.
 */
void split_role_save_last(split_role_t role);

/**
 * @brief Load the last persisted role from NVS.
 * @return SPLIT_ROLE_MASTER, SPLIT_ROLE_SLAVE, or SPLIT_ROLE_NONE if no
 *         value has been saved yet.
 */
split_role_t split_role_load_last(void);

/* =========================================================================
 * Role decision logic
 *
 * preferred_role encoding matches split_role_t:
 *   0 = auto (no preference / SPLIT_ROLE_NONE)
 *   1 = prefer MASTER
 *   2 = prefer SLAVE
 *
 * Decision priority (highest → lowest):
 *   1. Explicit user preference (own_pref / peer_pref from pair config)
 *   2. USB host connection active
 *   3. BLE host connection active
 *   4. Last persisted role from NVS
 *   5. Higher MAC address → MASTER
 * ========================================================================= */

/**
 * @brief Decide this device's role given full connection context.
 *
 * @param own_mac           This device's MAC
 * @param peer_mac          Peer's MAC
 * @param own_pref          Explicit preference (0=auto, 1=master, 2=slave)
 * @param peer_pref         Peer's explicit preference from ROLE_NEGOTIATE
 * @param own_usb_connected 1 if this device has an active USB host connection
 * @param own_ble_connected_bitmap Bitmap of own connected host profiles
 * @param own_has_unsynced_ble   1 if this device has unsynced bonds
 * @param own_last_role     Last role persisted in own NVS
 * @param peer_usb_connected 1 if peer has an active USB host connection
 * @param peer_ble_connected_bitmap Bitmap of peer connected host profiles
 * @param peer_has_unsynced_ble  1 if peer has unsynced bonds
 * @param peer_last_role    Last role reported by peer via ROLE_NEGOTIATE
 * @return SPLIT_ROLE_MASTER or SPLIT_ROLE_SLAVE
 */
split_role_t split_role_decide(const uint8_t own_mac[6],
                                const uint8_t peer_mac[6],
                                uint8_t own_pref,
                                uint8_t peer_pref,
                                uint8_t own_usb_connected,
                                uint16_t own_ble_connected_bitmap,
                                uint8_t own_has_unsynced_ble,
                                split_role_t own_last_role,
                                uint8_t peer_usb_connected,
                                uint16_t peer_ble_connected_bitmap,
                                uint8_t peer_has_unsynced_ble,
                                split_role_t peer_last_role);

/**
 * @brief Parse an incoming ROLE_NEGOTIATE payload and decide our role.
 *
 * @param src_mac            Source MAC of the ROLE_NEGOTIATE message
 * @param payload            Raw payload bytes (split_role_negotiate_payload_t)
 * @param len                Payload length
 * @param own_mac            Our MAC
 * @param own_pref           Our preferred_role from pairing config
 * @param own_usb_connected  1 if we have an active USB host connection
 * @param own_ble_connected_bitmap Bitmap of our connected host profiles
 * @param own_has_unsynced_ble   1 if we have unsynced bonds
 * @param own_last_role      Our last role loaded from NVS
 * @param out_role           Set to the decided role on success
 * @return ESP_OK, or ESP_ERR_INVALID_SIZE if payload is too short.
 */
esp_err_t split_role_on_negotiate(const uint8_t *src_mac,
                                   const uint8_t *payload, size_t len,
                                   const uint8_t own_mac[6],
                                   uint8_t own_pref,
                                   uint8_t own_usb_connected,
                                   uint16_t own_ble_connected_bitmap,
                                   uint8_t own_has_unsynced_ble,
                                   split_role_t own_last_role,
                                   split_role_t *out_role);

/**
 * @brief Parse an incoming ROLE_SWAP_REQ.
 *        The caller should respond with ROLE_SWAP_ACK and then swap roles.
 * @param current_role  Our current role
 * @param out_new_role  Set to the new role we should take after swap
 */
void split_role_on_swap_req(split_role_t current_role,
                             split_role_t *out_new_role);

/**
 * @brief Apply a role swap upon receiving ROLE_SWAP_ACK.
 * @param current_role  Our current role
 * @param out_new_role  Set to the new role after swap
 */
void split_role_on_swap_ack(split_role_t current_role,
                             split_role_t *out_new_role);
