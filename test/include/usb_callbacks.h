/**
 * @file usb_callbacks.h
 * @brief Shim — stubs for usb_callbacks API used by usb_callbacks_rx.c.
 *
 * execute_callback and usb_msg_module_t are provided by mock_tinyusb.h
 * (via usbmod.h shim or directly). This header provides
 * build_send_single_msg_packet and the hid_report_type_t stub.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "usb_defs.h"

/* hid_report_type_t stub (referenced by production usb_callbacks.h) */
#ifndef _TH_HID_REPORT_TYPE_T
#define _TH_HID_REPORT_TYPE_T
typedef uint8_t hid_report_type_t;
#endif

/* build_send_single_msg_packet — stub that records calls for test verification */
static int _mock_send_single_msg_count = 0;
static uint8_t _mock_last_send_flags = 0;
static uint16_t _mock_last_send_rem = 0;

static inline bool build_send_single_msg_packet(uint8_t flags, uint16_t rem,
                                                  uint8_t payload_len,
                                                  uint8_t *payload) {
    (void)payload_len; (void)payload;
    _mock_send_single_msg_count++;
    _mock_last_send_flags = flags;
    _mock_last_send_rem = rem;
    return true;
}

static inline void mock_send_single_msg_reset(void) {
    _mock_send_single_msg_count = 0;
    _mock_last_send_flags = 0;
    _mock_last_send_rem = 0;
}

/* send_single_packet — stub (used by usb_callbacks_tx.c, not rx) */
static inline bool send_single_packet(uint8_t *packet, uint16_t packet_len) {
    (void)packet; (void)packet_len;
    return true;
}
