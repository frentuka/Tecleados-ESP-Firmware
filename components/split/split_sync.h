#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "split_protocol.h"   // SPLIT_MATRIX_BYTES

/* =========================================================================
 * Remote matrix (MASTER side — receives key state from SLAVE)
 * ========================================================================= */

/**
 * @brief Handle SPLIT_MSG_KEY_STATE_FULL received from slave.
 *        Replaces the entire remote matrix snapshot.
 */
esp_err_t split_sync_on_key_state_full(const uint8_t *payload, size_t len);

/**
 * @brief Handle SPLIT_MSG_KEY_STATE_DELTA received from slave.
 *        Applies the incremental update to the remote matrix.
 */
esp_err_t split_sync_on_key_state_delta(const uint8_t *payload, size_t len);

/**
 * @brief Copy the current remote matrix into @p out_bitmap (SPLIT_MATRIX_BYTES).
 *        Thread-safe.
 */
void split_sync_get_remote_matrix(uint8_t *out_bitmap);

/**
 * @brief Zero out the remote matrix (call on peer disconnect).
 */
void split_sync_clear_remote_matrix(void);

/**
 * @brief Returns true (once) if the remote matrix changed since the last call.
 *        Clears the flag atomically.
 */
bool split_sync_remote_matrix_changed(void);

/* =========================================================================
 * Sending key state (SLAVE side — transmits to MASTER)
 * ========================================================================= */

/**
 * @brief Send a full matrix snapshot to the master.
 *        Call on connect or after any layer change.
 *
 * @param peer_mac      Master's MAC
 * @param matrix        Local matrix bitmap (SPLIT_MATRIX_BYTES)
 * @param active_layer  Current active layer index
 * @param tx_seq        Sequence counter (incremented on success)
 */
esp_err_t split_sync_send_full_state(const uint8_t *peer_mac,
                                      const uint8_t *matrix,
                                      uint8_t active_layer,
                                      uint16_t *tx_seq);

/**
 * @brief Send a delta (only bytes that changed) to the master.
 *        Falls back to a full-state send if more than 8 bytes changed.
 *
 * @param peer_mac      Master's MAC
 * @param old_matrix    Previous matrix state
 * @param new_matrix    Current matrix state
 * @param active_layer  Current active layer index
 * @param tx_seq        Sequence counter (incremented on success)
 */
esp_err_t split_sync_send_delta(const uint8_t *peer_mac,
                                 const uint8_t *old_matrix,
                                 const uint8_t *new_matrix,
                                 uint8_t active_layer,
                                 uint16_t *tx_seq);
