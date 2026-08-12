#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "comm_defs.h"
#include "comm_transport.h"

bool comm_build_send_single_packet(comm_transport_t target, uint8_t flags, uint16_t rem, uint8_t payload_len, const uint8_t *payload);
bool comm_send_single_packet(comm_transport_t target, uint8_t *packet, uint16_t logical_len);
