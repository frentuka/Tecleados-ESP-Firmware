/**
 * @file test_kb_system_action.c
 * @brief Tests for kb_system_action.c — tap/hold gesture recognition engine.
 *
 * Tests the REAL production kb_system_action_init, kb_system_action_process,
 * kb_system_action_process_ex, and kb_system_action_poll linked from
 * components/keyboard/kb_system_action.c.
 *
 * Internal state (trackers, find_tracker, etc.) is not accessible — all
 * assertions are behavioral, verifying events posted through the event bus.
 */
#include "test_harness.h"

/* Production functions (kb_system_action_init, _process, _process_ex, _poll)
   and types (kb_action_ev_t, kb_sys_action_timing_t, kb_sys_action_event_t)
   are available via test_constants.h → kb_system_action.h.
   Event bases (KB_EVENTS etc.) come from the linked event_bus.c. */

/* ---- Helpers ---- */

#define TEST_ACTION 0x2005

static int count_events(kb_action_ev_t type) {
    int n = 0;
    for (int i = 0; i < mock_event_count(); i++) {
        const mock_posted_event_t *e = mock_event_get(i);
        if (e->base == KB_EVENTS && e->event_id == KB_EVENT_SYSTEM_ACTION) {
            kb_sys_action_event_t ev;
            memcpy(&ev, e->data, sizeof(ev));
            if (ev.event == (int)type) n++;
        }
    }
    return n;
}

/* ---- Setup ---- */

TEST_SETUP(kb_system_action) {
    mock_events_reset();
    mock_timer_set(0);
    kb_system_action_init();
}

/* ---- Tests ---- */

TEST_CASE(kb_system_action, press_fires_press_event) {
    kb_system_action_process(TEST_ACTION, true);
    TEST_ASSERT_EQUAL(1, count_events(KB_EV_PRESS));
}

TEST_CASE(kb_system_action, release_fires_release_event) {
    kb_system_action_process(TEST_ACTION, true);
    kb_system_action_process(TEST_ACTION, false);
    TEST_ASSERT_EQUAL(1, count_events(KB_EV_RELEASE));
}

TEST_CASE(kb_system_action, single_tap_after_timeout) {
    kb_system_action_process(TEST_ACTION, true);

    mock_timer_advance(100000); /* 100 ms — shorter than hold */
    kb_system_action_process(TEST_ACTION, false);

    /* Wait past double-tap timeout (300ms default from release) */
    mock_timer_advance(400000);
    kb_system_action_poll();

    TEST_ASSERT_EQUAL(1, count_events(KB_EV_SINGLE_TAP));
    TEST_ASSERT_EQUAL(0, count_events(KB_EV_DOUBLE_TAP));
    TEST_ASSERT_EQUAL(0, count_events(KB_EV_HOLD));
}

TEST_CASE(kb_system_action, double_tap_within_window) {
    kb_system_action_process(TEST_ACTION, true);

    mock_timer_advance(50000); /* 50 ms */
    kb_system_action_process(TEST_ACTION, false);

    mock_timer_advance(100000); /* 150 ms total — within 300ms double-tap window */
    kb_system_action_process(TEST_ACTION, true);

    TEST_ASSERT_EQUAL(1, count_events(KB_EV_DOUBLE_TAP));
    TEST_ASSERT_EQUAL(0, count_events(KB_EV_SINGLE_TAP));
}

TEST_CASE(kb_system_action, hold_after_timeout) {
    kb_system_action_process(TEST_ACTION, true);

    mock_timer_advance(600000); /* 600 ms — past 500ms hold threshold */
    kb_system_action_poll();

    TEST_ASSERT_EQUAL(1, count_events(KB_EV_HOLD));
    TEST_ASSERT_EQUAL(0, count_events(KB_EV_SINGLE_TAP));
}

TEST_CASE(kb_system_action, hold_then_release_cleans_up) {
    kb_system_action_process(TEST_ACTION, true);
    mock_timer_advance(600000);
    kb_system_action_poll();

    mock_events_reset();
    kb_system_action_process(TEST_ACTION, false);

    /* After release from held state, a fresh press should work normally */
    mock_events_reset();
    mock_timer_advance(1000);
    kb_system_action_process(TEST_ACTION, true);
    mock_timer_advance(100000);
    kb_system_action_process(TEST_ACTION, false);
    mock_timer_advance(400000);
    kb_system_action_poll();

    /* If cleanup worked, we get a single tap from the new gesture */
    TEST_ASSERT_EQUAL(1, count_events(KB_EV_SINGLE_TAP));
}

TEST_CASE(kb_system_action, custom_timing_shorter_hold) {
    kb_sys_action_timing_t timing = { .double_tap_threshold_ms = 200, .hold_threshold_ms = 100 };
    kb_system_action_process_ex(TEST_ACTION, true, &timing);

    mock_timer_advance(150000); /* 150 ms — past custom 100ms hold */
    kb_system_action_poll();

    TEST_ASSERT_EQUAL(1, count_events(KB_EV_HOLD));
}

TEST_CASE(kb_system_action, custom_timing_shorter_double_tap) {
    kb_sys_action_timing_t timing = { .double_tap_threshold_ms = 100, .hold_threshold_ms = 500 };
    kb_system_action_process_ex(TEST_ACTION, true, &timing);
    mock_timer_advance(30000);
    kb_system_action_process_ex(TEST_ACTION, false, &timing);

    /* Wait past custom 100ms double-tap window */
    mock_timer_advance(150000);
    kb_system_action_poll();

    TEST_ASSERT_EQUAL(1, count_events(KB_EV_SINGLE_TAP));
}

TEST_CASE(kb_system_action, concurrent_trackers) {
    for (uint16_t i = 0; i < MAX_CONCURRENT_ACTIONS; i++) {
        kb_system_action_process(0x2010 + i, true);
    }

    /* All should be tracking — advance past hold timeout and verify */
    mock_timer_advance(600000);
    kb_system_action_poll();

    TEST_ASSERT_EQUAL(MAX_CONCURRENT_ACTIONS, count_events(KB_EV_HOLD));
}

TEST_CASE(kb_system_action, tracker_pool_overflow) {
    /* Fill all tracker slots */
    for (uint16_t i = 0; i < MAX_CONCURRENT_ACTIONS; i++) {
        kb_system_action_process(0x2010 + i, true);
    }

    mock_events_reset();

    /* 11th action: press event still fires, but no tracker allocated */
    kb_system_action_process(0x2099, true);
    TEST_ASSERT_EQUAL(1, count_events(KB_EV_PRESS));

    /* Advance past hold timeout — only the first 10 get HOLD events */
    mock_timer_advance(600000);
    kb_system_action_poll();
    TEST_ASSERT_EQUAL(MAX_CONCURRENT_ACTIONS, count_events(KB_EV_HOLD));
}

TEST_CASE(kb_system_action, release_without_prior_press) {
    /* Should not crash or post unexpected events beyond RELEASE */
    kb_system_action_process(TEST_ACTION, false);
    TEST_ASSERT_EQUAL(1, count_events(KB_EV_RELEASE));
    TEST_ASSERT_EQUAL(0, count_events(KB_EV_SINGLE_TAP));
}

TEST_CASE(kb_system_action, duplicate_press_ignored_during_hold_wait) {
    kb_system_action_process(TEST_ACTION, true);
    mock_timer_advance(100000);
    kb_system_action_process(TEST_ACTION, true); /* duplicate press */

    /* Should only have 2 PRESS events, no double-tap */
    TEST_ASSERT_EQUAL(2, count_events(KB_EV_PRESS));
    TEST_ASSERT_EQUAL(0, count_events(KB_EV_DOUBLE_TAP));
}
