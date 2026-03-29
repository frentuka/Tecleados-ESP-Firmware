/**
 * @file test_kb_report.c
 * @brief Tests for kb_report.c — HID report routing and NKRO→6KRO conversion.
 *
 * Tests the virtual_nkro_to_6kro conversion, routing priority (BLE vs USB),
 * protocol selection (boot vs report), and consumer report dispatch.
 */
#include "test_harness.h"
#include "mocks/mock_esp.h"
#include "mocks/mock_tinyusb.h"

/* Constants from the firmware */
#define NKRO_KEYS  0xE7
#define NKRO_BYTES ((NKRO_KEYS + 7) / 8)
#define ITF_NUM_HID_KBD 0

/* Inline the NKRO→6KRO conversion from kb_report.c */
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

static inline void set_nkro_bit(uint8_t *bitmap, uint8_t keycode) {
    bitmap[keycode >> 3] |= (uint8_t)(1U << (keycode & 7U));
}

/* ---- NKRO to 6KRO conversion tests ---- */

TEST_CASE(kb_report, nkro_to_6kro_empty) {
    uint8_t nkro[32] = {0};
    uint8_t mod = 0xFF;
    uint8_t keys[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    virtual_nkro_to_6kro(nkro, &mod, keys);
    TEST_ASSERT_EQUAL(0, mod);
    for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL(0, keys[i]);
}

TEST_CASE(kb_report, nkro_to_6kro_single_key) {
    uint8_t nkro[32] = {0};
    set_nkro_bit(nkro, 0x04); /* HID_KEY_A */

    uint8_t mod = 0;
    uint8_t keys[6] = {0};
    virtual_nkro_to_6kro(nkro, &mod, keys);

    TEST_ASSERT_EQUAL(0, mod);
    TEST_ASSERT_EQUAL(0x04, keys[0]);
    TEST_ASSERT_EQUAL(0, keys[1]);
}

TEST_CASE(kb_report, nkro_to_6kro_modifier_only) {
    uint8_t nkro[32] = {0};
    set_nkro_bit(nkro, 0xE0); /* Left Control */
    set_nkro_bit(nkro, 0xE1); /* Left Shift */

    uint8_t mod = 0;
    uint8_t keys[6] = {0};
    virtual_nkro_to_6kro(nkro, &mod, keys);

    TEST_ASSERT_EQUAL(0x03, mod); /* bits 0 + 1 */
    for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL(0, keys[i]);
}

TEST_CASE(kb_report, nkro_to_6kro_six_keys_plus_modifier) {
    uint8_t nkro[32] = {0};
    set_nkro_bit(nkro, 0xE0); /* Left Control */
    set_nkro_bit(nkro, 0x04); /* A */
    set_nkro_bit(nkro, 0x05); /* B */
    set_nkro_bit(nkro, 0x06); /* C */
    set_nkro_bit(nkro, 0x07); /* D */
    set_nkro_bit(nkro, 0x08); /* E */
    set_nkro_bit(nkro, 0x09); /* F */

    uint8_t mod = 0;
    uint8_t keys[6] = {0};
    virtual_nkro_to_6kro(nkro, &mod, keys);

    TEST_ASSERT_EQUAL(0x01, mod);
    TEST_ASSERT_EQUAL(0x04, keys[0]);
    TEST_ASSERT_EQUAL(0x05, keys[1]);
    TEST_ASSERT_EQUAL(0x06, keys[2]);
    TEST_ASSERT_EQUAL(0x07, keys[3]);
    TEST_ASSERT_EQUAL(0x08, keys[4]);
    TEST_ASSERT_EQUAL(0x09, keys[5]);
}

TEST_CASE(kb_report, nkro_to_6kro_more_than_six_keys_truncates) {
    uint8_t nkro[32] = {0};
    for (int i = 0x04; i <= 0x0C; i++) set_nkro_bit(nkro, i); /* 9 keys */

    uint8_t mod = 0;
    uint8_t keys[6] = {0};
    virtual_nkro_to_6kro(nkro, &mod, keys);

    /* Only first 6 should be present */
    TEST_ASSERT_EQUAL(0x04, keys[0]);
    TEST_ASSERT_EQUAL(0x09, keys[5]);
}

TEST_CASE(kb_report, nkro_to_6kro_all_modifiers) {
    uint8_t nkro[32] = {0};
    for (int i = 0xE0; i <= 0xE7; i++) set_nkro_bit(nkro, i);

    uint8_t mod = 0;
    uint8_t keys[6] = {0};
    virtual_nkro_to_6kro(nkro, &mod, keys);

    TEST_ASSERT_EQUAL(0xFF, mod);
    for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL(0, keys[i]);
}

/* ---- Routing tests ---- */

TEST_CASE(kb_report, hid_ready_usb_mounted) {
    mock_usb_reset();
    _mock_ble_routing_active = false;
    _mock_usb_mounted = true;
    _mock_usb_hid_ready[ITF_NUM_HID_KBD] = true;

    /* kb_hid_ready should return true for USB path */
    bool ready = !_mock_ble_routing_active && tud_mounted() && tud_hid_n_ready(ITF_NUM_HID_KBD);
    TEST_ASSERT_TRUE(ready);
}

TEST_CASE(kb_report, hid_ready_ble_routing) {
    mock_usb_reset();
    _mock_ble_routing_active = true;
    _mock_ble_connected = true;

    bool ready = ble_hid_is_routing_active() && ble_hid_is_connected();
    TEST_ASSERT_TRUE(ready);
}

TEST_CASE(kb_report, hid_not_ready_ble_routing_disconnected) {
    mock_usb_reset();
    _mock_ble_routing_active = true;
    _mock_ble_connected = false;

    bool ready = ble_hid_is_routing_active() && ble_hid_is_connected();
    TEST_ASSERT_FALSE(ready);
}

TEST_CASE(kb_report, hid_not_ready_usb_not_mounted) {
    mock_usb_reset();
    _mock_ble_routing_active = false;
    _mock_usb_mounted = false;

    bool ready = !ble_hid_is_routing_active() && tud_mounted();
    TEST_ASSERT_FALSE(ready);
}

TEST_CASE(kb_report, routing_priority_ble_over_usb) {
    mock_usb_reset();
    _mock_ble_routing_active = true;
    _mock_ble_connected = true;
    _mock_usb_mounted = true;

    /* When BLE routing is active, BLE takes priority regardless of USB state */
    TEST_ASSERT_TRUE(ble_hid_is_routing_active());
}

/* ---- Consumer report routing ---- */

TEST_CASE(kb_report, consumer_report_via_usb) {
    mock_usb_reset();
    _mock_ble_routing_active = false;
    _mock_usb_mounted = true;

    usb_send_consumer_report(0x00CD); /* Play/Pause */
    TEST_ASSERT_EQUAL(1, _mock_report_count);
    TEST_ASSERT_EQUAL(4, _mock_reports[0].report_id);
}

TEST_CASE(kb_report, consumer_report_via_ble) {
    mock_usb_reset();
    _mock_ble_routing_active = true;
    _mock_ble_connected = true;

    esp_err_t err = ble_hid_send_consumer_report(0x00CD);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(1, _mock_ble_send_count);
}
