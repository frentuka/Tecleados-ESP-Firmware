#pragma once

#include "comm_defs.h"
#include <stdbool.h>

#define TX_TIMEOUT_MS 1000
#define MAX_TX_BUF_SIZE 21500

#define TX_BLAST_BITMAP_BYTES 48
#define TX_BLAST_MAX_PACKETS (TX_BLAST_BITMAP_BYTES * 8)

void comm_process_tx_response(comm_transport_t source, const uint8_t *packet, uint16_t len);
void comm_erase_tx_buffer(void);
uint64_t comm_tx_get_last_packet_timestamp_us(void);

bool comm_tx_blast_active(void);
void comm_tx_blast_handle_bitmap(const uint8_t *packet, uint16_t len);

void comm_tx_init(void);

// High-level API for sending payloads (now queued)
bool comm_send_payload(comm_transport_t target, const uint8_t *payload, uint16_t payload_len);
