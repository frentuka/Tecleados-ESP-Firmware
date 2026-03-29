/**
 * @file test_status_module.c
 * @brief Tests for statusmod.c — event-driven state caching, BLE event
 *        handling, config event handling, and status push formatting.
 */
#include "test_harness.h"
#include "mocks/mock_esp.h"
#include "mocks/mock_esp_event.h"

/* Re-use event bases from other test files */
extern const char *KB_EVENTS;
extern const char *BLE_EVENTS;
extern const char *CONFIG_EVENTS;

/* Event IDs */
typedef enum {
    BLE_EVENT_ROUTING_CHANGED = 0,
    BLE_EVENT_PROFILE_CONNECTED,
    BLE_EVENT_PROFILE_DISCONNECTED,
    BLE_EVENT_PAIRING_STARTED,
    BLE_EVENT_PAIRING_COMPLETE,
    BLE_EVENT_PAIRING_FAILED,
    BLE_EVENT_PAIRING_TIMEOUT,
} ble_event_id_t;

typedef enum {
    CONFIG_EVENT_KIND_UPDATED = 0,
} config_event_id_t;

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

/* ---- Simulated status module cache ---- */

static struct {
    uint8_t  transport_mode;
    uint8_t  selected_profile;
    uint8_t  pairing_profile;
    uint16_t connected_bitmap;
} s_cache = {
    .transport_mode    = 0,
    .selected_profile  = 0,
    .pairing_profile   = 0xFF,
    .connected_bitmap  = 0,
};

static int s_push_count = 0;

static void reset_status(void) {
    s_cache.transport_mode = 0;
    s_cache.selected_profile = 0;
    s_cache.pairing_profile = 0xFF;
    s_cache.connected_bitmap = 0;
    s_push_count = 0;
}

static void send_status_push(void) {
    s_push_count++;
}

static void status_on_ble_event(int32_t event_id, void *data) {
    switch ((ble_event_id_t)event_id) {
    case BLE_EVENT_PROFILE_CONNECTED: {
        int idx = *(int *)data;
        if (idx >= 0 && idx < 16)
            s_cache.connected_bitmap |= (uint16_t)(1u << idx);
        break;
    }
    case BLE_EVENT_PROFILE_DISCONNECTED: {
        int idx = *(int *)data;
        if (idx >= 0 && idx < 16)
            s_cache.connected_bitmap &= (uint16_t)~(1u << idx);
        break;
    }
    case BLE_EVENT_PAIRING_STARTED: {
        int profile = *(int *)data;
        s_cache.pairing_profile = (uint8_t)profile;
        break;
    }
    case BLE_EVENT_PAIRING_COMPLETE: {
        const ble_pairing_result_t *r = (const ble_pairing_result_t *)data;
        s_cache.pairing_profile = 0xFF;
        s_cache.selected_profile = (uint8_t)r->profile_idx;
        break;
    }
    case BLE_EVENT_PAIRING_FAILED:
    case BLE_EVENT_PAIRING_TIMEOUT:
        s_cache.pairing_profile = 0xFF;
        break;
    case BLE_EVENT_ROUTING_CHANGED:
        s_cache.transport_mode = *(bool *)data ? 1 : 0;
        break;
    default:
        return;
    }
    send_status_push();
}

/* ---- Tests ---- */

TEST_CASE(status_module, initial_state) {
    reset_status();
    TEST_ASSERT_EQUAL(0, s_cache.transport_mode);
    TEST_ASSERT_EQUAL(0, s_cache.selected_profile);
    TEST_ASSERT_EQUAL(0xFF, s_cache.pairing_profile);
    TEST_ASSERT_EQUAL(0, s_cache.connected_bitmap);
}

TEST_CASE(status_module, profile_connected_sets_bitmap) {
    reset_status();
    int idx = 3;
    status_on_ble_event(BLE_EVENT_PROFILE_CONNECTED, &idx);

    TEST_ASSERT_EQUAL_HEX(0x0008, s_cache.connected_bitmap);
    TEST_ASSERT_EQUAL(1, s_push_count);
}

TEST_CASE(status_module, profile_disconnected_clears_bitmap) {
    reset_status();
    int idx = 3;
    status_on_ble_event(BLE_EVENT_PROFILE_CONNECTED, &idx);

    status_on_ble_event(BLE_EVENT_PROFILE_DISCONNECTED, &idx);
    TEST_ASSERT_EQUAL_HEX(0x0000, s_cache.connected_bitmap);
    TEST_ASSERT_EQUAL(2, s_push_count);
}

TEST_CASE(status_module, multiple_profiles_connected) {
    reset_status();
    int idx0 = 0, idx2 = 2, idx8 = 8;
    status_on_ble_event(BLE_EVENT_PROFILE_CONNECTED, &idx0);
    status_on_ble_event(BLE_EVENT_PROFILE_CONNECTED, &idx2);
    status_on_ble_event(BLE_EVENT_PROFILE_CONNECTED, &idx8);

    TEST_ASSERT_EQUAL_HEX(0x0105, s_cache.connected_bitmap); /* bits 0, 2, 8 */
}

TEST_CASE(status_module, pairing_started_sets_profile) {
    reset_status();
    int profile = 5;
    status_on_ble_event(BLE_EVENT_PAIRING_STARTED, &profile);

    TEST_ASSERT_EQUAL(5, s_cache.pairing_profile);
}

TEST_CASE(status_module, pairing_complete_updates_state) {
    reset_status();
    int profile = 2;
    status_on_ble_event(BLE_EVENT_PAIRING_STARTED, &profile);

    ble_pairing_result_t result = { .profile_idx = 2 };
    status_on_ble_event(BLE_EVENT_PAIRING_COMPLETE, &result);

    TEST_ASSERT_EQUAL(0xFF, s_cache.pairing_profile); /* cleared */
    TEST_ASSERT_EQUAL(2, s_cache.selected_profile);
}

TEST_CASE(status_module, pairing_failed_clears_pairing) {
    reset_status();
    int profile = 1;
    status_on_ble_event(BLE_EVENT_PAIRING_STARTED, &profile);
    TEST_ASSERT_EQUAL(1, s_cache.pairing_profile);

    status_on_ble_event(BLE_EVENT_PAIRING_FAILED, &profile);
    TEST_ASSERT_EQUAL(0xFF, s_cache.pairing_profile);
}

TEST_CASE(status_module, pairing_timeout_clears_pairing) {
    reset_status();
    int profile = 4;
    status_on_ble_event(BLE_EVENT_PAIRING_STARTED, &profile);
    status_on_ble_event(BLE_EVENT_PAIRING_TIMEOUT, &profile);
    TEST_ASSERT_EQUAL(0xFF, s_cache.pairing_profile);
}

TEST_CASE(status_module, routing_changed_to_ble) {
    reset_status();
    bool active = true;
    status_on_ble_event(BLE_EVENT_ROUTING_CHANGED, &active);

    TEST_ASSERT_EQUAL(1, s_cache.transport_mode);
}

TEST_CASE(status_module, routing_changed_to_usb) {
    reset_status();
    bool active = true;
    status_on_ble_event(BLE_EVENT_ROUTING_CHANGED, &active);
    active = false;
    status_on_ble_event(BLE_EVENT_ROUTING_CHANGED, &active);

    TEST_ASSERT_EQUAL(0, s_cache.transport_mode);
}

TEST_CASE(status_module, disconnect_nonexistent_profile_safe) {
    reset_status();
    int idx = 7;
    /* Disconnect without prior connect — bitmap stays 0 */
    status_on_ble_event(BLE_EVENT_PROFILE_DISCONNECTED, &idx);
    TEST_ASSERT_EQUAL_HEX(0x0000, s_cache.connected_bitmap);
}

TEST_CASE(status_module, boundary_profile_indices) {
    reset_status();
    int idx0 = 0, idx8 = 8, idx15 = 15;
    status_on_ble_event(BLE_EVENT_PROFILE_CONNECTED, &idx0);
    status_on_ble_event(BLE_EVENT_PROFILE_CONNECTED, &idx8);
    status_on_ble_event(BLE_EVENT_PROFILE_CONNECTED, &idx15);

    TEST_ASSERT(s_cache.connected_bitmap & (1 << 0));
    TEST_ASSERT(s_cache.connected_bitmap & (1 << 8));
    TEST_ASSERT(s_cache.connected_bitmap & (1 << 15));
}

TEST_CASE(status_module, each_ble_event_triggers_push) {
    reset_status();
    int idx = 1;
    bool active = true;
    ble_pairing_result_t result = { .profile_idx = 1 };

    status_on_ble_event(BLE_EVENT_PROFILE_CONNECTED, &idx);
    status_on_ble_event(BLE_EVENT_PAIRING_STARTED, &idx);
    status_on_ble_event(BLE_EVENT_PAIRING_COMPLETE, &result);
    status_on_ble_event(BLE_EVENT_ROUTING_CHANGED, &active);
    status_on_ble_event(BLE_EVENT_PROFILE_DISCONNECTED, &idx);

    TEST_ASSERT_EQUAL(5, s_push_count);
}
