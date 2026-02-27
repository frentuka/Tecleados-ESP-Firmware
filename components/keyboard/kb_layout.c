#include "kb_layout.h"
#include "class/hid/hid.h"

uint16_t kb_layout_get_action_code(uint8_t row, uint8_t col, uint8_t layer) {
  if (row >= KB_MATRIX_ROW_COUNT || col >= KB_MATRIX_COL_COUNT) {
    return ACTION_CODE_NONE;
  }

  if (layer >= KB_LAYER_COUNT) {
    layer = KB_LAYER_BASE;
  }

  uint16_t kc = keymaps[layer][row][col];
  if (kc == KB_KEY_TRANSPARENT) {
    kc = keymaps[KB_LAYER_BASE][row][col];
  }

  return kc;
}