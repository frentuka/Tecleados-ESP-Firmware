#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize the Custom Key engine.
 * Loads all custom keys from NVS into a RAM table.
 * Must be called once from kb_manager_start(), after kb_system_action_init().
 */
void kb_custom_key_init(void);

/**
 * @brief Reload the Custom Key table from NVS.
 * Called by the cfgmod on_update callback whenever the user saves/deletes a custom key.
 */
void kb_custom_key_reload(const char *key);

/**
 * @brief Process a key press/release whose action code is in
 * [ACTION_CODE_CKEY_MIN, ACTION_CODE_CKEY_MAX].
 *
 * Called by kb_macro_process_action() for codes in that range.
 *
 * @param action_code  Full 16-bit action code (e.g. 0x3000 + id).
 * @param is_pressed   true = key down, false = key up.
 */
void kb_custom_key_process_action(uint16_t action_code, bool is_pressed);
