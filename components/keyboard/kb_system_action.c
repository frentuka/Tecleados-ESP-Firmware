#include "kb_system_action.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "event_bus.h"

static const char *TAG = "kb_sys_action";

#define DOUBLE_TAP_TIMEOUT_US_DEFAULT  300000LL  /* 300 ms */
#define HOLD_TIMEOUT_US_DEFAULT        500000LL  /* 500 ms */
#define MAX_CONCURRENT_ACTIONS         10

typedef enum {
    STATE_IDLE,
    STATE_PRESSED_WAIT_HOLD,   /* Pressed — waiting to see if it becomes a hold */
    STATE_RELEASED_WAIT_DOUBLE, /* Released — waiting to see if a second press follows */
    STATE_HELD,                /* Hold event already fired */
} action_state_t;

typedef struct {
    uint16_t       action_code;
    action_state_t state;
    /* Timestamp updated on press AND on transition to RELEASED_WAIT_DOUBLE,
     * so the double-tap window is always measured from the moment of release. */
    int64_t        timestamp_us;
    int64_t        double_tap_timeout_us;
    int64_t        hold_timeout_us;
} action_tracker_t;

static action_tracker_t s_trackers[MAX_CONCURRENT_ACTIONS];
static TaskHandle_t     s_task_handle = NULL;

/* ---- Event notification ---- */

static void notify_event(uint16_t action_code, kb_action_ev_t event) {
    kb_sys_action_event_t ev = { .action_code = action_code, .event = (int)event };
    esp_event_post(KB_EVENTS, KB_EVENT_SYSTEM_ACTION, &ev, sizeof(ev), 0);
}

/* ---- Tracker lifetime ---- */

static action_tracker_t *find_tracker(uint16_t action_code) {
    for (int i = 0; i < MAX_CONCURRENT_ACTIONS; i++) {
        if (s_trackers[i].state != STATE_IDLE &&
            s_trackers[i].action_code == action_code) {
            return &s_trackers[i];
        }
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
    ESP_LOGW(TAG, "Tracker pool full — dropping action 0x%04X", action_code);
    return NULL;
}

static void free_tracker(action_tracker_t *t) {
    if (t) {
        t->state       = STATE_IDLE;
        t->action_code = 0;
    }
}

/* ---- Background timing task ---- */

static void sys_action_task(void *arg) {
    while (1) {
        int64_t now = esp_timer_get_time();

        for (int i = 0; i < MAX_CONCURRENT_ACTIONS; i++) {
            action_tracker_t *t = &s_trackers[i];

            if (t->state == STATE_PRESSED_WAIT_HOLD) {
                if (now - t->timestamp_us >= t->hold_timeout_us) {
                    notify_event(t->action_code, KB_EV_HOLD);
                    t->state = STATE_HELD;
                }
            } else if (t->state == STATE_RELEASED_WAIT_DOUBLE) {
                /* timestamp_us was reset to the release moment, so this window
                 * is always double_tap_timeout_us from the release. */
                if (now - t->timestamp_us >= t->double_tap_timeout_us) {
                    notify_event(t->action_code, KB_EV_SINGLE_TAP);
                    free_tracker(t);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ---- Core state machine ---- */

static void process_action(uint16_t action_code, bool is_pressed,
                            const kb_sys_action_timing_t *timing) {
    /* Immediate press / release events */
    notify_event(action_code, is_pressed ? KB_EV_PRESS : KB_EV_RELEASE);

    action_tracker_t *t   = find_tracker(action_code);
    int64_t           now = esp_timer_get_time();

    if (is_pressed) {
        if (t == NULL) {
            /* First press: allocate tracker and start waiting for hold */
            t = alloc_tracker(action_code, timing);
            if (t) {
                t->state        = STATE_PRESSED_WAIT_HOLD;
                t->timestamp_us = now;
            }
        } else if (t->state == STATE_RELEASED_WAIT_DOUBLE) {
            /* Second press within the double-tap window */
            notify_event(action_code, KB_EV_DOUBLE_TAP);
            free_tracker(t);
        }
        /* Duplicate press while PRESSED_WAIT_HOLD or HELD: ignore */
    } else {
        if (t != NULL) {
            if (t->state == STATE_PRESSED_WAIT_HOLD) {
                /* Released before hold timeout: wait for a possible second press.
                 * Reset timestamp to NOW so the double-tap window is measured
                 * from the release, not from the original press. */
                t->state        = STATE_RELEASED_WAIT_DOUBLE;
                t->timestamp_us = now;
            } else if (t->state == STATE_HELD) {
                /* Was already held: release simply cleans up */
                free_tracker(t);
            }
        }
    }
}

/* ---- Public API ---- */

void kb_system_action_init(void) {
    memset(s_trackers, 0, sizeof(s_trackers));
    if (s_task_handle == NULL) {
        xTaskCreateWithCaps(sys_action_task, "kb_sys_action", 4096, NULL, 5,
                            &s_task_handle,
                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
}


void kb_system_action_process(uint16_t action_code, bool is_pressed) {
    process_action(action_code, is_pressed, NULL);
}

void kb_system_action_process_ex(uint16_t action_code, bool is_pressed,
                                 const kb_sys_action_timing_t *timing) {
    process_action(action_code, is_pressed, timing);
}
