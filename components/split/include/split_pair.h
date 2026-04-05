#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"
#include "split_crypto.h"

/* =========================================================================
 * Persistent pairing data (stored in NVS via cfgmod)
 * ========================================================================= */

typedef struct __attribute__((packed)) split_pair_data {
    uint8_t  peer_mac[6];                       // MAC of the paired peer
    uint8_t  shared_key[SPLIT_CRYPTO_KEY_SIZE]; // AES-128 derived from X25519
    uint8_t  preferred_role;                    // 0=auto, 1=prefer master, 2=prefer slave
    uint8_t  channel;                           // WiFi channel used for ESP-NOW
    uint8_t  paired;                            // 1 if valid pairing data exists
    uint8_t  _pad[3];                           // alignment
} split_pair_data_t;

/* =========================================================================
 * Pairing session phase (in-RAM only, not persisted)
 * ========================================================================= */

typedef enum {
    PAIR_PHASE_IDLE = 0,
    PAIR_PHASE_BROADCASTING,    // Sending DISCOVERY beacons, awaiting PAIR_REQUEST
    PAIR_PHASE_SENT_REQUEST,    // Sent PAIR_REQUEST, awaiting PAIR_RESPONSE
    PAIR_PHASE_COMPLETE,
    PAIR_PHASE_FAILED,
} split_pair_phase_t;

/* =========================================================================
 * API
 * ========================================================================= */

/**
 * @brief Load pairing data from NVS. Call once after cfgmod is initialised.
 */
esp_err_t split_pair_init(void);

/**
 * @brief Begin a pairing session (enter BROADCASTING phase).
 *        Clears any in-progress ephemeral ECDH state.
 */
esp_err_t split_pair_start(void);

/**
 * @brief Cancel the active pairing session and return to IDLE.
 */
void split_pair_cancel(void);

/**
 * @brief Build a DISCOVERY beacon payload.
 * @return Payload length, or 0 on error.
 */
size_t split_pair_build_discovery(uint8_t *out, size_t out_max,
                                   const uint8_t own_mac[6],
                                   uint8_t preferred_role);

/**
 * @brief Handle an incoming DISCOVERY beacon (we become initiator → send PAIR_REQUEST).
 *        Only acts when phase == PAIR_PHASE_BROADCASTING.
 */
esp_err_t split_pair_on_discovery(const uint8_t *src_mac,
                                   const uint8_t *payload, size_t len,
                                   const uint8_t own_mac[6]);

/**
 * @brief Handle an incoming PAIR_REQUEST (we become responder → send PAIR_RESPONSE + derive key).
 *        Also resolves the race condition where both sides sent PAIR_REQUEST simultaneously:
 *        the device with the higher MAC becomes the responder.
 *
 * @return ESP_OK when pairing is complete on the responder side.
 *         ESP_ERR_INVALID_STATE if we should wait (lower MAC in race condition).
 */
esp_err_t split_pair_on_pair_request(const uint8_t *src_mac,
                                      const uint8_t *payload, size_t len,
                                      const uint8_t own_mac[6]);

/**
 * @brief Handle an incoming PAIR_RESPONSE (initiator finalises pairing → derive key).
 *
 * @return ESP_OK when pairing is complete on the initiator side.
 */
esp_err_t split_pair_on_pair_response(const uint8_t *src_mac,
                                       const uint8_t *payload, size_t len);

/** @brief Persist current pairing data to NVS. */
esp_err_t split_pair_save(void);

/** @brief Erase pairing data from NVS and reset to unpaired state. */
esp_err_t split_pair_clear(void);

/** @brief Copy current pairing data. Returns false if not paired. */
bool split_pair_get_data(split_pair_data_t *out);

/** @brief Returns true if a valid pairing exists. */
bool split_pair_is_paired(void);

/** @brief Returns the current in-RAM pairing session phase. */
split_pair_phase_t split_pair_get_phase(void);
