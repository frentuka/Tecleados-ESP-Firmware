#pragma once

#include "class/hid/hid.h"
#include "kb_matrix.h"
#include "cfg_layouts.h" // For CFG_LAYOUT_MAX_COUNT
#include <stdint.h>

/* ---- Special marker ---- */
#define KB_KEY_TRANSPARENT 0xFFFF  /* Falls back to the base layer */

/* ---- Action code spaces (16-bit values) ---- */
#define ACTION_CODE_NONE      0x0000

#define ACTION_CODE_HID_MIN   0x0001
#define ACTION_CODE_HID_MAX   0x00FF

#define ACTION_CODE_MEDIA_MIN 0x0100 /* Reserved for individual consumer key codes */
#define ACTION_CODE_MEDIA_MAX 0x01FF

#define ACTION_CODE_SYSTEM_MIN 0x2000
#define ACTION_CODE_SYSTEM_MAX 0x20FF

#define ACTION_CODE_CKEY_MIN  0x3000
#define ACTION_CODE_CKEY_MAX  0x3FFF

#define ACTION_CODE_MACRO_MIN 0x4000
#define ACTION_CODE_MACRO_MAX 0x4FFF

/* ---- System action codes (within ACTION_CODE_SYSTEM_*) ---- */
// Removed hardcoded SYS_ACTION_LAYER_* as layers now use the 0x5000 block

#define SYS_ACTION_BLE_ON      (ACTION_CODE_SYSTEM_MIN + 3)
#define SYS_ACTION_BLE_OFF     (ACTION_CODE_SYSTEM_MIN + 4)
#define SYS_ACTION_BLE_TOGGLE  (ACTION_CODE_SYSTEM_MIN + 5)
#define SYS_ACTION_BLE_1       (ACTION_CODE_SYSTEM_MIN + 6)
#define SYS_ACTION_BLE_2       (ACTION_CODE_SYSTEM_MIN + 7)
#define SYS_ACTION_BLE_3       (ACTION_CODE_SYSTEM_MIN + 8)
#define SYS_ACTION_BLE_4       (ACTION_CODE_SYSTEM_MIN + 9)
#define SYS_ACTION_BLE_5       (ACTION_CODE_SYSTEM_MIN + 10)
#define SYS_ACTION_BLE_6       (ACTION_CODE_SYSTEM_MIN + 11)
#define SYS_ACTION_BLE_7       (ACTION_CODE_SYSTEM_MIN + 12)
#define SYS_ACTION_BLE_8       (ACTION_CODE_SYSTEM_MIN + 13)
#define SYS_ACTION_BLE_9       (ACTION_CODE_SYSTEM_MIN + 14)

#define SYS_ACTION_BRIGHTNESS_UP   (ACTION_CODE_SYSTEM_MIN + 16)
#define SYS_ACTION_BRIGHTNESS_DOWN (ACTION_CODE_SYSTEM_MIN + 17)

#define SYS_ACTION_VOLUME_UP   (ACTION_CODE_SYSTEM_MIN + 18)
#define SYS_ACTION_VOLUME_DOWN (ACTION_CODE_SYSTEM_MIN + 19)
#define SYS_ACTION_MUTE        (ACTION_CODE_SYSTEM_MIN + 20)

#define MEDIA_ACTION_NEXT      (ACTION_CODE_SYSTEM_MIN + 21)
#define MEDIA_ACTION_PREV      (ACTION_CODE_SYSTEM_MIN + 22)
#define MEDIA_ACTION_TOGGLE    (ACTION_CODE_SYSTEM_MIN + 23)
#define MEDIA_ACTION_PLAY      MEDIA_ACTION_TOGGLE

#define SYS_ACTION_RGB_MODE_NEXT       (ACTION_CODE_SYSTEM_MIN + 24)
#define SYS_ACTION_RGB_MODE_PREV       (ACTION_CODE_SYSTEM_MIN + 25)
#define SYS_ACTION_RGB_SPEED_NEXT      (ACTION_CODE_SYSTEM_MIN + 26)
#define SYS_ACTION_RGB_SPEED_PREV      (ACTION_CODE_SYSTEM_MIN + 27)
#define SYS_ACTION_RGB_BRIGHTNESS_UP   (ACTION_CODE_SYSTEM_MIN + 28)
#define SYS_ACTION_RGB_BRIGHTNESS_DOWN (ACTION_CODE_SYSTEM_MIN + 29)

/* ---- Split keyboard system actions ---- */
#define SYS_ACTION_SPLIT_PAIR          (ACTION_CODE_SYSTEM_MIN + 30)
#define SYS_ACTION_SPLIT_ROLE_SWAP     (ACTION_CODE_SYSTEM_MIN + 31)
#define SYS_ACTION_SPLIT_DISCONNECT    (ACTION_CODE_SYSTEM_MIN + 32)

/* ---- Layers ---- */
#define KB_LAYER_MAX       CFG_LAYOUT_MAX_COUNT  // 16
#define KB_LAYER_BASE      0

/* ---- Layer action codes (within ACTION_CODE_LAYER_MIN) ---- */
#define ACTION_CODE_LAYER_MIN          0x5000
#define ACTION_CODE_LAYER_MOMENTARY    (ACTION_CODE_LAYER_MIN + 0x00) // 0x5000 - 0x500F
#define ACTION_CODE_LAYER_TOGGLE       (ACTION_CODE_LAYER_MIN + 0x10) // 0x5010 - 0x501F
#define ACTION_CODE_LAYER_ON           (ACTION_CODE_LAYER_MIN + 0x20) // 0x5020 - 0x502F
#define ACTION_CODE_LAYER_OFF          (ACTION_CODE_LAYER_MIN + 0x30) // 0x5030 - 0x503F
#define ACTION_CODE_LAYER_CLEAR_ALL    (ACTION_CODE_LAYER_MIN + 0x40) // 0x5040 - Return to Base (Panic)

// Utility checks:
#define IS_LAYER_ACTION(a)    ((a) >= ACTION_CODE_LAYER_MIN && (a) <= ACTION_CODE_LAYER_CLEAR_ALL)
#define IS_LAYER_MOMENTARY(a) ((a) >= ACTION_CODE_LAYER_MOMENTARY && (a) <= ACTION_CODE_LAYER_MOMENTARY + 15)
#define IS_LAYER_TOGGLE(a)    ((a) >= ACTION_CODE_LAYER_TOGGLE && (a) <= ACTION_CODE_LAYER_TOGGLE + 15)
#define IS_LAYER_ON(a)        ((a) >= ACTION_CODE_LAYER_ON && (a) <= ACTION_CODE_LAYER_ON + 15)
#define IS_LAYER_OFF(a)       ((a) >= ACTION_CODE_LAYER_OFF && (a) <= ACTION_CODE_LAYER_OFF + 15)
#define LAYER_ID_FROM_ACTION(a) ((uint8_t)((a) & 0x0F))

/* ---- Default compile-time keymap ---- */
/* Defined once in kb_layout.c; used by cfg_layouts.c as the factory default. */
extern const uint16_t keymaps_base[KB_MATRIX_ROW_COUNT][KB_MATRIX_COL_COUNT];

/* ---- Lookup ---- */
uint16_t kb_layout_get_action_code(uint8_t row, uint8_t col, uint16_t layer_mask);
