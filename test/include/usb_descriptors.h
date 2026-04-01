/**
 * @file usb_descriptors.h
 * @brief Shim — provides USB descriptor constants for host testing.
 *
 * Only the constants and enums needed by production code under test.
 * Full descriptors and TUD_* macros are not needed.
 */
#pragma once

#include "mocks/mock_tinyusb.h"

/* Report IDs */
#define REPORT_ID_KEYBOARD 1
#define REPORT_ID_NKRO     2
#define REPORT_ID_COMM     3
#define REPORT_ID_CONSUMER 4

/* Sizes */
#define NKRO_KEYS       0xE7
#define NKRO_BYTES      ((NKRO_KEYS + 7) / 8)
#define NKRO_REPORT_SIZE 64
#define COMM_REPORT_SIZE 63

/* Interfaces */
enum { ITF_NUM_HID_KBD = 0, ITF_NUM_HID_COMM = 1, ITF_NUM_TOTAL = 2 };
