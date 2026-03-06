#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

void ble_hid_init(void);
bool ble_hid_is_connected(void);
esp_err_t ble_hid_send_keyboard_report(const uint8_t *report, size_t len);
esp_err_t ble_hid_send_consumer_report(uint16_t media_keycode);

void ble_hid_profile_pair(uint8_t profile_id);
void ble_hid_profile_connect_and_select(uint8_t profile_id);
void ble_hid_profile_toggle_connection(uint8_t profile_id);
void ble_hid_set_routing_active(bool active);
bool ble_hid_is_routing_active(void);