#include "kb_layout.h"
#include "cfg_layouts.h"

/* ============================================================
   Factory-default keymap
   ============================================================
   Defining this here (not in the header) ensures a single copy in flash.
   cfg_layouts.c reads it via the extern declaration in kb_layout.h.
   ============================================================ */

const uint16_t keymaps_base[KB_MATRIX_ROW_COUNT][KB_MATRIX_COL_COUNT] = {
    // Row 0: Esc, 1-0, -, =, Backspace, Insert
    {HID_KEY_ESCAPE, HID_KEY_1, HID_KEY_2, HID_KEY_3, HID_KEY_4, HID_KEY_5,
     HID_KEY_6, HID_KEY_7, HID_KEY_8, HID_KEY_9, HID_KEY_0, HID_KEY_MINUS,
     HID_KEY_EQUAL, HID_KEY_BACKSPACE, HID_KEY_INSERT,
     HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE},
    // Row 1: Tab, Q-P, [, ], \, Home
    {HID_KEY_TAB, HID_KEY_Q, HID_KEY_W, HID_KEY_E, HID_KEY_R, HID_KEY_T,
     HID_KEY_Y, HID_KEY_U, HID_KEY_I, HID_KEY_O, HID_KEY_P,
     HID_KEY_BRACKET_LEFT, HID_KEY_BRACKET_RIGHT, HID_KEY_BACKSLASH,
     HID_KEY_HOME, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE},
    // Row 2: CapsLock, A-L, ;, ', Enter, PgUp
    {HID_KEY_CAPS_LOCK, HID_KEY_A, HID_KEY_S, HID_KEY_D, HID_KEY_F, HID_KEY_G,
     HID_KEY_H, HID_KEY_J, HID_KEY_K, HID_KEY_L, HID_KEY_SEMICOLON,
     HID_KEY_APOSTROPHE, HID_KEY_ENTER, HID_KEY_NONE,
     HID_KEY_PAGE_UP, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE},
    // Row 3: LShift, Z-M, ,, ., /, RShift, Up, PgDn
    {HID_KEY_SHIFT_LEFT, HID_KEY_NONE, HID_KEY_Z, HID_KEY_X, HID_KEY_C,
     HID_KEY_V, HID_KEY_B, HID_KEY_N, HID_KEY_M, HID_KEY_COMMA,
     HID_KEY_PERIOD, HID_KEY_SLASH, HID_KEY_SHIFT_RIGHT,
     HID_KEY_ARROW_UP, HID_KEY_PAGE_DOWN, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE},
    // Row 4: LCtrl, LGui, LAlt, Space, RAlt, FN1, FN2, Left, Down, Right
    {HID_KEY_CONTROL_LEFT, HID_KEY_GUI_LEFT, HID_KEY_ALT_LEFT,
     HID_KEY_NONE, HID_KEY_NONE, HID_KEY_SPACE,
     HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE,
     HID_KEY_ALT_RIGHT, ACTION_CODE_LAYER_MOMENTARY + 1, ACTION_CODE_LAYER_MOMENTARY + 2,
     HID_KEY_ARROW_LEFT, HID_KEY_ARROW_DOWN, HID_KEY_ARROW_RIGHT,
     HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE},
    // Row 5: (unused)
    {HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE,
     HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE,
     HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE,
     HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE},
};

/* ============================================================
   Action code lookup (delegates to config module cache)
   ============================================================ */

#include "cfg_system.h"
#include "esp_log.h"

uint16_t kb_layout_get_action_code(uint8_t row, uint8_t col, uint16_t layer_mask) {
    cfg_system_t sys;
    if (cfg_system_get(&sys) != ESP_OK) {
        sys.transparent_stack_fallback = false;
    }

    if (!sys.transparent_stack_fallback) {
        // Direct to base logic
        uint8_t top_layer = layer_mask ? 31 - __builtin_clz((unsigned)layer_mask) : 0;
        uint16_t kc = cfg_layout_get_action_code(row, col, top_layer);
        
        if (kc == KB_KEY_TRANSPARENT && top_layer != 0) {
            kc = cfg_layout_get_action_code(row, col, 0);
        }
        return kc;
    }

    // Stack fall-through
    uint16_t valid_mask = layer_mask;
    while (valid_mask) {
        uint8_t top_layer = 31 - __builtin_clz((unsigned)valid_mask);
        uint16_t kc = cfg_layout_get_action_code(row, col, top_layer);
        if (kc != KB_KEY_TRANSPARENT) {
            return kc;
        }
        valid_mask &= ~(1u << top_layer);
    }
    
    // Always fall back to Base
    return cfg_layout_get_action_code(row, col, 0);
}
