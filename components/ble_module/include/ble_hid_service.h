#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and register the HID GATT service.
 */
void ble_hid_svc_register(void);

/**
 * @brief Send a keyboard report.
 *
 * @param conn_handle Connection handle of the connected central
 * @param report      Pointer to standard 8-byte Keyboard report
 * @param len         Length of the report (must be 8)
 * @return int        0 on success, NimBLE error code otherwise
 */
int ble_hid_tx_keyboard_report(uint16_t conn_handle, const uint8_t *report,
                               size_t len);

/**
 * @brief Send a consumer control (media) report.
 *
 * @param conn_handle Connection handle
 * @param media_keycode A 16-bit consumer control usage ID (0 to release).
 * @return int        0 on success, NimBLE error code otherwise
 */
int ble_hid_tx_consumer_report(uint16_t conn_handle, uint16_t media_keycode);

/**
 * @brief Send a battery level notification.
 *
 * @param conn_handle Connection handle
 * @param level       Battery level (0-100)
 * @return int        0 on success, NimBLE error code otherwise
 */
int ble_hid_notify_battery_level(uint16_t conn_handle, uint8_t level);

#ifdef __cplusplus
}
#endif
