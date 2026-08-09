#pragma once

#include "comm_defs.h"
#include <stdbool.h>

#define RX_TIMEOUT_MS 1000
#define MAX_RX_BUF_SIZE 10240

#define RX_BLAST_BITMAP_BYTES 48
#define RX_BLAST_MAX_PACKETS (RX_BLAST_BITMAP_BYTES * 8)

void comm_process_rx_request(comm_transport_t source, const uint8_t *packet, uint16_t len);
void comm_erase_rx_buffer(void);
uint64_t comm_rx_get_last_packet_timestamp_us(void);

bool comm_rx_blast_active(void);
void comm_rx_blast_update_activity(void);
void comm_rx_blast_build_bitmap_response(uint8_t *out_payload, uint16_t *out_len);
bool comm_rx_blast_commit(comm_transport_t source, const uint8_t *last_packet, uint16_t len);
