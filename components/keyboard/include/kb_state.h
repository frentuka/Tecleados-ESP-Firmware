#pragma once

#include <stdbool.h>
#include <stdint.h>


// Standard HID LED status bits
#define KB_LED_NUM_LOCK (1 << 0)
#define KB_LED_CAPS_LOCK (1 << 1)
#define KB_LED_SCROLL_LOCK (1 << 2)
#define KB_LED_COMPOSE (1 << 3)
#define KB_LED_KANA (1 << 4)

/**
 * @brief Initialize the global keyboard state tracker.
 */
void kb_state_init(void);

/**
 * @brief Update the internal keyboard LED state based on a host output report.
 * @param led_status The bitmask of LEDs (Num Lock, Caps Lock, etc).
 */
void kb_state_update_leds(uint8_t led_status);

/**
 * @brief Get the current internal keyboard LED state.
 * @return Bitmask of active LEDs.
 */
uint8_t kb_state_get_leds(void);
