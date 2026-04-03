#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "splitmod.h"   // split_role_t

/* =========================================================================
 * Role decision logic — stateless pure functions
 *
 * preferred_role encoding matches split_role_t:
 *   0 = auto (no preference)
 *   1 = prefer MASTER
 *   2 = prefer SLAVE
 * ========================================================================= */

/**
 * @brief Decide this device's role given both sides' preferences and MACs.
 *
 * Rules (in priority order):
 *   1. One side explicitly prefers MASTER, the other doesn't  → MASTER wins.
 *   2. One side explicitly prefers SLAVE,  the other doesn't  → SLAVE wins.
 *   3. Both have same (or conflicting explicit) preference     → higher MAC = MASTER.
 *
 * @param own_mac   This device's MAC
 * @param peer_mac  Peer's MAC
 * @param own_pref  This device's preferred_role (0/1/2)
 * @param peer_pref Peer's preferred_role from ROLE_NEGOTIATE message
 * @return SPLIT_ROLE_MASTER or SPLIT_ROLE_SLAVE
 */
split_role_t split_role_decide(const uint8_t own_mac[6],
                                const uint8_t peer_mac[6],
                                uint8_t own_pref,
                                uint8_t peer_pref);

/**
 * @brief Parse an incoming ROLE_NEGOTIATE payload and decide our role.
 *
 * @param src_mac   Source MAC of the ROLE_NEGOTIATE message
 * @param payload   Raw payload bytes
 * @param len       Payload length
 * @param own_mac   Our MAC
 * @param own_pref  Our preferred_role from pairing config
 * @param out_role  Set to the decided role on success
 * @return ESP_OK, or ESP_ERR_INVALID_SIZE if payload is too short.
 */
esp_err_t split_role_on_negotiate(const uint8_t *src_mac,
                                   const uint8_t *payload, size_t len,
                                   const uint8_t own_mac[6],
                                   uint8_t own_pref,
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
