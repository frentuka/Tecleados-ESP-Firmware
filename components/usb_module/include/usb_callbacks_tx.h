#pragma once

#include "usb_defs.h"

// ============ TX Buffer ============

#define TX_TIMEOUT_MS 1000
#define MAX_TX_BUF_SIZE 21500
#define MAX_TX_BUF_SIZE_IN_PAYLOADS MAX_TX_BUF_SIZE / MAX_PAYLOAD_LENGTH

void process_tx_response(const usb_packet_msg_t msg);
void erase_tx_buffer();
uint64_t tx_get_last_packet_timestamp_us();