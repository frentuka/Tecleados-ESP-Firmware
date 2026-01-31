#pragma once

#include <stdint.h>

bool build_send_single_msg_packet(uint8_t flags, uint16_t rem, uint8_t payload_len, uint8_t *payload);
bool send_single_packet(uint8_t *packet, uint16_t packet_len);
bool send_payload(const uint8_t *payload, uint16_t payload_len);