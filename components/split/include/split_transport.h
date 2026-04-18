#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "split_crypto.h"
#include "split_protocol.h"

/* =========================================================================
 * Types
 * ========================================================================= */

/** Broadcast MAC address for ESP-NOW. */
#define SPLIT_BROADCAST_MAC  {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

/**
 * @brief Callback invoked when a frame is received for a registered protocol.
 *
 * @param src_mac   Sender's MAC address (6 bytes)
 * @param type      Message type byte from frame header
 * @param seq       Sequence number from frame header
 * @param payload   Pointer to payload (valid only during callback; excludes MIC)
 * @param len       Payload length in bytes
 * @param mic       Authentication tag (SPLIT_FRAME_MIC_SIZE bytes); zeroed for plaintext messages
 */
typedef void (*split_transport_recv_cb_t)(const uint8_t *src_mac,
                                          uint8_t type, uint64_t seq,
                                          const uint8_t *payload, size_t len,
                                          const uint8_t *mic);

/**
 * @brief Callback invoked after a send completes (success or failure).
 *
 * @param dst_mac   Destination MAC address
 * @param status    ESP-NOW send status
 */
typedef void (*split_transport_send_cb_t)(const uint8_t *dst_mac,
                                          esp_now_send_status_t status);

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/**
 * @brief Initialize the ESP-NOW transport layer.
 *
 * Initializes WiFi in station mode (no AP connect), starts ESP-NOW,
 * and registers the internal receive callback.
 *
 * Must be called before any other split_transport_* function.
 */
esp_err_t split_transport_init(void);

/**
 * @brief Deinitialize the transport layer and release ESP-NOW resources.
 */
esp_err_t split_transport_deinit(void);

/**
 * @brief Set (or clear) the AES-128-CCM session key used for all sends and receives.
 *
 * When a key is set every outgoing frame is encrypted and every incoming frame
 * is authenticated before being dispatched to the registered protocol handler.
 * Frames that fail authentication are silently dropped.
 *
 * Pass key=NULL to clear the key and revert to plaintext mode (used during
 * pairing, when no shared secret has been established yet).
 *
 * @param key  16-byte AES-128 session key, or NULL to disable encryption.
 */
void split_transport_set_session_key(const uint8_t *key);

/**
 * @brief Set the secondary "Handshake Key" used for recovery and negotiation.
 *
 * This key (usually the permanent paired key) is used as a fallback for
 * ROLE_NEGOTIATE messages if decryption with the primary session key fails.
 * This allows a rebooted device to re-enter a session with a peer that
 * is still using a transient transient key.
 */
void split_transport_set_handshake_key(const uint8_t *key);


/* =========================================================================
 * Protocol Registration
 * ========================================================================= */

/**
 * @brief Register a receive callback for a protocol ID.
 *
 * When a frame arrives with the given protocol ID and passes validation,
 * the callback is invoked from the ESP-NOW receive task context.
 *
 * @param proto_id  Protocol ID (e.g., SPLIT_PROTO_SPLIT)
 * @param cb        Receive callback
 */
esp_err_t split_transport_register_protocol(uint8_t proto_id,
                                            split_transport_recv_cb_t cb);

/**
 * @brief Set a callback for send-complete notifications.
 */
void split_transport_set_send_cb(split_transport_send_cb_t cb);

/* =========================================================================
 * Send / Receive
 * ========================================================================= */

/**
 * @brief Send a framed message to a peer (or broadcast).
 *
 * Builds a protocol frame (header + payload + MIC placeholder) and sends
 * it via ESP-NOW. Encryption is handled at a higher layer before calling
 * this — the transport sends the buffer as-is.
 *
 * @param dst_mac     Destination MAC (6 bytes), or broadcast MAC
 * @param proto_id    Protocol ID
 * @param type        Message type
 * @param seq         Sequence number
 * @param payload     Payload data (may be NULL if len == 0)
 * @param payload_len Payload length (max SPLIT_MAX_PAYLOAD)
 * @return ESP_OK on successful enqueue, error otherwise.
 */
esp_err_t split_transport_send(const uint8_t *dst_mac,
                               uint8_t proto_id, uint8_t type, uint64_t seq,
                               const uint8_t *payload, size_t payload_len);

/* =========================================================================
 * Peer Management
 * ========================================================================= */

/**
 * @brief Add an ESP-NOW peer. Must be called before unicast send to that peer.
 *
 * @param mac      Peer MAC address (6 bytes)
 * @param channel  WiFi channel (0 = current channel)
 */
esp_err_t split_transport_add_peer(const uint8_t *mac, uint8_t channel);

/**
 * @brief Remove an ESP-NOW peer.
 */
esp_err_t split_transport_remove_peer(const uint8_t *mac);

/**
 * @brief Check if a peer is registered.
 */
bool split_transport_peer_exists(const uint8_t *mac);

/* =========================================================================
 * Channel
 * ========================================================================= */

/**
 * @brief Set the WiFi channel used for ESP-NOW.
 */
esp_err_t split_transport_set_channel(uint8_t channel);

/**
 * @brief Get the current WiFi channel.
 */
uint8_t split_transport_get_channel(void);

/**
 * @brief Get the RTOS tick of the last successful frame sent to a non-broadcast peer.
 *
 * This provides a low-overhead way to track transport activity for heartbeat calculations.
 */
TickType_t split_transport_get_last_tx_time(void);
