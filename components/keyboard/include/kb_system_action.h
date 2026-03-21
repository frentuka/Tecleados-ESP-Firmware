#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Types of events that can be fired for a system action.
 */
typedef enum {
    KB_EV_PRESS,        // Fired immediately when the key is pressed
    KB_EV_RELEASE,      // Fired when the key is released
    KB_EV_SINGLE_TAP,   // Fired when the key is released before hold timeout, and no double tap occurs
    KB_EV_DOUBLE_TAP,   // Fired when the key is pressed twice within the double tap window
    KB_EV_HOLD          // Fired when the key is held longer than the hold timeout
} kb_action_ev_t;

/**
 * @brief Per-action timing configuration for the tap/hold engine.
 * Used by Custom Keys (MultiAction mode) to override the engine's global defaults.
 * Set a field to 0 to use the engine's built-in default for that threshold.
 */
typedef struct {
    uint32_t double_tap_threshold_ms; // 0 => engine default (300 ms)
    uint32_t hold_threshold_ms;       // 0 => engine default (500 ms)
} kb_sys_action_timing_t;

/**
 * @brief Initialize the system action generic tap/hold engine.
 */
void kb_system_action_init(void);

/**
 * @brief Process a raw system action press/release from the keyboard matrix.
 * Uses the engine's global default timing thresholds.
 *
 * @param action_code The SYS_ACTION_* code.
 * @param is_pressed True if pressed, false if released.
 */
void kb_system_action_process(uint16_t action_code, bool is_pressed);

/**
 * @brief Process a raw action press/release with custom per-action timing thresholds.
 * Used by Custom Keys (MultiAction mode) to override engine-global defaults.
 *
 * @param action_code Action code to track.
 * @param is_pressed  True if pressed, false if released.
 * @param timing      Custom thresholds (0 in a field = use engine default).
 */
void kb_system_action_process_ex(uint16_t action_code, bool is_pressed,
                                 const kb_sys_action_timing_t *timing);
