#pragma once

#include "usb_defs.h"
#include <stdbool.h>

// ============ RX Buffer ============

#define RX_TIMEOUT_MS 250 // max timeout for incoming sequential packets
#define MAX_RX_BUF_SIZE 21500 // 21.5KB -> 43(payload size) * 100(polling rate) * 5(seconds)
#define MAX_RX_BUF_SIZE_IN_PAYLOADS (MAX_RX_BUF_SIZE / MAX_PAYLOAD_LENGTH)

// Max packets supportable by bitmap (48 bytes * 8 bits = 384)
#define RX_BLAST_BITMAP_BYTES 48
#define RX_BLAST_MAX_PACKETS (RX_BLAST_BITMAP_BYTES * 8)

// Legacy sequential API
void process_rx_request(const usb_packet_msg_t msg);
void erase_rx_buffer();
uint64_t rx_get_last_packet_timestamp_us();

// Blast mode API
bool rx_blast_active();
void rx_blast_receive_packet(const usb_packet_msg_t *msg);
void rx_blast_build_bitmap_response(usb_packet_msg_t *out_msg);
bool rx_blast_commit(const usb_packet_msg_t *last_msg);