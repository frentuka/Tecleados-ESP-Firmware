#include "kb_system_action.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "kb_sys_action";

#define DOUBLE_TAP_TIMEOUT_US_DEFAULT  300000 // 300 ms
#define HOLD_TIMEOUT_US_DEFAULT        500000 // 500 ms
#define MAX_CONCURRENT_ACTIONS 10

typedef enum {
    STATE_IDLE,
    STATE_PRESSED_WAIT_HOLD,
    STATE_RELEASED_WAIT_DOUBLE,
    STATE_HELD
} action_state_t;

typedef struct {
    uint16_t      action_code;
    action_state_t state;
    int64_t       press_time_us;
    /* Per-action overrides — 0 means use the global default */
    int64_t       double_tap_timeout_us; // derived from timing at alloc time
    int64_t       hold_timeout_us;       // derived from timing at alloc time
} action_tracker_t;

static action_tracker_t s_trackers[MAX_CONCURRENT_ACTIONS];
static kb_sys_action_cb_t s_action_cb = NULL;
static TaskHandle_t s_task_handle = NULL;

static void notify_event(uint16_t action_code, kb_action_ev_t event) {
    const char *ev_str = "UNKNOWN";
    switch (event) {
        case KB_EV_PRESS:      ev_str = "PRESS"; break;
        case KB_EV_RELEASE:    ev_str = "RELEASE"; break;
        case KB_EV_SINGLE_TAP: ev_str = "SINGLE_TAP"; break;
        case KB_EV_DOUBLE_TAP: ev_str = "DOUBLE_TAP"; break;
        case KB_EV_HOLD:       ev_str = "HOLD"; break;
    }
    ESP_LOGI(TAG, "Event: %s (Action: 0x%04X)", ev_str, action_code);

    if (s_action_cb) {
        s_action_cb(action_code, event);
    }
}

static action_tracker_t *find_tracker(uint16_t action_code) {
    for (int i = 0; i < MAX_CONCURRENT_ACTIONS; i++) {
        if (s_trackers[i].action_code == action_code &&
            s_trackers[i].state != STATE_IDLE) {
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
            /* Resolve thresholds: use override if non-zero, else engine default */
            if (timing && timing->double_tap_threshold_ms > 0) {
                s_trackers[i].double_tap_timeout_us =
                    (int64_t)timing->double_tap_threshold_ms * 1000LL;
            } else {
                s_trackers[i].double_tap_timeout_us = DOUBLE_TAP_TIMEOUT_US_DEFAULT;
            }
            if (timing && timing->hold_threshold_ms > 0) {
                s_trackers[i].hold_timeout_us =
                    (int64_t)timing->hold_threshold_ms * 1000LL;
            } else {
                s_trackers[i].hold_timeout_us = HOLD_TIMEOUT_US_DEFAULT;
            }
            return &s_trackers[i];
        }
    }
    return NULL; // Overflow — silent drop
}

static void free_tracker(action_tracker_t *t) {
    if (t) {
        t->state = STATE_IDLE;
        t->action_code = 0;
    }
}

static void sys_action_task(void *arg) {
    while (1) {
        int64_t now = esp_timer_get_time();

        for (int i = 0; i < MAX_CONCURRENT_ACTIONS; i++) {
            action_tracker_t *t = &s_trackers[i];

            if (t->state == STATE_PRESSED_WAIT_HOLD) {
                if (now - t->press_time_us >= t->hold_timeout_us) {
                    notify_event(t->action_code, KB_EV_HOLD);
                    t->state = STATE_HELD;
                }
            } else if (t->state == STATE_RELEASED_WAIT_DOUBLE) {
                if (now - t->press_time_us >= t->double_tap_timeout_us) {
                    notify_event(t->action_code, KB_EV_SINGLE_TAP);
                    free_tracker(t);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10)); // Check every 10 ms
    }
}

void kb_system_action_init(void) {
    memset(s_trackers, 0, sizeof(s_trackers));
    if (s_task_handle == NULL) {
        xTaskCreateWithCaps(sys_action_task, "kb_sys_action", 4096, NULL, 5,
                            &s_task_handle,
                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
}

void kb_system_action_register_cb(kb_sys_action_cb_t cb) {
    s_action_cb = cb;
}

/* ---- Core state-machine (shared by both public entry points) ---- */

static void process_action(uint16_t action_code, bool is_pressed,
                            const kb_sys_action_timing_t *timing) {
    /* Always fire raw PRESS / RELEASE immediately */
    notify_event(action_code, is_pressed ? KB_EV_PRESS : KB_EV_RELEASE);

    action_tracker_t *t = find_tracker(action_code);
    int64_t now = esp_timer_get_time();

    if (is_pressed) {
        if (t == NULL) {
            /* First press */
            t = alloc_tracker(action_code, timing);
            if (t) {
                t->state = STATE_PRESSED_WAIT_HOLD;
                t->press_time_us = now;
            }
        } else {
            if (t->state == STATE_RELEASED_WAIT_DOUBLE) {
                /* Second press within double-tap window */
                notify_event(action_code, KB_EV_DOUBLE_TAP);
                free_tracker(t);
            }
            /* Duplicate press while STATE_PRESSED_WAIT_HOLD / STATE_HELD — ignore */
        }
    } else {
        /* Released */
        if (t != NULL) {
            if (t->state == STATE_PRESSED_WAIT_HOLD) {
                /* Released before hold timeout — wait for possible double-tap */
                t->state = STATE_RELEASED_WAIT_DOUBLE;
            } else if (t->state == STATE_HELD) {
                /* Was held, now released — done */
                free_tracker(t);
            }
        }
    }
}

void kb_system_action_process(uint16_t action_code, bool is_pressed) {
    process_action(action_code, is_pressed, NULL);
}

void kb_system_action_process_ex(uint16_t action_code, bool is_pressed,
                                 const kb_sys_action_timing_t *timing) {
    process_action(action_code, is_pressed, timing);
}
