/**
 * @file test_constants.h
 * @brief Shared firmware constants and helpers for all test modules.
 *
 * Includes real production headers for canonical constant values, then
 * defines test-only extras not present in any production header.
 * Included automatically via test_harness.h.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ======================================================================
 * Production headers — canonical constants
 *
 * These pull in the real firmware definitions via include-path shims
 * (e.g. esp_event.h → mocks/mock_esp_event.h).
 * ====================================================================== */

#include "event_bus.h"          /* event bases, IDs, payload structs          */
#include "kb_system_action.h"   /* kb_action_ev_t, kb_sys_action_timing_t     */

/*
 * kb_layout.h transitively includes:
 *   class/hid/hid.h  (shim — HID keycodes)
 *   kb_matrix.h      (KB_MATRIX_ROW_COUNT, _COL_COUNT, _KEYS, _BITMAP_BYTES)
 *     driver/gpio.h  (shim — GPIO stubs)
 *     freertos/...   (shims — mock_freertos.h)
 *
 * And directly defines:
 *   ACTION_CODE_*, SYS_ACTION_*, MEDIA_ACTION_*,
 *   KB_KEY_TRANSPARENT, KB_LAYER_*, kb_layer_t
 */
#include "kb_layout.h"

/* ======================================================================
 * Typedef guards — prevent redefinition in test files that still have
 * legacy #ifndef _TH_* guarded typedefs (event_bus.h already defines
 * these structs, so the test files' copies must be skipped).
 * ====================================================================== */

#define _TH_KB_SYS_ACTION_EVENT_T
#define _TH_BLE_PAIRING_RESULT_T
#define _TH_CONFIG_UPDATE_EVENT_T

/* ======================================================================
 * Aliases — bridge naming differences between production and tests
 * ====================================================================== */

/* Production uses KB_MATRIX_KEYS; tests historically used KB_MATRIX_KEY_COUNT */
#ifndef KB_MATRIX_KEY_COUNT
  #define KB_MATRIX_KEY_COUNT KB_MATRIX_KEYS
#endif

/* ======================================================================
 * USB Protocol Constants (from usb_defs.h — not linked due to C23 syntax,
 * so redefined here for test files that need them)
 * ====================================================================== */

#ifndef PAYLOAD_FLAG_FIRST
  #define PAYLOAD_FLAG_FIRST      0x80
  #define PAYLOAD_FLAG_MID        0x40
  #define PAYLOAD_FLAG_LAST       0x20
  #define PAYLOAD_FLAG_ACK        0x10
  #define PAYLOAD_FLAG_NAK        0x08
  #define PAYLOAD_FLAG_STATUS_REQ 0x50
  #define PAYLOAD_FLAG_BITMAP     0x48
  #define PAYLOAD_FLAG_OK         0x04
  #define PAYLOAD_FLAG_ERR        0x02
  #define PAYLOAD_FLAG_ABORT      0x01
#endif

#ifndef MAX_PAYLOAD_LENGTH
  #define MAX_PAYLOAD_LENGTH      58
#endif

#ifndef COMM_REPORT_SIZE
  #define COMM_REPORT_SIZE        63
#endif

#define MAX_RX_BUF_SIZE         21500
#define MAX_TX_BUF_SIZE         21500
#define RX_BLAST_BITMAP_BYTES   48
#define RX_BLAST_MAX_PACKETS    (RX_BLAST_BITMAP_BYTES * 8) /* 384 */
#define TX_BLAST_MAX_RECONCILE_ROUNDS 5
#define TX_NAK_RESEND_MAX_ATTEMPTS    3

/* ======================================================================
 * HID Constants (extras not in production headers)
 * ====================================================================== */

#define NKRO_BITMAP_BYTES 32
#define NKRO_TOTAL_KEYS   0xE7
#define HID_MODIFIER_MIN  0xE0
#define HID_MODIFIER_MAX  0xE7
#define SIXKRO_MAX_KEYS   6

/* ======================================================================
 * Tap/Hold Engine Defaults (also in kb_system_action.c as private #defines;
 * re-exported here so tests can reference the expected default values)
 * ====================================================================== */

#ifndef DOUBLE_TAP_TIMEOUT_US_DEFAULT
  #define DOUBLE_TAP_TIMEOUT_US_DEFAULT  300000LL
  #define HOLD_TIMEOUT_US_DEFAULT        500000LL
#endif

#ifndef MAX_CONCURRENT_ACTIONS
  #define MAX_CONCURRENT_ACTIONS         10
#endif

/* ======================================================================
 * Custom Key / Macro Constants
 * ====================================================================== */

#define CFG_CKEYS_MAX_COUNT 120
#define CFG_MACRO_MAX_EVENTS 256
#define CFG_MACROS_MAX_COUNT 64
#define CFGMOD_MAX_KEY_LEN 12

/* ======================================================================
 * Shared Bitmap Helpers
 * ====================================================================== */

static inline void kb_bit_set(uint8_t *bitmap, size_t bit_index) {
    bitmap[bit_index >> 3] |= (uint8_t)(1U << (bit_index & 7U));
}

static inline void kb_bit_clear(uint8_t *bitmap, size_t bit_index) {
    bitmap[bit_index >> 3] &= (uint8_t)~(1U << (bit_index & 7U));
}

static inline bool kb_bit_get(const uint8_t *bitmap, size_t bit_index) {
    return (bitmap[bit_index >> 3] & (uint8_t)(1U << (bit_index & 7U))) != 0;
}

/* ======================================================================
 * Shared Packed Struct — USB Packet (mirrors usb_defs.h without C23 syntax)
 * ====================================================================== */

#ifndef _TH_USB_PACKET_MSG_T
#define _TH_USB_PACKET_MSG_T
PACKED_STRUCT_BEGIN
typedef struct PACKED_ATTR {
    uint8_t  flags;
    uint16_t remaining_packets;
    uint8_t  payload_len;
    uint8_t  payload[MAX_PAYLOAD_LENGTH];
    uint8_t  crc;
} usb_packet_msg_t;
PACKED_STRUCT_END
#endif

/* ======================================================================
 * BLE / Status constants
 * ====================================================================== */

#define MAX_BLE_PROFILES 16
