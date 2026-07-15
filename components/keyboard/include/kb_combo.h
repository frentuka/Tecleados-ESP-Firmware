#pragma once
#include <stdbool.h>
#include <stdint.h>

/** Initialize the combo engine: allocate state, load combos from NVS, register events. */
void kb_combo_init(void);

/**
 * Process a matrix edge event through the combo engine.
 * Called from kb_manager_task at each key-down/key-up edge.
 *
 * @param row        Physical matrix row
 * @param col        Physical matrix column
 * @param is_pressed true = key down, false = key up
 * @param layer      Current active layer
 * @return true if the key event was consumed (caller should skip normal dispatch).
 *         Only returns true when delayedPress mode is active for a matching combo.
 */
bool kb_combo_process_key(uint8_t row, uint8_t col, bool is_pressed, uint16_t layer_mask);

/**
 * Tick the combo timeout engine. Called once per scan cycle.
 * Flushes timed-out suppressed keys (delayedPress mode only).
 *
 * @param now_us Current timestamp (esp_timer_get_time)
 */
void kb_combo_tick(int64_t now_us);
