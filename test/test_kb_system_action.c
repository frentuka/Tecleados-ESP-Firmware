/**
 * @file test_kb_system_action.c
 * @brief Tests for kb_system_action.c — tap/hold gesture recognition engine.
 *
 * Tests the state machine transitions: press→hold, press→release→single_tap,
 * press→release→press→double_tap, timing thresholds, concurrent tracker limits,
 * and custom per-action timing.
 */
#include "test_harness.h"
#include "mocks/mock_esp.h"
#include "mocks/mock_esp_event.h"

/* Re-declare event base (defined in test_kb_state.c, but we need them here too) */
#ifndef _EVENTS_DEFINED_SYS_ACTION
#define _EVENTS_DEFINED_SYS_ACTION
extern const char *KB_EVENTS;
extern const char *BLE_EVENTS;
extern const char *CONFIG_EVENTS;
#endif

#define KB_EVENT_SYSTEM_ACTION 0

/* Event types */
typedef enum {
    KB_EV_PRESS = 0,
    KB_EV_RELEASE,
    KB_EV_SINGLE_TAP,
    KB_EV_DOUBLE_TAP,
    KB_EV_HOLD
} kb_action_ev_t;

typedef struct {
    uint32_t double_tap_threshold_ms;
    uint32_t hold_threshold_ms;
} kb_sys_action_timing_t;

#ifndef _TH_KB_SYS_ACTION_EVENT_T
#define _TH_KB_SYS_ACTION_EVENT_T
typedef struct {
    uint16_t action_code;
    int      event;
} kb_sys_action_event_t;
#endif

/* ---- Replicate the state machine for host testing ---- */

#define DOUBLE_TAP_TIMEOUT_US_DEFAULT  300000LL
#define HOLD_TIMEOUT_US_DEFAULT        500000LL
#define MAX_CONCURRENT_ACTIONS         10

typedef enum {
    STATE_IDLE,
    STATE_PRESSED_WAIT_HOLD,
    STATE_RELEASED_WAIT_DOUBLE,
    STATE_HELD,
} action_state_t;

typedef struct {
    uint16_t       action_code;
    action_state_t state;
    int64_t        timestamp_us;
    int64_t        double_tap_timeout_us;
    int64_t        hold_timeout_us;
} action_tracker_t;

static action_tracker_t s_trackers[MAX_CONCURRENT_ACTIONS];

static void notify_event(uint16_t action_code, kb_action_ev_t event) {
    kb_sys_action_event_t ev = { .action_code = action_code, .event = (int)event };
    esp_event_post(KB_EVENTS, KB_EVENT_SYSTEM_ACTION, &ev, sizeof(ev), 0);
}

static action_tracker_t *find_tracker(uint16_t action_code) {
    for (int i = 0; i < MAX_CONCURRENT_ACTIONS; i++) {
        if (s_trackers[i].state != STATE_IDLE &&
            s_trackers[i].action_code == action_code)
            return &s_trackers[i];
    }
    return NULL;
}

static action_tracker_t *alloc_tracker(uint16_t action_code,
                                       const kb_sys_action_timing_t *timing) {
    for (int i = 0; i < MAX_CONCURRENT_ACTIONS; i++) {
        if (s_trackers[i].state == STATE_IDLE) {
            s_trackers[i].action_code = action_code;
            s_trackers[i].double_tap_timeout_us =
                (timing && timing->double_tap_threshold_ms > 0)
                ? (int64_t)timing->double_tap_threshold_ms * 1000LL
                : DOUBLE_TAP_TIMEOUT_US_DEFAULT;
            s_trackers[i].hold_timeout_us =
                (timing && timing->hold_threshold_ms > 0)
                ? (int64_t)timing->hold_threshold_ms * 1000LL
                : HOLD_TIMEOUT_US_DEFAULT;
            return &s_trackers[i];
        }
    }
    return NULL;
}

static void free_tracker(action_tracker_t *t) {
    if (t) { t->state = STATE_IDLE; t->action_code = 0; }
}

/* Background task simulation — call manually from tests */
static void poll_trackers(void) {
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < MAX_CONCURRENT_ACTIONS; i++) {
        action_tracker_t *t = &s_trackers[i];
        if (t->state == STATE_PRESSED_WAIT_HOLD) {
            if (now - t->timestamp_us >= t->hold_timeout_us) {
                notify_event(t->action_code, KB_EV_HOLD);
                t->state = STATE_HELD;
            }
        } else if (t->state == STATE_RELEASED_WAIT_DOUBLE) {
            if (now - t->timestamp_us >= t->double_tap_timeout_us) {
                notify_event(t->action_code, KB_EV_SINGLE_TAP);
                free_tracker(t);
            }
        }
    }
}

static void process_action(uint16_t action_code, bool is_pressed,
                            const kb_sys_action_timing_t *timing) {
    notify_event(action_code, is_pressed ? KB_EV_PRESS : KB_EV_RELEASE);

    action_tracker_t *t = find_tracker(action_code);
    int64_t now = esp_timer_get_time();

    if (is_pressed) {
        if (t == NULL) {
            t = alloc_tracker(action_code, timing);
            if (t) {
                t->state = STATE_PRESSED_WAIT_HOLD;
                t->timestamp_us = now;
            }
        } else if (t->state == STATE_RELEASED_WAIT_DOUBLE) {
            notify_event(action_code, KB_EV_DOUBLE_TAP);
            free_tracker(t);
        }
    } else {
        if (t != NULL) {
            if (t->state == STATE_PRESSED_WAIT_HOLD) {
                t->state = STATE_RELEASED_WAIT_DOUBLE;
                t->timestamp_us = now;
            } else if (t->state == STATE_HELD) {
                free_tracker(t);
            }
        }
    }
}

static void init_sys_action(void) {
    memset(s_trackers, 0, sizeof(s_trackers));
    mock_events_reset();
    mock_timer_set(0);
}

/* ---- Helpers ---- */

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

/* ---- Tests ---- */

#define TEST_ACTION 0x2005

TEST_CASE(kb_system_action, press_fires_press_event) {
    init_sys_action();
    process_action(TEST_ACTION, true, NULL);
    TEST_ASSERT_EQUAL(1, count_events(KB_EV_PRESS));
}

TEST_CASE(kb_system_action, release_fires_release_event) {
    init_sys_action();
    process_action(TEST_ACTION, true, NULL);
    process_action(TEST_ACTION, false, NULL);
    TEST_ASSERT_EQUAL(1, count_events(KB_EV_RELEASE));
}

TEST_CASE(kb_system_action, single_tap_after_timeout) {
    init_sys_action();
    mock_timer_set(0);
    process_action(TEST_ACTION, true, NULL);

    mock_timer_advance(100000); /* 100 ms — shorter than hold */
    process_action(TEST_ACTION, false, NULL);

    /* Wait for double-tap timeout */
    mock_timer_advance(400000); /* total 500ms from release */
    poll_trackers();

    TEST_ASSERT_EQUAL(1, count_events(KB_EV_SINGLE_TAP));
    TEST_ASSERT_EQUAL(0, count_events(KB_EV_DOUBLE_TAP));
    TEST_ASSERT_EQUAL(0, count_events(KB_EV_HOLD));
}

TEST_CASE(kb_system_action, double_tap_within_window) {
    init_sys_action();
    mock_timer_set(0);
    process_action(TEST_ACTION, true, NULL);

    mock_timer_advance(50000); /* 50 ms */
    process_action(TEST_ACTION, false, NULL);

    mock_timer_advance(100000); /* 150 ms total — within 300ms double-tap window */
    process_action(TEST_ACTION, true, NULL);

    TEST_ASSERT_EQUAL(1, count_events(KB_EV_DOUBLE_TAP));
    TEST_ASSERT_EQUAL(0, count_events(KB_EV_SINGLE_TAP));
}

TEST_CASE(kb_system_action, hold_after_timeout) {
    init_sys_action();
    mock_timer_set(0);
    process_action(TEST_ACTION, true, NULL);

    mock_timer_advance(600000); /* 600 ms — past 500ms hold threshold */
    poll_trackers();

    TEST_ASSERT_EQUAL(1, count_events(KB_EV_HOLD));
    TEST_ASSERT_EQUAL(0, count_events(KB_EV_SINGLE_TAP));
}

TEST_CASE(kb_system_action, hold_then_release_cleans_up) {
    init_sys_action();
    mock_timer_set(0);
    process_action(TEST_ACTION, true, NULL);
    mock_timer_advance(600000);
    poll_trackers();

    process_action(TEST_ACTION, false, NULL);

    /* Tracker should be freed */
    TEST_ASSERT_NULL(find_tracker(TEST_ACTION));
}

TEST_CASE(kb_system_action, custom_timing_shorter_hold) {
    init_sys_action();
    kb_sys_action_timing_t timing = { .double_tap_threshold_ms = 200, .hold_threshold_ms = 100 };
    mock_timer_set(0);
    process_action(TEST_ACTION, true, &timing);

    mock_timer_advance(150000); /* 150 ms — past custom 100ms hold */
    poll_trackers();

    TEST_ASSERT_EQUAL(1, count_events(KB_EV_HOLD));
}

TEST_CASE(kb_system_action, custom_timing_shorter_double_tap) {
    init_sys_action();
    kb_sys_action_timing_t timing = { .double_tap_threshold_ms = 100, .hold_threshold_ms = 500 };
    mock_timer_set(0);
    process_action(TEST_ACTION, true, &timing);
    mock_timer_advance(30000);
    process_action(TEST_ACTION, false, &timing);

    /* Wait past custom 100ms double-tap window */
    mock_timer_advance(150000);
    poll_trackers();

    TEST_ASSERT_EQUAL(1, count_events(KB_EV_SINGLE_TAP));
}

TEST_CASE(kb_system_action, concurrent_trackers) {
    init_sys_action();
    mock_timer_set(0);

    for (uint16_t i = 0; i < MAX_CONCURRENT_ACTIONS; i++) {
        process_action(0x2010 + i, true, NULL);
    }

    /* All should have trackers */
    for (uint16_t i = 0; i < MAX_CONCURRENT_ACTIONS; i++) {
        TEST_ASSERT_NOT_NULL(find_tracker(0x2010 + i));
    }
}

TEST_CASE(kb_system_action, tracker_pool_overflow) {
    init_sys_action();
    mock_timer_set(0);

    /* Fill all tracker slots */
    for (uint16_t i = 0; i < MAX_CONCURRENT_ACTIONS; i++) {
        process_action(0x2010 + i, true, NULL);
    }

    /* 11th action should still get press/release events but no tracker */
    int before = mock_event_count();
    process_action(0x2099, true, NULL);
    TEST_ASSERT(mock_event_count() > before); /* Press event still posted */
    TEST_ASSERT_NULL(find_tracker(0x2099));
}

TEST_CASE(kb_system_action, release_without_prior_press) {
    init_sys_action();
    /* Should not crash or post unexpected events beyond RELEASE */
    process_action(TEST_ACTION, false, NULL);
    TEST_ASSERT_EQUAL(1, count_events(KB_EV_RELEASE));
    TEST_ASSERT_EQUAL(0, count_events(KB_EV_SINGLE_TAP));
}

TEST_CASE(kb_system_action, duplicate_press_ignored_during_hold_wait) {
    init_sys_action();
    mock_timer_set(0);
    process_action(TEST_ACTION, true, NULL);
    mock_timer_advance(100000);
    process_action(TEST_ACTION, true, NULL); /* duplicate press */

    /* Should only have 2 PRESS events, no double-tap */
    TEST_ASSERT_EQUAL(2, count_events(KB_EV_PRESS));
    TEST_ASSERT_EQUAL(0, count_events(KB_EV_DOUBLE_TAP));
}
