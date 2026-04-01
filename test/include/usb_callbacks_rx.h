/**
 * @file usb_callbacks_rx.h
 * @brief Shim — RX callback declarations for host testing.
 *
 * Mirrors production usb_callbacks_rx.h using test-compatible usb_defs.h.
 */
#pragma once

#include "usb_defs.h"
#include <stdbool.h>

#define RX_TIMEOUT_MS 1000
#define MAX_RX_BUF_SIZE 21500
#define MAX_RX_BUF_SIZE_IN_PAYLOADS (MAX_RX_BUF_SIZE / MAX_PAYLOAD_LENGTH)

#define RX_BLAST_BITMAP_BYTES 48
#define RX_BLAST_MAX_PACKETS (RX_BLAST_BITMAP_BYTES * 8)

void process_rx_request(const usb_packet_msg_t msg);
void erase_rx_buffer(void);
uint64_t rx_get_last_packet_timestamp_us(void);

bool rx_blast_active(void);
void rx_blast_update_activity(void);
void rx_blast_receive_packet(const usb_packet_msg_t *msg);
void rx_blast_build_bitmap_response(usb_packet_msg_t *out_msg);
bool rx_blast_commit(const usb_packet_msg_t *last_msg);
