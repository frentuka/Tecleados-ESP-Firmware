#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Wire Constants
 * ========================================================================= */

#define SPLIT_FRAME_MAGIC        0x4B53   // "KS" (Keyboard Split)
#define SPLIT_FRAME_VERSION      0x01

#define SPLIT_PROTO_SPLIT        0x01     // Split keyboard protocol
#define SPLIT_PROTO_DONGLE       0x02     // Dongle protocol (future)

#define SPLIT_FRAME_HEADER_SIZE  6        // magic(2) + proto(1) + type(1) + seq(2)
#define SPLIT_FRAME_MIC_SIZE     4        // AES-128-CCM truncated auth tag
#define SPLIT_FRAME_OVERHEAD     (SPLIT_FRAME_HEADER_SIZE + SPLIT_FRAME_MIC_SIZE)

#define SPLIT_ESP_NOW_MAX        250      // ESP-NOW max payload
#define SPLIT_MAX_PAYLOAD        (SPLIT_ESP_NOW_MAX - SPLIT_FRAME_OVERHEAD) // 240 bytes

// 6 rows × 18 cols = 108 keys → ceil(108/8) = 14 bytes
#define SPLIT_MATRIX_BYTES       14

/* =========================================================================
 * Message Types (split protocol)
 * ========================================================================= */

typedef enum split_msg_type : uint8_t {
    // Discovery & Pairing (0x0_)
    SPLIT_MSG_DISCOVERY       = 0x01,  // Broadcast beacon during pairing
    SPLIT_MSG_PAIR_REQUEST    = 0x02,  // Key exchange initiation
    SPLIT_MSG_PAIR_RESPONSE   = 0x03,  // Key exchange completion

    // Role Negotiation (0x1_)
    SPLIT_MSG_ROLE_NEGOTIATE  = 0x10,  // Propose/accept roles
    SPLIT_MSG_ROLE_SWAP_REQ   = 0x11,  // Request role swap
    SPLIT_MSG_ROLE_SWAP_ACK   = 0x12,  // Acknowledge role swap

    // Key State (0x2_)
    SPLIT_MSG_KEY_STATE_FULL  = 0x20,  // Full matrix bitmap
    SPLIT_MSG_KEY_STATE_DELTA = 0x21,  // Incremental: changed bytes only

    // Connection (0x3_)
    SPLIT_MSG_HEARTBEAT       = 0x30,  // Keepalive
    SPLIT_MSG_DISCONNECT      = 0x31,  // Graceful disconnect

    // Configuration (0x4_)
    SPLIT_MSG_CONFIG_SYNC     = 0x40,  // Config data fragment
    SPLIT_MSG_CONFIG_SYNC_ACK = 0x41,  // Config sync acknowledged

    // Diagnostics (0x5_)
    SPLIT_MSG_PING            = 0x50,  // RTT benchmark probe  (Master → Slave)
    SPLIT_MSG_PONG            = 0x51,  // RTT benchmark reply  (Slave  → Master)
} split_msg_type_t;

/* =========================================================================
 * Wire Frame
 * ========================================================================= */

/**
 * On-wire frame layout (packed, little-endian):
 *
 *   [magic:2][proto:1][type:1][seq:2][payload:0..240][mic:4]
 *
 * Before encryption: header is authenticated but not encrypted (AAD).
 * Payload + MIC are encrypted with AES-128-CCM.
 * During pairing (no key yet): mic is zeroed, payload is plaintext.
 */
typedef struct __attribute__((packed)) split_frame_header {
    uint16_t magic;      // SPLIT_FRAME_MAGIC
    uint8_t  proto;      // Protocol ID (SPLIT_PROTO_SPLIT, etc.)
    uint8_t  type;       // split_msg_type_t
    uint16_t seq;        // Monotonic sequence number
} split_frame_header_t;

_Static_assert(sizeof(split_frame_header_t) == SPLIT_FRAME_HEADER_SIZE,
               "Frame header must be exactly 6 bytes");

/* =========================================================================
 * Message Payloads
 * ========================================================================= */

/** SPLIT_MSG_DISCOVERY — broadcast during pairing. Unencrypted. */
typedef struct __attribute__((packed)) split_discovery_payload {
    uint8_t  version;           // Protocol version
    uint8_t  device_id[6];      // This device's MAC
    uint8_t  preferred_role;    // 0=auto, 1=master, 2=slave
    uint16_t firmware_version;  // For compatibility checks
} split_discovery_payload_t;

/** SPLIT_MSG_PAIR_REQUEST / SPLIT_MSG_PAIR_RESPONSE — key exchange. Unencrypted. */
typedef struct __attribute__((packed)) split_pair_payload {
    uint8_t  device_id[6];      // Sender's MAC
    uint8_t  public_key[32];    // X25519 public key
} split_pair_payload_t;

/** SPLIT_MSG_ROLE_NEGOTIATE — role proposal. */
typedef struct __attribute__((packed)) split_role_payload {
    uint8_t  proposed_role;     // Role sender wants to take (split_role_t)
    uint8_t  device_id[6];      // For tiebreaker comparison
} split_role_payload_t;

/** SPLIT_MSG_KEY_STATE_FULL — complete matrix snapshot. */
typedef struct __attribute__((packed)) split_key_state_full_payload {
    uint8_t  active_layer;                  // Current active layer on sender
    uint8_t  matrix[SPLIT_MATRIX_BYTES];    // Full key bitmap (row-major, 14 bytes)
} split_key_state_full_payload_t;

/** SPLIT_MSG_KEY_STATE_DELTA — incremental matrix update. */
typedef struct __attribute__((packed)) split_key_state_delta_payload {
    uint8_t  active_layer;      // Current active layer on sender
    uint16_t changed_mask;      // Bitmask of which bitmap bytes changed (bit i = byte i, 14 bits used)
    uint8_t  values[];          // Changed bytes in ascending byte-index order
} split_key_state_delta_payload_t;

/** SPLIT_MSG_HEARTBEAT — keepalive. */
typedef struct __attribute__((packed)) split_heartbeat_payload {
    uint8_t  state;             // Sender's split_state_t
    uint8_t  role;              // Sender's split_role_t
    uint8_t  battery_pct;      // Battery percentage (0xFF = wired/unknown)
    int8_t   rssi;              // Sender's last measured RSSI of peer
    uint32_t sent_us;           // Low 32 bits of esp_timer_get_time() at send time.
                                // Slave fills this; Master echoes it back unchanged
                                // so Slave can compute RTT = (now - sent_us) / 2.
} split_heartbeat_payload_t;

// Max data bytes per CONFIG_SYNC fragment
// SPLIT_MAX_PAYLOAD(240) - kind(1) - key(12) - fragment_idx(1) - fragment_total(1) = 225
#define SPLIT_CONFIG_SYNC_KEY_LEN    12
#define SPLIT_CONFIG_SYNC_HDR_SIZE   15
#define SPLIT_CONFIG_SYNC_DATA_MAX   (SPLIT_MAX_PAYLOAD - SPLIT_CONFIG_SYNC_HDR_SIZE)

/** SPLIT_MSG_CONFIG_SYNC — fragmented config data. */
typedef struct __attribute__((packed)) split_config_sync_payload {
    uint8_t  kind;                              // cfgmod_kind_t being synced
    uint8_t  key[SPLIT_CONFIG_SYNC_KEY_LEN];    // NVS storage key (null-padded)
    uint8_t  fragment_idx;                      // Fragment index (0-based)
    uint8_t  fragment_total;                    // Total fragment count
    uint8_t  data[];                            // Blob slice (variable, max SPLIT_CONFIG_SYNC_DATA_MAX)
} split_config_sync_payload_t;

_Static_assert(sizeof(split_config_sync_payload_t) == SPLIT_CONFIG_SYNC_HDR_SIZE,
               "CONFIG_SYNC header size mismatch");

/** SPLIT_MSG_CONFIG_SYNC_ACK — sync result from slave. */
typedef struct __attribute__((packed)) split_config_sync_ack_payload {
    uint8_t  kind;
    uint8_t  key[SPLIT_CONFIG_SYNC_KEY_LEN];
    uint8_t  status;    // 0 = ESP_OK
} split_config_sync_ack_payload_t;

/** SPLIT_MSG_PING / SPLIT_MSG_PONG — RTT benchmark.
 *  Master sends PING with probe_idx and sent_us filled.
 *  Slave echoes the entire payload back as PONG (no modification).
 *  Master computes RTT = now_us - sent_us when PONG is received. */
typedef struct __attribute__((packed)) split_ping_payload {
    uint8_t  probe_idx;     // Probe index (0-based) so master can match pongs out-of-order
    uint8_t  _pad[3];       // Alignment padding
    uint32_t sent_us;       // esp_timer_get_time() low 32 bits at send time
} split_ping_payload_t;

/* =========================================================================
 * Serialization helpers (implemented in split_protocol.c)
 * ========================================================================= */

/**
 * @brief Build a frame into the provided buffer.
 * @param[out] out_buf   Output buffer (must be >= SPLIT_FRAME_OVERHEAD + payload_len)
 * @param      out_max   Size of output buffer
 * @param      proto     Protocol ID
 * @param      type      Message type
 * @param      seq       Sequence number
 * @param      payload   Payload data (may be NULL if payload_len == 0)
 * @param      payload_len Payload length
 * @return Total frame size, or 0 on error.
 */
size_t split_protocol_build_frame(uint8_t *out_buf, size_t out_max,
                                  uint8_t proto, uint8_t type, uint16_t seq,
                                  const uint8_t *payload, size_t payload_len);

/**
 * @brief Parse and validate a received frame.
 * @param[in]  data       Raw received data
 * @param      len        Length of received data
 * @param[out] out_header Parsed header
 * @param[out] out_payload Pointer set to start of payload within data
 * @param[out] out_payload_len Payload length
 * @return true if frame is valid, false otherwise.
 */
bool split_protocol_parse_frame(const uint8_t *data, size_t len,
                                split_frame_header_t *out_header,
                                const uint8_t **out_payload,
                                size_t *out_payload_len);
