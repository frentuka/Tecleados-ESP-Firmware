#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

void ble_hid_init(void);
bool ble_hid_is_connected(void);
esp_err_t ble_hid_send_keyboard_report(const uint8_t *report, size_t len);
esp_err_t ble_hid_send_consumer_report(uint16_t media_keycode);