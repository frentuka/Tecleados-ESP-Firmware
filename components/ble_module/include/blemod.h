#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** @brief Initialize the BLE HID stack and start advertising. */
void ble_hid_init(void);

/** @brief Returns true if the currently selected profile is connected. */
bool ble_hid_is_connected(void);

/** @brief Send an 8-byte standard keyboard HID report over BLE. */
esp_err_t ble_hid_send_keyboard_report(const uint8_t *report, size_t len);

/** @brief Send a 16-bit consumer control (media key) HID report over BLE. */
esp_err_t ble_hid_send_consumer_report(uint16_t media_keycode);

/** @brief Erase profile credentials and begin discoverable pairing advertising. */
void ble_hid_profile_pair(uint8_t profile_id);

/** @brief Select a profile and trigger reconnection advertising if not connected. */
void ble_hid_profile_connect_and_select(uint8_t profile_id);

/** @brief Toggle the connection on a profile: disconnect if connected, reconnect if not. */
void ble_hid_profile_toggle_connection(uint8_t profile_id);

/** @brief Enable or disable BLE HID report routing. Stops advertising when disabled. */
void ble_hid_set_routing_active(bool active);

/** @brief Returns true if BLE HID routing is currently enabled. */
bool ble_hid_is_routing_active(void);

/** @brief Returns a bitmask of connected profiles (bit N set = profile N is connected). */
uint16_t ble_hid_get_connected_profiles_bitmap(void);

/** @brief Returns the profile currently being paired, or -1 if not pairing. */
int ble_hid_get_pairing_profile(void);
