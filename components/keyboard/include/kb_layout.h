#pragma once

#include "class/hid/hid.h"
#include "kb_matrix.h"
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
#define SYS_ACTION_LAYER_BASE  (ACTION_CODE_SYSTEM_MIN + 0)
#define SYS_ACTION_LAYER_FN1   (ACTION_CODE_SYSTEM_MIN + 1)
#define SYS_ACTION_LAYER_FN2   (ACTION_CODE_SYSTEM_MIN + 2)

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

/* ---- Layers ---- */
#define KB_LAYER_COUNT 4

typedef enum {
    KB_LAYER_BASE = 0,
    KB_LAYER_FN1  = 1,
    KB_LAYER_FN2  = 2,
    KB_LAYER_FN3  = 3, /* FN3 = FN1 + FN2 held simultaneously */
} kb_layer_t;

/* ---- Default compile-time keymap ---- */
/* Defined once in kb_layout.c; used by cfg_layouts.c as the factory default. */
extern const uint16_t keymaps[KB_LAYER_COUNT][KB_MATRIX_ROW_COUNT][KB_MATRIX_COL_COUNT];

/* ---- Lookup ---- */
uint16_t kb_layout_get_action_code(uint8_t row, uint8_t col, uint8_t layer);
