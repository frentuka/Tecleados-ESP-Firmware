#pragma once

#include <stdint.h>
#include <stdbool.h>

// ======== types ========

typedef enum usb_msg_module: uint8_t {
	MODULE_CONFIG = 0,
	MODULE_SYSTEM,
    MODULE_ACTION,
    USB_MODULE_COUNT
} usb_msg_module_t;

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

#define MAX_PAYLOAD_LENGTH 58

typedef struct __attribute__ ((packed)) {
    uint8_t flags;
	uint16_t remaining_packets;
    uint8_t payload_len;
    uint8_t payload[MAX_PAYLOAD_LENGTH];
    uint8_t crt;
} usb_packet_msg_t;

// ======== callback type ========

typedef bool (*usb_data_callback_t)(uint8_t *data, uint16_t data_len);