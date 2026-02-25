#include "kb_report.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "kb_layout.h"
#include "kb_matrix.h"

#include "usb_descriptors.h"
#include "usbmod.h"

#include "ble_hid_service.h"
#include "blemod.h"

static inline void set_bit(uint8_t *bitmap, size_t bit_index) {
  bitmap[bit_index >> 3] |= (uint8_t)(1U << (bit_index & 7U));
}

static void matrix_to_6kro(const uint8_t *matrix, uint8_t keycodes[6]) {
  memset(keycodes, 0, 6);
  size_t out = 0;

  for (uint8_t r = 0; r < KB_MATRIX_ROW_COUNT && out < 6; ++r) {
    for (uint8_t c = 0; c < KB_MATRIX_COL_COUNT && out < 6; ++c) {
      size_t bit_index = (r * KB_MATRIX_COL_COUNT) + c;
      uint8_t byte = matrix[bit_index >> 3];
      uint8_t bit = (uint8_t)(1U << (bit_index & 7U));
      if (byte & bit) {
        uint8_t layer = kb_layout_get_active_layer(matrix);
        uint8_t kc = kb_layout_get_keycode(r, c, layer);
        if (kc != HID_KEY_NONE) {
          keycodes[out++] = kc;
        }
      }
    }
  }
}

static void matrix_to_nkro(const uint8_t *matrix, uint8_t *nkro) {
  memset(nkro, 0, NKRO_BYTES);

  for (uint8_t r = 0; r < KB_MATRIX_ROW_COUNT; ++r) {
    for (uint8_t c = 0; c < KB_MATRIX_COL_COUNT; ++c) {
      size_t bit_index = (r * KB_MATRIX_COL_COUNT) + c;
      uint8_t byte = matrix[bit_index >> 3];
      uint8_t bit = (uint8_t)(1U << (bit_index & 7U));
      if (byte & bit) {
        uint8_t layer = kb_layout_get_active_layer(matrix);
        uint8_t kc = kb_layout_get_keycode(r, c, layer);
        if (kc != HID_KEY_NONE && kc < NKRO_KEYS) {
          set_bit(nkro, kc);
        }
      }
    }
  }
}

esp_err_t kb_send_report(const uint8_t *matrix) {
  esp_err_t final_result = ESP_FAIL;

  // Extract keys and separate standard keys from modifiers (0xE0 - 0xE7)
  uint8_t keys[6] = {0};
  uint8_t modifiers = 0;
  uint8_t basic_keys[6] = {0};
  int out_idx = 0;

  matrix_to_6kro(matrix, keys);

  for (int i = 0; i < 6; i++) {
    if (keys[i] >= 0xE0 && keys[i] <= 0xE7) {
      // It's a modifier key (e.g., Left Ctrl is 0xE0, bit 0)
      modifiers |= (1 << (keys[i] - 0xE0));
    } else if (keys[i] != 0 && out_idx < 6) {
      // Standard keycode
      basic_keys[out_idx++] = keys[i];
    }
  }

  // 1. Send via BLE if connected
  if (ble_hid_is_connected()) {
    uint8_t report[8] = {0};
    report[0] = modifiers;
    // report[1] is reserved (0)
    memcpy(&report[2], basic_keys, 6);

    if (ble_hid_send_keyboard_report(report, 8) == ESP_OK) {
      final_result = ESP_OK;
    }
  }

  // 2. Send via USB if mounted and ready
  if (tud_mounted() && tud_hid_n_ready(ITF_NUM_HID_KBD)) {
    bool result = false;

    if (usb_keyboard_use_boot_protocol()) {
      result = usb_send_keyboard_6kro(modifiers, basic_keys);
    } else {
      uint8_t s_nkro[NKRO_BYTES];
      matrix_to_nkro(matrix, s_nkro);
      result = usb_send_keyboard_nkro(s_nkro, NKRO_BYTES);
    }

    if (result) {
      final_result = ESP_OK;
    }
  }

  return final_result;
}