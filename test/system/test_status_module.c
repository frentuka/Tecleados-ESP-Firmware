/**
 * @file test_status_module.c
 * @brief Tests for statusmod.c — real production event handlers,
 *        BLE event processing, config event handling, and status push.
 *
 * Links against real statusmod.c (included in main.c).
 * Uses mock event system to post events and dispatch to real handlers,
 * then verifies the JSON status push via mock send_payload.
 */
#include "test_harness.h"
#include "mocks/mock_esp.h"
#include "mocks/mock_esp_event.h"
#include "mocks/mock_tinyusb.h"

/* Production: status_module_init() from linked statusmod.c.
   Event bases/IDs from linked event_bus.c via event_bus.h.
   cfg_ble_get_state() from cfg_ble.h shim (controllable mock).
   send_payload() from mock_tinyusb.h (captures payload).
   usbmod_register_callback() from mock_tinyusb.h. */

TEST_SETUP(status_module) {
    mock_events_reset();
    mock_usb_reset();
    mock_ble_state_reset();
    /* Init with default BLE state (profile 0, USB mode) */
    status_module_init();
    /* Clear posted events but keep handlers registered by init */
    _mock_event_count = 0;
    _mock_tx_payload_len = 0;
}

/* Helper: clear posted events without wiping handler registrations */
static void clear_posted_events(void) {
    _mock_event_count = 0;
}

/* Helper: post a BLE event and dispatch it to the real handler */
static void post_and_dispatch_ble(int32_t event_id, void *data, size_t size) {
    clear_posted_events();
    esp_event_post(BLE_EVENTS, event_id, data, size, 0);
    mock_event_dispatch_all();
}

/* Helper: check that a status push was sent (payload len > 0) */
static bool status_was_pushed(void) {
    return _mock_tx_payload_len > 0;
}

/* Helper: extract JSON string from the status push payload.
   Format: 7-byte header + JSON. Returns NULL if no push or too short. */
static const char *get_status_json(void) {
    if (_mock_tx_payload_len <= 7) return NULL;
    return (const char *)(_mock_tx_payload + 7);
}

/* Helper: check JSON contains a substring (safe with NULL) */
static bool json_contains(const char *json, const char *needle) {
    if (!json || !needle) return false;
    return strstr(json, needle) != NULL;
}

/* ---- Init tests ---- */

TEST_CASE(status_module, init_registers_handlers) {
    mock_events_reset();
    mock_usb_reset();
    mock_ble_state_reset();
    status_module_init();

    /* Should have registered 2 event handlers + 1 USB callback */
    TEST_ASSERT(_mock_handler_count >= 2);
    TEST_ASSERT_NOT_NULL(_mock_usb_callbacks[MODULE_STATUS]);
}

TEST_CASE(status_module, init_seeds_cache_from_ble_state) {
    mock_events_reset();
    mock_usb_reset();
    mock_ble_state_set(3, true);
    status_module_init();

    /* After init, the first event should include the seeded state.
       Trigger a push via the USB callback. */
    _mock_tx_payload_len = 0;
    _mock_usb_callbacks[MODULE_STATUS](NULL, 0);

    TEST_ASSERT_TRUE(status_was_pushed());
    const char *json = get_status_json();
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_TRUE(json_contains(json, "\"profile\":3"));
    TEST_ASSERT_TRUE(json_contains(json, "\"mode\":1"));
}

/* ---- BLE event: profile connected ---- */

TEST_CASE(status_module, profile_connected_updates_bitmap) {
    int idx = 3;
    post_and_dispatch_ble(BLE_EVENT_PROFILE_CONNECTED, &idx, sizeof(idx));

    TEST_ASSERT_TRUE(status_was_pushed());
    const char *json = get_status_json();
    TEST_ASSERT_NOT_NULL(json);
    /* Bitmap should have bit 3 set = 8 */
    TEST_ASSERT_TRUE(json_contains(json, "\"bitmap\":8"));
}

TEST_CASE(status_module, profile_disconnected_clears_bitmap) {
    /* Connect then disconnect */
    int idx = 3;
    post_and_dispatch_ble(BLE_EVENT_PROFILE_CONNECTED, &idx, sizeof(idx));

    _mock_tx_payload_len = 0;
    post_and_dispatch_ble(BLE_EVENT_PROFILE_DISCONNECTED, &idx, sizeof(idx));

    const char *json = get_status_json();
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_TRUE(json_contains(json, "\"bitmap\":0"));
}

TEST_CASE(status_module, multiple_profiles_connected) {
    int idx0 = 0, idx2 = 2, idx8 = 8;
    post_and_dispatch_ble(BLE_EVENT_PROFILE_CONNECTED, &idx0, sizeof(idx0));
    post_and_dispatch_ble(BLE_EVENT_PROFILE_CONNECTED, &idx2, sizeof(idx2));
    _mock_tx_payload_len = 0;
    post_and_dispatch_ble(BLE_EVENT_PROFILE_CONNECTED, &idx8, sizeof(idx8));

    const char *json = get_status_json();
    TEST_ASSERT_NOT_NULL(json);
    /* bits 0,2,8 = 1+4+256 = 261 */
    TEST_ASSERT_TRUE(json_contains(json, "\"bitmap\":261"));
}

/* ---- BLE event: pairing ---- */

TEST_CASE(status_module, pairing_started_sets_profile) {
    int profile = 5;
    post_and_dispatch_ble(BLE_EVENT_PAIRING_STARTED, &profile, sizeof(profile));

    const char *json = get_status_json();
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_TRUE(json_contains(json, "\"pairing\":5"));
}

TEST_CASE(status_module, pairing_complete_clears_pairing) {
    int profile = 2;
    post_and_dispatch_ble(BLE_EVENT_PAIRING_STARTED, &profile, sizeof(profile));

    ble_pairing_result_t result = { .profile_idx = 2 };
    _mock_tx_payload_len = 0;
    post_and_dispatch_ble(BLE_EVENT_PAIRING_COMPLETE, &result, sizeof(result));

    const char *json = get_status_json();
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_TRUE(json_contains(json, "\"pairing\":-1"));
    TEST_ASSERT_TRUE(json_contains(json, "\"profile\":2"));
}

TEST_CASE(status_module, pairing_failed_clears_pairing) {
    int profile = 1;
    post_and_dispatch_ble(BLE_EVENT_PAIRING_STARTED, &profile, sizeof(profile));

    _mock_tx_payload_len = 0;
    post_and_dispatch_ble(BLE_EVENT_PAIRING_FAILED, &profile, sizeof(profile));

    const char *json = get_status_json();
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_TRUE(json_contains(json, "\"pairing\":-1"));
}

TEST_CASE(status_module, pairing_timeout_clears_pairing) {
    int profile = 4;
    post_and_dispatch_ble(BLE_EVENT_PAIRING_STARTED, &profile, sizeof(profile));

    _mock_tx_payload_len = 0;
    post_and_dispatch_ble(BLE_EVENT_PAIRING_TIMEOUT, &profile, sizeof(profile));

    const char *json = get_status_json();
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_TRUE(json_contains(json, "\"pairing\":-1"));
}

/* ---- BLE event: routing changed ---- */

TEST_CASE(status_module, routing_changed_to_ble) {
    bool active = true;
    post_and_dispatch_ble(BLE_EVENT_ROUTING_CHANGED, &active, sizeof(active));

    const char *json = get_status_json();
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_TRUE(json_contains(json, "\"mode\":1"));
}

TEST_CASE(status_module, routing_changed_to_usb) {
    bool active = true;
    post_and_dispatch_ble(BLE_EVENT_ROUTING_CHANGED, &active, sizeof(active));

    active = false;
    _mock_tx_payload_len = 0;
    post_and_dispatch_ble(BLE_EVENT_ROUTING_CHANGED, &active, sizeof(active));

    const char *json = get_status_json();
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_TRUE(json_contains(json, "\"mode\":0"));
}

/* ---- Edge cases ---- */

TEST_CASE(status_module, disconnect_nonexistent_safe) {
    int idx = 7;
    post_and_dispatch_ble(BLE_EVENT_PROFILE_DISCONNECTED, &idx, sizeof(idx));

    /* Should still push (with bitmap=0) without crashing */
    TEST_ASSERT_TRUE(status_was_pushed());
    const char *json = get_status_json();
    TEST_ASSERT_TRUE(json_contains(json, "\"bitmap\":0"));
}

TEST_CASE(status_module, boundary_profile_index_15) {
    int idx = 15;
    post_and_dispatch_ble(BLE_EVENT_PROFILE_CONNECTED, &idx, sizeof(idx));

    const char *json = get_status_json();
    TEST_ASSERT_NOT_NULL(json);
    /* bit 15 = 32768 */
    TEST_ASSERT_TRUE(json_contains(json, "\"bitmap\":32768"));
}

/* ---- Manual USB callback ---- */

TEST_CASE(status_module, usb_callback_triggers_push) {
    _mock_tx_payload_len = 0;
    /* Call the registered USB callback directly */
    TEST_ASSERT_NOT_NULL(_mock_usb_callbacks[MODULE_STATUS]);
    _mock_usb_callbacks[MODULE_STATUS](NULL, 0);
    TEST_ASSERT_TRUE(status_was_pushed());
}

/* ---- Full event sequence ---- */

TEST_CASE(status_module, full_ble_event_sequence) {
    int idx = 1;
    bool active = true;
    ble_pairing_result_t result = { .profile_idx = 1 };

    post_and_dispatch_ble(BLE_EVENT_PROFILE_CONNECTED, &idx, sizeof(idx));
    post_and_dispatch_ble(BLE_EVENT_PAIRING_STARTED, &idx, sizeof(idx));
    post_and_dispatch_ble(BLE_EVENT_PAIRING_COMPLETE, &result, sizeof(result));
    post_and_dispatch_ble(BLE_EVENT_ROUTING_CHANGED, &active, sizeof(active));

    /* Final state: profile 1 connected, BLE mode, no pairing */
    _mock_tx_payload_len = 0;
    post_and_dispatch_ble(BLE_EVENT_PROFILE_DISCONNECTED, &idx, sizeof(idx));

    const char *json = get_status_json();
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_TRUE(json_contains(json, "\"mode\":1"));
    TEST_ASSERT_TRUE(json_contains(json, "\"profile\":1"));
    TEST_ASSERT_TRUE(json_contains(json, "\"pairing\":-1"));
    TEST_ASSERT_TRUE(json_contains(json, "\"bitmap\":0"));
}
