/**
 * @file test_event_bus.c
 * @brief Tests for event_bus.c — event base definitions, payload structs,
 *        and mock event system integration.
 */
#include "test_harness.h"
#include "mocks/mock_esp.h"
#include "mocks/mock_esp_event.h"

/* Event bases (KB_EVENTS, BLE_EVENTS, CONFIG_EVENTS) come from the linked
   event_bus.c. Payload structs (kb_sys_action_event_t, ble_pairing_result_t,
   config_update_event_t) come from event_bus.h via test_constants.h. */

/* ---- Tests ---- */

TEST_CASE(event_bus, event_bases_are_distinct) {
    TEST_ASSERT(KB_EVENTS != BLE_EVENTS);
    TEST_ASSERT(KB_EVENTS != CONFIG_EVENTS);
    TEST_ASSERT(BLE_EVENTS != CONFIG_EVENTS);
}

TEST_CASE(event_bus, post_and_retrieve_kb_event) {
    mock_events_reset();
    kb_sys_action_event_t payload = { .action_code = 0x2005, .event = 4 };
    esp_event_post(KB_EVENTS, 0, &payload, sizeof(payload), 0);

    TEST_ASSERT_EQUAL(1, mock_event_count());
    const mock_posted_event_t *ev = mock_event_get(0);
    TEST_ASSERT(ev->base == KB_EVENTS);
    TEST_ASSERT_EQUAL(0, ev->event_id);

    kb_sys_action_event_t retrieved;
    memcpy(&retrieved, ev->data, sizeof(retrieved));
    TEST_ASSERT_EQUAL_HEX(0x2005, retrieved.action_code);
    TEST_ASSERT_EQUAL(4, retrieved.event);
}

TEST_CASE(event_bus, post_ble_pairing_complete) {
    mock_events_reset();
    ble_pairing_result_t result = {
        .profile_idx = 3,
        .addr_type = 1,
        .addr = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
    };
    esp_event_post(BLE_EVENTS, 4, &result, sizeof(result), 0);

    const mock_posted_event_t *ev = mock_event_get(0);
    TEST_ASSERT_NOT_NULL(ev);

    ble_pairing_result_t got;
    memcpy(&got, ev->data, sizeof(got));
    TEST_ASSERT_EQUAL(3, got.profile_idx);
    TEST_ASSERT_EQUAL(0xAA, got.addr[0]);
    TEST_ASSERT_EQUAL(0xFF, got.addr[5]);
}

TEST_CASE(event_bus, post_config_update) {
    mock_events_reset();
    config_update_event_t ev = { .kind = 0 };
    strlcpy(ev.key, "ly0", sizeof(ev.key));
    esp_event_post(CONFIG_EVENTS, 0, &ev, sizeof(ev), 0);

    const mock_posted_event_t *posted = mock_event_get(0);
    config_update_event_t got;
    memcpy(&got, posted->data, sizeof(got));
    TEST_ASSERT_EQUAL(0, got.kind);
    TEST_ASSERT_STR_EQUAL("ly0", got.key);
}

TEST_CASE(event_bus, find_last_event) {
    mock_events_reset();
    uint8_t led1 = 0x01;
    uint8_t led2 = 0x03;
    esp_event_post(KB_EVENTS, 1, &led1, sizeof(led1), 0);
    esp_event_post(KB_EVENTS, 1, &led2, sizeof(led2), 0);

    const mock_posted_event_t *last = mock_event_find_last(KB_EVENTS, 1);
    TEST_ASSERT_NOT_NULL(last);
    TEST_ASSERT_EQUAL(0x03, last->data[0]);
}

TEST_CASE(event_bus, count_matching_events) {
    mock_events_reset();
    uint8_t dummy = 0;
    esp_event_post(KB_EVENTS, 0, &dummy, 1, 0);
    esp_event_post(KB_EVENTS, 0, &dummy, 1, 0);
    esp_event_post(BLE_EVENTS, 0, &dummy, 1, 0);
    esp_event_post(KB_EVENTS, 1, &dummy, 1, 0);

    TEST_ASSERT_EQUAL(2, mock_event_count_matching(KB_EVENTS, 0));
    TEST_ASSERT_EQUAL(1, mock_event_count_matching(BLE_EVENTS, 0));
    TEST_ASSERT_EQUAL(1, mock_event_count_matching(KB_EVENTS, 1));
}

TEST_CASE(event_bus, handler_registration) {
    mock_events_reset();
    esp_event_handler_register(KB_EVENTS, 0, NULL, NULL);
    esp_event_handler_register(BLE_EVENTS, -1, NULL, NULL);

    TEST_ASSERT_EQUAL(2, _mock_handler_count);
}

TEST_CASE(event_bus, multiple_events_accumulate) {
    mock_events_reset();
    uint8_t payloads[] = {10, 20, 30};
    for (int i = 0; i < 3; i++) {
        esp_event_post(KB_EVENTS, i, &payloads[i], sizeof(payloads[i]), 0);
    }

    TEST_ASSERT_EQUAL(3, mock_event_count());
    for (int i = 0; i < 3; i++) {
        const mock_posted_event_t *ev = mock_event_get(i);
        TEST_ASSERT_EQUAL(i, ev->event_id);
        TEST_ASSERT_EQUAL(payloads[i], ev->data[0]);
    }
}

TEST_CASE(event_bus, payload_struct_sizes) {
    /* uint16 + int = 8 bytes (with padding) */
    TEST_ASSERT_EQUAL(8, sizeof(kb_sys_action_event_t));
    /* int + uint8 + 6 bytes, with possible padding */
    TEST_ASSERT(sizeof(ble_pairing_result_t) >= 11);
    /* uint8 + 16 char */
    TEST_ASSERT(sizeof(config_update_event_t) >= 17);
}

TEST_CASE(event_bus, config_update_key_fits) {
    config_update_event_t ev;
    strlcpy(ev.key, "123456789012", sizeof(ev.key)); /* 12 chars + null */
    TEST_ASSERT_STR_EQUAL("123456789012", ev.key);
}
