#pragma once

#include "usb_defs.h"
#include <stdbool.h>

// ============ TX Buffer ============

#define TX_TIMEOUT_MS 250
#define MAX_TX_BUF_SIZE 21500
#define MAX_TX_BUF_SIZE_IN_PAYLOADS (MAX_TX_BUF_SIZE / MAX_PAYLOAD_LENGTH)

// Max packets supportable by bitmap (48 bytes * 8 bits = 384)
#define TX_BLAST_BITMAP_BYTES 48
#define TX_BLAST_MAX_PACKETS (TX_BLAST_BITMAP_BYTES * 8)

// Legacy sequential API
void process_tx_response(const usb_packet_msg_t msg);
void erase_tx_buffer();
uint64_t tx_get_last_packet_timestamp_us();

// Blast mode API
bool tx_blast_active();
void tx_blast_handle_bitmap(const usb_packet_msg_t *msg);