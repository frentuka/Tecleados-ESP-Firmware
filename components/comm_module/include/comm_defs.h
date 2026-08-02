#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "comm_transport.h" // For comm_transport_t

// Protocol sizing constants
#define COMM_MAX_PACKET_SIZE 260
#define COMM_REPORT_SIZE 63

// ======== types ========

typedef enum comm_module_id: uint8_t {
	MODULE_CONFIG = 0,
	MODULE_SYSTEM,
    MODULE_ACTION,
    MODULE_STATUS,
    MODULE_SPLIT,
    MODULE_BLE,        // BLE profile / routing control
    COMM_MODULE_COUNT
} comm_module_id_t;

// ======== flags ========

#define PAYLOAD_FLAG_FIRST 0b10000000 // 0x80
#define PAYLOAD_FLAG_MID   0b01000000 // 0x40
#define PAYLOAD_FLAG_LAST  0b00100000 // 0x20

// transport-wise
#define PAYLOAD_FLAG_ACK   0b00010000 // 0x10
#define PAYLOAD_FLAG_NAK   0b00001000 // 0x08

// blast reconcile (combined flag values unused in normal flow)
#define PAYLOAD_FLAG_STATUS_REQ  0b01010000 // 0x50 (MID|ACK)
#define PAYLOAD_FLAG_BITMAP      0b01001000 // 0x48 (MID|NAK)

// process-wise
#define PAYLOAD_FLAG_OK    0b00000100 // 0x04
#define PAYLOAD_FLAG_ERR   0b00000010 // 0x02

#define PAYLOAD_FLAG_ABORT 0b00000001 // 0x01

// ======== single packet struct ========

// Packet header (4 bytes)
typedef struct __attribute__ ((packed)) {
    uint8_t flags;
    uint16_t remaining_packets;
    uint8_t payload_len;
} comm_packet_header_t;

// ======== callback type ========

typedef bool (*comm_data_callback_t)(comm_transport_t source, uint8_t *data, uint16_t data_len);
