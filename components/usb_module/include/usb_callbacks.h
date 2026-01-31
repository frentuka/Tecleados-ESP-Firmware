#pragma once

#include <stdint.h>
#include "tinyusb.h"

// TinyUSB HID callbacks are required when HID is enabled in the descriptor/config.
// Provide minimal stubs to satisfy the linker (and optionally extend later).
uint16_t usbmod_tud_hid_get_report_cb(uint8_t instance,
                              uint8_t report_id,
                              hid_report_type_t report_type,
                              uint8_t *buffer,
                              uint16_t reqlen);

void usbmod_tud_hid_set_report_cb(uint8_t instance,
                           uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer,
                           uint16_t bufsize);

void usb_callbacks_init(void);