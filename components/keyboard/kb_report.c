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
    size_t out = 0;

    /* Scan regular keys 0x01-0xDF (0xE0+ are modifiers, handled below).
     * Stops the loop at 0xE0 instead of 256 — saves 32 iterations. */
    for (uint16_t kc = 1; kc < 0xE0; ++kc) {
        if (v_nkro[kc >> 3] & (uint8_t)(1U << (kc & 7U))) {
            if (out < 6) {
                out_basic_keys[out++] = (uint8_t)kc;
            }
        }
    }

    /* Modifier keycodes 0xE0-0xE7 occupy byte 28 (0xE0>>3) of the bitmap.
     * Their bit positions match the HID modifier byte layout directly. */
    *out_modifiers = v_nkro[0xE0 >> 3];
}

/* ---- Public API ---- */

bool kb_hid_ready(void) {
    if (ble_hid_is_routing_active()) {
        // BLE mode: only ready when actually connected.
        // Do NOT fall through to USB — that would split traffic across transports.
        return ble_hid_is_connected();
    }
    return tud_mounted() && tud_hid_n_ready(ITF_NUM_HID_KBD);
}

esp_err_t kb_send_report(const uint8_t *v_nkro) {
    /* --- BLE path (always 6KRO) --- */
    if (ble_hid_is_routing_active()) {
        // BLE routing is enabled: ONLY send via BLE.
        // If not yet connected (e.g. stack initializing on boot), block and let
        // the caller retry — never silently fall through to USB.
        if (!ble_hid_is_connected()) return ESP_ERR_INVALID_STATE;

        uint8_t modifiers = 0;
        uint8_t basic_keys[6] = {0};
        virtual_nkro_to_6kro(v_nkro, &modifiers, basic_keys);

        uint8_t report[8] = {0};
        report[0] = modifiers;
        memcpy(&report[2], basic_keys, 6);
        return ble_hid_send_keyboard_report(report, 8);
    }

    /* --- USB path (only when BLE routing is explicitly off) --- */
    if (!tud_mounted()) return ESP_FAIL;
    if (!tud_hid_n_ready(ITF_NUM_HID_KBD)) return ESP_FAIL;

    if (usb_keyboard_use_boot_protocol()) {
        uint8_t modifiers = 0;
        uint8_t basic_keys[6] = {0};
        virtual_nkro_to_6kro(v_nkro, &modifiers, basic_keys);
        return usb_send_keyboard_6kro(modifiers, basic_keys) ? ESP_OK : ESP_FAIL;
    }

    uint8_t modifiers = v_nkro[0xE0 >> 3];
    return usb_send_keyboard_nkro(modifiers, v_nkro, NKRO_BYTES) ? ESP_OK : ESP_FAIL;
}

esp_err_t kb_send_consumer_report(uint16_t media_keycode) {
    if (ble_hid_is_routing_active()) {
        if (!ble_hid_is_connected()) return ESP_ERR_INVALID_STATE;
        return ble_hid_send_consumer_report(media_keycode);
    }

    if (!tud_mounted() || !tud_hid_n_ready(ITF_NUM_HID_KBD)) return ESP_FAIL;
    return usb_send_consumer_report(media_keycode) ? ESP_OK : ESP_FAIL;
}
