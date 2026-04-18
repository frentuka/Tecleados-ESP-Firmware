#include "battery.h"

/**
 * Default (weak) implementation — returns 0xFF (wired / level unknown).
 *
 * Replace this with a strong definition in your board-specific code to
 * provide real ADC-based battery voltage reading.
 */
__attribute__((weak))
uint8_t battery_get_level_pct(void)
{
    return 0xFF;
}
