#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"

#include "splitmod.h"      // split_state_t, split_role_t
#include "split_crypto.h"  // SPLIT_CRYPTO_KEY_SIZE

/* =========================================================================
 * Split session — central mutable state shared across split_* submodules.
 *
 * All sub-modules access this via thin accessors so the ownership of every
 * field is explicit. Mutations that need atomicity on a dual-core ESP32-S3
 * (sequence numbers, anti-replay window) go through critical-section-protected
 * helpers instead of raw reads/writes.
 * ========================================================================= */

void split_session_init(void);

/* ---- State machine ----------------------------------------------------- */
split_state_t split_session_get_state(void);
void          split_session_set_state(split_state_t s);

split_role_t  split_session_get_role(void);
void          split_session_set_role(split_role_t r);

/* ---- MAC addresses ----------------------------------------------------- */
const uint8_t *split_session_own_mac(void);
const uint8_t *split_session_peer_mac(void);
void           split_session_set_peer_mac(const uint8_t mac[6]);
void           split_session_clear_peer_mac(void);

/* ---- Metrics ----------------------------------------------------------- */
int8_t   split_session_get_rssi(void);
void     split_session_set_rssi(int8_t r);
uint16_t split_session_get_latency_us(void);
void     split_session_set_latency_us(uint16_t v);

/* ---- Sequence number (thread-safe) ------------------------------------- */
uint64_t split_session_next_seq(void);

/* ---- Anti-replay window ------------------------------------------------ */
/**
 * @brief Check an inbound sequence number and update the window.
 *        Returns true when the frame is in the forward half of the 16-bit
 *        sequence space, false when it is a duplicate or old.
 */
bool split_session_check_rx_seq(uint64_t seq);
void split_session_reset_rx_seq(void);

/* ---- Peer liveness ----------------------------------------------------- */
void       split_session_mark_peer_seen(void);
TickType_t split_session_peer_last_seen(void);

void split_session_set_link_stale(bool stale);
bool split_session_is_link_stale(void);

void       split_session_mark_connected_now(void);
TickType_t split_session_connected_at(void);

/* ---- Long-term session key (derived during pairing, kept in NVS) ------- */
/** @param key 16-byte AES-128 key, or NULL to zero the stored copy. */
void           split_session_set_stored_key(const uint8_t *key);
const uint8_t *split_session_stored_key(void);
