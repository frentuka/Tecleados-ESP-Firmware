#pragma once

#include <stdint.h>

/**
 * @brief Get the current battery charge level.
 *
 * @return Battery percentage 0–100, or 0xFF if wired/unknown.
 *
 * This is a weak function. The default implementation returns 0xFF (unknown).
 * Override it in a hardware-specific source file by providing a strong definition
 * that reads the ADC-connected battery voltage divider:
 *
 * @code
 * #include "battery.h"
 * #include "esp_adc/adc_oneshot.h"
 *
 * uint8_t battery_get_level_pct(void) {
 *     // Sample ADC channel connected to battery divider,
 *     // convert raw voltage → 0–100 percentage.
 *     ...
 * }
 * @endcode
 */
uint8_t battery_get_level_pct(void);
