#pragma once

#include <stdint.h>
#include <stdbool.h>

void usb_crc_prepare_packet(uint8_t *packet);
bool usb_crc_verify_packet(const uint8_t *packet);