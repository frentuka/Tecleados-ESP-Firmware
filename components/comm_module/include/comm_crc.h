#pragma once

#include <stdint.h>
#include <stdbool.h>

void comm_crc_prepare_packet(uint8_t *packet, uint16_t logical_len);
bool comm_crc_verify_packet(const uint8_t *packet, uint16_t logical_len);
