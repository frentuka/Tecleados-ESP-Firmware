#pragma once

#include "class/hid/hid.h"
#include "kb_matrix.h"
#include <stdint.h>

#define KB_LAYER_COUNT 3

// default fn keys
// should be overriden by config
#define KB_FN1_ROW 4
#define KB_FN1_COL 2
#define KB_FN2_ROW 4
#define KB_FN2_COL 9

// special marker: falls back to default layer
#define KB_KEY_TRANSPARENT 0xFF

// default
typedef enum {
  KB_LAYER_BASE = 0,
  KB_LAYER_FN1 = 1,
  KB_LAYER_FN2 = 2,
} kb_layer_t;

// default keyboard layout. should be overriden by config
static const uint8_t
    keymaps[KB_LAYER_COUNT][KB_MATRIX_ROW_COUNT][KB_MATRIX_COL_COUNT] = {
        [KB_LAYER_BASE] =
            {// Row 0
             {HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A},
             // Row 1
             {HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A},
             // Row 2
             {HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A},
             // Row 3
             {HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A},
             // Row 4
             {HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A},
             // Row 5
             {HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A}},
        [KB_LAYER_FN1] =
            {// Row 0
             {HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A},
             // Row 1
             {HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A},
             // Row 2
             {HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A},
             // Row 3
             {HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A},
             // Row 4
             {HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A},
             // Row 5
             {HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
              HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A}},
        [KB_LAYER_FN2] = {
            // Row 0
            {HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
             HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
             HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
             HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A},
            // Row 1
            {HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
             HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
             HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
             HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A},
            // Row 2
            {HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
             HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
             HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
             HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A},
            // Row 3
            {HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
             HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
             HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
             HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A},
            // Row 4
            {HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
             HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
             HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
             HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A},
            // Row 5
            {HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
             HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
             HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A,
             HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A, HID_KEY_A}}};

uint8_t kb_layout_get_keycode(uint8_t row, uint8_t col, uint8_t layer);
uint8_t kb_layout_get_active_layer(const uint8_t *matrix);