/**
 * @file usb_defs.h
 * @brief Shim — C17/MSVC-compatible version of production usb_defs.h.
 *
 * The production header uses C23 typed enums (enum : uint8_t) and
 * __attribute__((packed)), which MSVC C17 mode doesn't support.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Match test_harness.h packed struct macros */
#ifndef PACKED_STRUCT_BEGIN
  #ifdef _MSC_VER
    #define PACKED_STRUCT_BEGIN __pragma(pack(push, 1))
    #define PACKED_STRUCT_END   __pragma(pack(pop))
    #define PACKED_ATTR
  #else
    #define PACKED_STRUCT_BEGIN
    #define PACKED_STRUCT_END
    #define PACKED_ATTR __attribute__((packed))
  #endif
#endif

#ifndef _TH_USB_MSG_MODULE_T
#define _TH_USB_MSG_MODULE_T
typedef enum {
    MODULE_CONFIG = 0,
    MODULE_SYSTEM,
    MODULE_ACTION,
    MODULE_STATUS,
    USB_MODULE_COUNT
} usb_msg_module_t;
#endif

/* Flags */
#define PAYLOAD_FLAG_FIRST      0x80
#define PAYLOAD_FLAG_MID        0x40
#define PAYLOAD_FLAG_LAST       0x20
#define PAYLOAD_FLAG_ACK        0x10
#define PAYLOAD_FLAG_NAK        0x08
#define PAYLOAD_FLAG_STATUS_REQ 0x50
#define PAYLOAD_FLAG_BITMAP     0x48
#define PAYLOAD_FLAG_OK         0x04
#define PAYLOAD_FLAG_ERR        0x02
#define PAYLOAD_FLAG_ABORT      0x01

#define MAX_PAYLOAD_LENGTH 58

#ifndef _TH_USB_PACKET_MSG_T
#define _TH_USB_PACKET_MSG_T
PACKED_STRUCT_BEGIN
typedef struct PACKED_ATTR {
    uint8_t  flags;
    uint16_t remaining_packets;
    uint8_t  payload_len;
    uint8_t  payload[MAX_PAYLOAD_LENGTH];
    uint8_t  crc;
} usb_packet_msg_t;
PACKED_STRUCT_END
#endif

#ifndef _TH_USB_DATA_CALLBACK_T
#define _TH_USB_DATA_CALLBACK_T
typedef bool (*usb_data_callback_t)(uint8_t *data, uint16_t data_len);
#endif
