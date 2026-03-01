#pragma once

#include "usb_defs.h"

// ============ RX Buffer ============

#define RX_TIMEOUT_MS 250 // max timeout for incoming sequential packets
#define MAX_RX_BUF_SIZE 21500 // 21.5KB -> 43(payload size) * 100(polling rate) * 5(seconds)
#define MAX_RX_BUF_SIZE_IN_PAYLOADS MAX_RX_BUF_SIZE / MAX_PAYLOAD_LENGTH // 500 packets long

void process_rx_request(const usb_packet_msg_t msg);
void erase_rx_buffer();
uint64_t rx_get_last_packet_timestamp_us();