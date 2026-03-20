#include "kb_report.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "kb_layout.h"
#include "kb_matrix.h"

#include "usb_descriptors.h"
#include "usbmod.h"

#include "ble_hid_service.h"
#include "blemod.h"

/* ---- Helpers ---- */

static void virtual_nkro_to_6kro(const uint8_t *v_nkro,
                                  uint8_t *out_modifiers,
                                  uint8_t  out_basic_keys[6]) {
    memset(out_basic_keys, 0, 6);
    *out_modifiers = 0;
    size_t out = 0;

    for (uint16_t kc = 1; kc < 256; ++kc) {
        if (v_nkro[kc >> 3] & (uint8_t)(1U << (kc & 7U))) {
            if (kc >= 0xE0 && kc <= 0xE7) {
                *out_modifiers |= (uint8_t)(1 << (kc - 0xE0));
            } else if (out < 6) {
                out_basic_keys[out++] = (uint8_t)kc;
            }
        }
    }
}

/* ---- Public API ---- */

bool kb_hid_ready(void) {
    if (ble_hid_is_routing_active()) {
        return ble_hid_is_connected();
    }
    return tud_mounted() && tud_hid_n_ready(ITF_NUM_HID_KBD);
}

esp_err_t kb_send_report(const uint8_t *v_nkro) {
    uint8_t modifiers    = 0;
    uint8_t basic_keys[6] = {0};
    virtual_nkro_to_6kro(v_nkro, &modifiers, basic_keys);

    if (ble_hid_is_routing_active()) {
        if (!ble_hid_is_connected()) return ESP_FAIL;

        uint8_t report[8] = {0};
        report[0] = modifiers;
        memcpy(&report[2], basic_keys, 6);
        return ble_hid_send_keyboard_report(report, 8);
    }

    if (!tud_mounted()) return ESP_FAIL;
    if (!tud_hid_n_ready(ITF_NUM_HID_KBD)) return ESP_FAIL; /* Endpoint busy; caller retries */

    bool ok = usb_keyboard_use_boot_protocol()
              ? usb_send_keyboard_6kro(modifiers, basic_keys)
              : usb_send_keyboard_nkro(modifiers, v_nkro, NKRO_BYTES);
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t kb_send_consumer_report(uint16_t media_keycode) {
    if (ble_hid_is_routing_active()) {
        if (!ble_hid_is_connected()) return ESP_FAIL;
        return ble_hid_send_consumer_report(media_keycode);
    }

    if (!tud_mounted() || !tud_hid_n_ready(ITF_NUM_HID_KBD)) return ESP_FAIL;
    return usb_send_consumer_report(media_keycode) ? ESP_OK : ESP_FAIL;
}
