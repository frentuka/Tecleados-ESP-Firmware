#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "tinyusb.h"

// test stuff
void usb_send_char(char c);
void usb_send_keystroke(uint8_t hid_keycode);

// real stuff
bool usb_keyboard_use_boot_protocol(void);
bool usb_send_keyboard_6kro(uint8_t modifier, const uint8_t keycodes[6]);
bool usb_send_keyboard_nkro(const uint8_t *bitmap, uint16_t len);

void usbmod_register_callback(usb_msg_type_t callback_type, usb_data_callback_t callback);
bool usbmod_execute_callback(usb_msg_type_t callback_type, uint8_t const *data, uint16_t data_len);

void usb_task(void *arg);
void usb_init();

// callback stuff (to be managed inside usb_callbacks)
// TinyUSB HID callbacks are required when HID is enabled in the descriptor/config.
// Provide minimal stubs to satisfy the linker (and optionally extend later).
uint16_t tud_hid_get_report_cb(uint8_t instance,
                              uint8_t report_id,
                              hid_report_type_t report_type,
                              uint8_t *buffer,
                              uint16_t reqlen);

void tud_hid_set_report_cb(uint8_t instance,
                           uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer,
                           uint16_t bufsize);