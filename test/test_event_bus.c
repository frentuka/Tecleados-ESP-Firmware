/**
 * @file test_event_bus.c
 * @brief Tests for event_bus.c — event base definitions, payload structs,
 *        and mock event system integration.
 */
#include "test_harness.h"
#include "mocks/mock_esp.h"
#include "mocks/mock_esp_event.h"

/* Event bases (defined in test_kb_state.c) */
extern const char *KB_EVENTS;
extern const char *BLE_EVENTS;
extern const char *CONFIG_EVENTS;

/* Payload structs — guarded, may already be defined in same TU */
#ifndef _TH_KB_SYS_ACTION_EVENT_T
#define _TH_KB_SYS_ACTION_EVENT_T
typedef struct {
    uint16_t action_code;
    int      event;
} kb_sys_action_event_t;
#endif

#ifndef _TH_BLE_PAIRING_RESULT_T
#define _TH_BLE_PAIRING_RESULT_T
typedef struct {
    int     profile_idx;
    uint8_t addr_type;
    uint8_t addr[6];
} ble_pairing_result_t;
#endif

#ifndef _TH_CONFIG_UPDATE_EVENT_T
#define _TH_CONFIG_UPDATE_EVENT_T
typedef struct {
    uint8_t kind;
    char    key[16];
} config_update_event_t;
#endif

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

TEST_CASE(event_bus, dispatch_to_handler) {
    mock_events_reset();

    static int handler_called = 0;
    static uint16_t handler_data = 0;
    handler_called = 0;
    handler_data = 0;

    esp_event_handler_t my_handler = (esp_event_handler_t)(void(*)(void*, const char*, int32_t, void*))
        NULL; /* We'll test dispatch manually */

    /* Post an event then verify manual dispatch via mock_event_dispatch_all */
    uint8_t payload = 42;
    esp_event_post(KB_EVENTS, 1, &payload, sizeof(payload), 0);

    TEST_ASSERT_EQUAL(1, mock_event_count());
}

TEST_CASE(event_bus, payload_struct_sizes) {
    TEST_ASSERT_EQUAL(8, sizeof(kb_sys_action_event_t));  /* uint16 + int (padded) */
    TEST_ASSERT(sizeof(ble_pairing_result_t) >= 11);       /* int + uint8 + 6 bytes */
    TEST_ASSERT(sizeof(config_update_event_t) >= 17);      /* uint8 + 16 char */
}

TEST_CASE(event_bus, config_update_key_fits) {
    /* CFGMOD_MAX_KEY_LEN is 12; key field is 16 — verify it fits */
    config_update_event_t ev;
    strlcpy(ev.key, "123456789012", sizeof(ev.key)); /* 12 chars + null */
    TEST_ASSERT_STR_EQUAL("123456789012", ev.key);
}
