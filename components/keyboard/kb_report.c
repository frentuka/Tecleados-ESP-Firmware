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

static void virtual_nkro_to_6kro(const uint8_t *v_nkro, uint8_t *modifiers,
                                 uint8_t basic_keys[6]) {
  memset(basic_keys, 0, 6);
  *modifiers = 0;
  size_t out = 0;

  for (uint16_t kc = 1; kc < 256; ++kc) {
    size_t byte_idx = kc >> 3;
    uint8_t bit = (uint8_t)(1U << (kc & 7U));
    if (v_nkro[byte_idx] & bit) {
      if (kc >= 0xE0 && kc <= 0xE7) {
        *modifiers |= (1 << (kc - 0xE0));
      } else if (out < 6) {
        basic_keys[out++] = (uint8_t)kc;
      }
    }
  }
}

esp_err_t kb_send_report(const uint8_t *v_nkro) {
  esp_err_t final_result = ESP_FAIL;

  // Extract keys and separate standard keys from modifiers (0xE0 - 0xE7)
  uint8_t modifiers = 0;
  uint8_t basic_keys[6] = {0};

  virtual_nkro_to_6kro(v_nkro, &modifiers, basic_keys);

  // 1. Prioritize USB if mounted
  if (tud_mounted()) {
    if (tud_hid_n_ready(ITF_NUM_HID_KBD)) {
      bool result = false;
      if (usb_keyboard_use_boot_protocol()) {
        result = usb_send_keyboard_6kro(modifiers, basic_keys);
      } else {
        result = usb_send_keyboard_nkro(modifiers, v_nkro, NKRO_BYTES);
      }
      if (result) {
        final_result = ESP_OK;
      }
    } else {
      // USB is busy; return FAIL so the caller can retry.
      // Do NOT fall back to BLE here to avoid interleaved reports on different transports.
      return ESP_FAIL;
    }
  }
  // 2. Otherwise send via BLE if connected
  else if (ble_hid_is_connected()) {
    uint8_t report[8] = {0};
    report[0] = modifiers;
    memcpy(&report[2], basic_keys, 6);
    if (ble_hid_send_keyboard_report(report, 8) == ESP_OK) {
      final_result = ESP_OK;
    }
  }

  return final_result;
}

esp_err_t kb_send_consumer_report(uint16_t media_keycode) {
  esp_err_t final_result = ESP_FAIL;

  // 1. Send via BLE if connected
  if (ble_hid_is_connected()) {
    if (ble_hid_send_consumer_report(media_keycode) == ESP_OK) {
      final_result = ESP_OK;
    }
  }

  // 2. Send via USB if mounted and ready
  if (tud_mounted() && tud_hid_n_ready(ITF_NUM_HID_KBD)) {
    if (usb_send_consumer_report(media_keycode)) {
      final_result = ESP_OK;
    }
  }

  return final_result;
}