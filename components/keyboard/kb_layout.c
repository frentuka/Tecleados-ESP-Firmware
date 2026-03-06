#include "kb_layout.h"
#include "cfg_layouts.h"

uint16_t kb_layout_get_action_code(uint8_t row, uint8_t col, uint8_t layer) {
  return cfg_layout_get_action_code(row, col, layer);
}