/**
 * @file test_kb_state.c
 * @brief Tests for kb_state.c — LED state tracker.
 *
 * Tests the REAL production kb_state_init/update_leds/get_leds functions
 * linked from components/keyboard/kb_state.c.
 */
#include "test_harness.h"

/* Production functions are linked via main.c. Event bases (KB_EVENTS etc.)
 * are defined in the linked event_bus.c. */

/* ---- Setup: reset mock events + production state before each test ---- */

TEST_SETUP(kb_state) {
    mock_events_reset();
    kb_state_init();
}

/* ---- Tests ---- */

TEST_CASE(kb_state, init_sets_zero) {
    TEST_ASSERT_EQUAL(0, kb_state_get_leds());
}

TEST_CASE(kb_state, update_caps_lock) {
    kb_state_update_leds(KB_LED_BIT_CAPS_LOCK);
    TEST_ASSERT_EQUAL(KB_LED_BIT_CAPS_LOCK, kb_state_get_leds());
}

TEST_CASE(kb_state, update_posts_event_on_change) {
    kb_state_update_leds(KB_LED_BIT_CAPS_LOCK);

    TEST_ASSERT_EQUAL(1, mock_event_count());
    const mock_posted_event_t *ev = mock_event_get(0);
    TEST_ASSERT_NOT_NULL(ev);
    TEST_ASSERT(ev->base == KB_EVENTS);
    TEST_ASSERT_EQUAL(KB_EVENT_LED_STATE, ev->event_id);
    TEST_ASSERT_EQUAL(KB_LED_BIT_CAPS_LOCK, ev->data[0]);
}

TEST_CASE(kb_state, no_event_when_unchanged) {
    kb_state_update_leds(KB_LED_BIT_NUM_LOCK);
    int count_after_first = mock_event_count();

    kb_state_update_leds(KB_LED_BIT_NUM_LOCK); /* same value */
    TEST_ASSERT_EQUAL(count_after_first, mock_event_count());
}

TEST_CASE(kb_state, multiple_leds) {
    uint8_t combined = KB_LED_BIT_CAPS_LOCK | KB_LED_BIT_NUM_LOCK;
    kb_state_update_leds(combined);
    TEST_ASSERT_EQUAL(combined, kb_state_get_leds());
}

TEST_CASE(kb_state, clear_leds) {
    kb_state_update_leds(KB_LED_BIT_CAPS_LOCK);
    kb_state_update_leds(0);
    TEST_ASSERT_EQUAL(0, kb_state_get_leds());
    TEST_ASSERT_EQUAL(2, mock_event_count()); /* Two changes */
}

TEST_CASE(kb_state, transition_between_different_states) {
    kb_state_update_leds(KB_LED_BIT_NUM_LOCK);
    kb_state_update_leds(KB_LED_BIT_CAPS_LOCK);
    kb_state_update_leds(KB_LED_BIT_SCROLL_LOCK);

    TEST_ASSERT_EQUAL(3, mock_event_count());
    TEST_ASSERT_EQUAL(KB_LED_BIT_SCROLL_LOCK, kb_state_get_leds());
}

TEST_CASE(kb_state, all_leds_on) {
    uint8_t all = KB_LED_BIT_NUM_LOCK | KB_LED_BIT_CAPS_LOCK | KB_LED_BIT_SCROLL_LOCK;
    kb_state_update_leds(all);
    TEST_ASSERT_EQUAL(all, kb_state_get_leds());
}
