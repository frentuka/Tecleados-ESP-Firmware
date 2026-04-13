#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

/* =========================================================================
 * Split task — main state-machine loop.
 *
 * Runs a ~100 Hz tick that drives per-state behaviour:
 *   PAIRING      broadcast DISCOVERY, respect pairing deadline
 *   CONNECTING   retransmit ROLE_NEGOTIATE
 *   CONNECTED    heartbeat, bench probes, deferred config sync
 *   DISCONNECTED reconnect with exponential backoff
 *
 * Deferred-work flags (set from the WiFi task / event-bus handlers) let the
 * main task own NVS-heavy / blocking operations such as config sync push.
 * ========================================================================= */

esp_err_t split_task_start(void);
void      split_task_stop(void);

/* ---- State-transition helpers used by dispatch / lifecycle ------------- */

/**
 * @brief Tear down an active connection and begin reconnect backoff.
 *        Safe to call from any task.
 *
 * Sets state to DISCONNECTED, clears role / link-stale / anti-replay, resets
 * routing to standalone, resets config-sync reassembly, posts
 * SPLIT_EVENT_DISCONNECTED, and resets reconnect backoff to minimum.
 */
void split_task_handle_disconnect(const char *reason);

/**
 * @brief Build and send a ROLE_NEGOTIATE frame to the current peer.
 *        Used from dispatch (on-pairing-complete / peer re-negotiation) and
 *        from the CONNECTING tick. Updates the last-sent timestamp.
 */
void split_task_send_role_negotiate(void);

/**
 * @brief Arm the CONNECTING tick to fire a ROLE_NEGOTIATE on the next pass.
 *        Used by the DISCONNECTED tick to kick the reconnect handshake.
 */
void split_task_trigger_immediate_role_neg(void);

/**
 * @brief Reset the pairing deadline (0 = no timeout).
 *        Called by the public start/cancel pairing API.
 */
void split_task_set_pairing_deadline(TickType_t deadline);

/** @brief Force the next PAIRING tick to emit a DISCOVERY immediately. */
void split_task_reset_discovery_timer(void);

/** @brief Reset reconnect backoff to minimum after a successful connect. */
void split_task_reset_reconnect_backoff(void);

/* ---- Deferred config-sync requests ------------------------------------- */

/** @brief Ask the main task to push the full config blob once settle elapses. */
void split_task_request_config_sync_initial(void);

/** @brief Ask the main task to push all syncable entries immediately. */
void split_task_request_config_sync_incremental(void);

/** @brief Ask the main task to push our own ble_cfg + bonds back to the peer. */
void split_task_request_reverse_ble_sync(void);
