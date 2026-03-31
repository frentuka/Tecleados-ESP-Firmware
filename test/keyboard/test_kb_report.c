/**
 * @file test_kb_report.c
 * @brief Tests for kb_report.c — HID report routing and NKRO-to-6KRO conversion.
 *
 * Tests the REAL production kb_hid_ready, kb_send_report, and
 * kb_send_consumer_report linked from components/keyboard/kb_report.c.
 * virtual_nkro_to_6kro is static — tested indirectly via kb_send_report.
 */
#include "test_harness.h"

/* All constants (HID_MODIFIER_*, NKRO_*, SIXKRO_*)
   and kb_bit_set provided by test_constants.h.
   Production functions (kb_hid_ready, kb_send_report, kb_send_consumer_report)
   are linked via main.c. Mock USB/BLE state is in mock_tinyusb.h. */

/* ---- Setup ---- */

TEST_SETUP(kb_report) {
    mock_usb_reset();
}

/* ---- NKRO-to-6KRO conversion (tested via real kb_send_report → captured mock reports) ---- */

TEST_CASE(kb_report, send_report_empty_nkro) {
    uint8_t nkro[NKRO_BITMAP_BYTES] = {0};
    _mock_ble_routing_active = false;
    _mock_usb_mounted = true;
    _mock_usb_boot_protocol = true; /* 6KRO mode — easier to inspect */

    esp_err_t err = kb_send_report(nkro);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(1, _mock_report_count);
    /* 6KRO report: [modifier, k1..k6] — all zero */
    TEST_ASSERT_EQUAL(0, _mock_reports[0].data[0]); /* modifier */
    for (int i = 1; i <= 6; i++) TEST_ASSERT_EQUAL(0, _mock_reports[0].data[i]);
}

TEST_CASE(kb_report, send_report_single_key) {
    uint8_t nkro[NKRO_BITMAP_BYTES] = {0};
    kb_bit_set(nkro, 0x04); /* HID_KEY_A */
    _mock_ble_routing_active = false;
    _mock_usb_mounted = true;
    _mock_usb_boot_protocol = true;

    kb_send_report(nkro);
    TEST_ASSERT_EQUAL(0, _mock_reports[0].data[0]); /* no modifier */
    TEST_ASSERT_EQUAL(0x04, _mock_reports[0].data[1]); /* first key */
}

TEST_CASE(kb_report, send_report_modifier_only) {
    uint8_t nkro[NKRO_BITMAP_BYTES] = {0};
    kb_bit_set(nkro, HID_MODIFIER_MIN);     /* Left Control */
    kb_bit_set(nkro, HID_MODIFIER_MIN + 1); /* Left Shift */
    _mock_ble_routing_active = false;
    _mock_usb_mounted = true;
    _mock_usb_boot_protocol = true;

    kb_send_report(nkro);
    TEST_ASSERT_EQUAL(0x03, _mock_reports[0].data[0]); /* bits 0+1 */
    for (int i = 1; i <= 6; i++) TEST_ASSERT_EQUAL(0, _mock_reports[0].data[i]);
}

TEST_CASE(kb_report, send_report_six_keys_plus_modifier) {
    uint8_t nkro[NKRO_BITMAP_BYTES] = {0};
    kb_bit_set(nkro, HID_MODIFIER_MIN); /* Left Control */
    for (int k = 0x04; k <= 0x09; k++) kb_bit_set(nkro, k); /* A-F */
    _mock_ble_routing_active = false;
    _mock_usb_mounted = true;
    _mock_usb_boot_protocol = true;

    kb_send_report(nkro);
    TEST_ASSERT_EQUAL(0x01, _mock_reports[0].data[0]); /* modifier */
    TEST_ASSERT_EQUAL(0x04, _mock_reports[0].data[1]); /* A */
    TEST_ASSERT_EQUAL(0x09, _mock_reports[0].data[6]); /* F */
}

TEST_CASE(kb_report, send_report_more_than_six_truncates) {
    uint8_t nkro[NKRO_BITMAP_BYTES] = {0};
    for (int k = 0x04; k <= 0x0C; k++) kb_bit_set(nkro, k); /* 9 keys */
    _mock_ble_routing_active = false;
    _mock_usb_mounted = true;
    _mock_usb_boot_protocol = true;

    kb_send_report(nkro);
    /* Only first 6 non-modifier keys fit */
    TEST_ASSERT_EQUAL(0x04, _mock_reports[0].data[1]); /* A */
    TEST_ASSERT_EQUAL(0x09, _mock_reports[0].data[6]); /* F */
}

TEST_CASE(kb_report, send_report_all_modifiers) {
    uint8_t nkro[NKRO_BITMAP_BYTES] = {0};
    for (int m = HID_MODIFIER_MIN; m <= HID_MODIFIER_MAX; m++) kb_bit_set(nkro, m);
    _mock_ble_routing_active = false;
    _mock_usb_mounted = true;
    _mock_usb_boot_protocol = true;

    kb_send_report(nkro);
    TEST_ASSERT_EQUAL(0xFF, _mock_reports[0].data[0]);
}

TEST_CASE(kb_report, send_report_nkro_protocol) {
    uint8_t nkro[NKRO_BITMAP_BYTES] = {0};
    kb_bit_set(nkro, 0x04);
    _mock_ble_routing_active = false;
    _mock_usb_mounted = true;
    _mock_usb_boot_protocol = false; /* NKRO mode */

    kb_send_report(nkro);
    TEST_ASSERT_EQUAL(1, _mock_report_count);
    TEST_ASSERT_EQUAL(2, _mock_reports[0].report_id); /* NKRO report */
}

/* ---- Routing tests (real kb_hid_ready) ---- */

TEST_CASE(kb_report, hid_ready_usb_mounted) {
    _mock_ble_routing_active = false;
    _mock_usb_mounted = true;
    _mock_usb_hid_ready[ITF_NUM_HID_KBD] = true;
    TEST_ASSERT_TRUE(kb_hid_ready());
}

TEST_CASE(kb_report, hid_ready_ble_routing) {
    _mock_ble_routing_active = true;
    _mock_ble_connected = true;
    TEST_ASSERT_TRUE(kb_hid_ready());
}

TEST_CASE(kb_report, hid_not_ready_ble_routing_disconnected) {
    _mock_ble_routing_active = true;
    _mock_ble_connected = false;
    TEST_ASSERT_FALSE(kb_hid_ready());
}

TEST_CASE(kb_report, hid_not_ready_usb_not_mounted) {
    _mock_ble_routing_active = false;
    _mock_usb_mounted = false;
    TEST_ASSERT_FALSE(kb_hid_ready());
}

TEST_CASE(kb_report, routing_priority_ble_over_usb) {
    _mock_ble_routing_active = true;
    _mock_ble_connected = true;
    _mock_usb_mounted = true;
    _mock_usb_hid_ready[ITF_NUM_HID_KBD] = true;

    /* Send a report — should go to BLE, not USB */
    uint8_t nkro[NKRO_BITMAP_BYTES] = {0};
    kb_bit_set(nkro, 0x04);
    kb_send_report(nkro);

    /* BLE got the report */
    TEST_ASSERT_EQUAL(1, _mock_ble_send_count);
    /* USB did NOT get a report */
    TEST_ASSERT_EQUAL(0, _mock_report_count);
}

TEST_CASE(kb_report, send_report_fails_when_ble_disconnected) {
    _mock_ble_routing_active = true;
    _mock_ble_connected = false;

    uint8_t nkro[NKRO_BITMAP_BYTES] = {0};
    esp_err_t err = kb_send_report(nkro);
    TEST_ASSERT_EQUAL(ESP_FAIL, err);
}

/* ---- Consumer report routing (real kb_send_consumer_report) ---- */

TEST_CASE(kb_report, consumer_report_via_usb) {
    _mock_ble_routing_active = false;
    _mock_usb_mounted = true;
    _mock_usb_hid_ready[ITF_NUM_HID_KBD] = true;

    esp_err_t err = kb_send_consumer_report(0x00CD); /* Play/Pause */
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(1, _mock_report_count);
    TEST_ASSERT_EQUAL(4, _mock_reports[0].report_id); /* Consumer report */
}

TEST_CASE(kb_report, consumer_report_via_ble) {
    _mock_ble_routing_active = true;
    _mock_ble_connected = true;

    esp_err_t err = kb_send_consumer_report(0x00CD);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(1, _mock_ble_send_count);
}
