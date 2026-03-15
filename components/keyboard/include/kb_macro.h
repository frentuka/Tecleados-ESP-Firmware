#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialize the Macro Engine and Virtual State
 */
void kb_macro_init(void);

/**
 * @brief Process an action code from the matrix (edge event)
 */
void kb_macro_process_action(uint16_t action_code, bool is_pressed);

/**
 * @brief Send a raw HID key press to the virtual state
 */
void kb_macro_virtual_press(uint8_t hid_keycode);

/**
 * @brief Send a raw HID key release to the virtual state
 */
void kb_macro_virtual_release(uint8_t hid_keycode);

/**
 * @brief Get the currently active layer
 */
uint8_t kb_macro_get_active_layer(void);

/**
 * @brief Force a report to be sent from the current virtual state
 */
#include "esp_err.h"
esp_err_t kb_macro_send_report(void);

/**
 * @brief Fire a single-action tap (press + delay + release) via the macro task.
 * Safe to call from any task context. Does NOT block the caller.
 *
 * @param action_code  Action code to fire (HID, System, CKey, etc.)
 * @param duration_ms  How long to hold the virtual key before releasing (ms).
 *                     If 0, a default of 10 ms is used.
 */
void kb_macro_fire_tap(uint16_t action_code, uint32_t duration_ms, uint32_t delay_ms);
