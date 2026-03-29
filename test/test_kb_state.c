/**
 * @file test_kb_state.c
 * @brief Tests for kb_state.c — LED state tracker.
 *
 * Tests init, update (change detection), get, and event posting.
 */
#include "test_harness.h"
#include "mocks/mock_esp.h"
#include "mocks/mock_esp_event.h"

/* Event base definitions */
ESP_EVENT_DEFINE_BASE(KB_EVENTS);
ESP_EVENT_DEFINE_BASE(BLE_EVENTS);
ESP_EVENT_DEFINE_BASE(CONFIG_EVENTS);

/* Event IDs */
#define KB_EVENT_SYSTEM_ACTION 0
#define KB_EVENT_LED_STATE     1

/* LED bit definitions */
#define KB_LED_BIT_NUM_LOCK    (1 << 0)
#define KB_LED_BIT_CAPS_LOCK   (1 << 1)
#define KB_LED_BIT_SCROLL_LOCK (1 << 2)

/* Inline the kb_state logic for testing */
static uint8_t s_led_status = 0;

static void kb_state_init(void) {
    s_led_status = 0;
}

static void kb_state_update_leds(uint8_t led_status) {
    if (s_led_status != led_status) {
        s_led_status = led_status;
        esp_event_post(KB_EVENTS, KB_EVENT_LED_STATE, &led_status, sizeof(uint8_t), 0);
    }
}

static uint8_t kb_state_get_leds(void) { return s_led_status; }

/* ---- Tests ---- */

TEST_CASE(kb_state, init_sets_zero) {
    s_led_status = 0xFF;
    kb_state_init();
    TEST_ASSERT_EQUAL(0, kb_state_get_leds());
}

TEST_CASE(kb_state, update_caps_lock) {
    mock_events_reset();
    kb_state_init();
    kb_state_update_leds(KB_LED_BIT_CAPS_LOCK);
    TEST_ASSERT_EQUAL(KB_LED_BIT_CAPS_LOCK, kb_state_get_leds());
}

TEST_CASE(kb_state, update_posts_event_on_change) {
    mock_events_reset();
    kb_state_init();
    kb_state_update_leds(KB_LED_BIT_CAPS_LOCK);

    TEST_ASSERT_EQUAL(1, mock_event_count());
    const mock_posted_event_t *ev = mock_event_get(0);
    TEST_ASSERT_NOT_NULL(ev);
    TEST_ASSERT(ev->base == KB_EVENTS);
    TEST_ASSERT_EQUAL(KB_EVENT_LED_STATE, ev->event_id);
    TEST_ASSERT_EQUAL(KB_LED_BIT_CAPS_LOCK, ev->data[0]);
}

TEST_CASE(kb_state, no_event_when_unchanged) {
    mock_events_reset();
    kb_state_init();
    kb_state_update_leds(KB_LED_BIT_NUM_LOCK);
    int count_after_first = mock_event_count();

    kb_state_update_leds(KB_LED_BIT_NUM_LOCK); /* same value */
    TEST_ASSERT_EQUAL(count_after_first, mock_event_count());
}

TEST_CASE(kb_state, multiple_leds) {
    mock_events_reset();
    kb_state_init();
    uint8_t combined = KB_LED_BIT_CAPS_LOCK | KB_LED_BIT_NUM_LOCK;
    kb_state_update_leds(combined);
    TEST_ASSERT_EQUAL(combined, kb_state_get_leds());
}

TEST_CASE(kb_state, clear_leds) {
    mock_events_reset();
    kb_state_init();
    kb_state_update_leds(KB_LED_BIT_CAPS_LOCK);
    kb_state_update_leds(0);
    TEST_ASSERT_EQUAL(0, kb_state_get_leds());
    TEST_ASSERT_EQUAL(2, mock_event_count()); /* Two changes */
}

TEST_CASE(kb_state, transition_between_different_states) {
    mock_events_reset();
    kb_state_init();
    kb_state_update_leds(KB_LED_BIT_NUM_LOCK);
    kb_state_update_leds(KB_LED_BIT_CAPS_LOCK);
    kb_state_update_leds(KB_LED_BIT_SCROLL_LOCK);

    TEST_ASSERT_EQUAL(3, mock_event_count());
    TEST_ASSERT_EQUAL(KB_LED_BIT_SCROLL_LOCK, kb_state_get_leds());
}

TEST_CASE(kb_state, all_leds_on) {
    mock_events_reset();
    kb_state_init();
    uint8_t all = KB_LED_BIT_NUM_LOCK | KB_LED_BIT_CAPS_LOCK | KB_LED_BIT_SCROLL_LOCK;
    kb_state_update_leds(all);
    TEST_ASSERT_EQUAL(all, kb_state_get_leds());
}
