#include "kb_system_action.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "kb_sys_action";

#define DOUBLE_TAP_TIMEOUT_US  300000 // 300ms
#define HOLD_TIMEOUT_US        500000 // 500ms
#define MAX_CONCURRENT_ACTIONS 10

typedef enum {
    STATE_IDLE,
    STATE_PRESSED_WAIT_HOLD,
    STATE_RELEASED_WAIT_DOUBLE,
    STATE_HELD
} action_state_t;

typedef struct {
    uint16_t action_code;
    action_state_t state;
    int64_t press_time_us;
} action_tracker_t;

static action_tracker_t s_trackers[MAX_CONCURRENT_ACTIONS];
static kb_sys_action_cb_t s_action_cb = NULL;
static TaskHandle_t s_task_handle = NULL;

static void notify_event(uint16_t action_code, kb_action_ev_t event) {
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

static action_tracker_t *alloc_tracker(uint16_t action_code) {
    for (int i = 0; i < MAX_CONCURRENT_ACTIONS; i++) {
        if (s_trackers[i].state == STATE_IDLE) {
            s_trackers[i].action_code = action_code;
            return &s_trackers[i];
        }
    }
    return NULL; // Overflow, should rarely happen for system keys
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
                if (now - t->press_time_us >= HOLD_TIMEOUT_US) {
                    // Hold timeout reached while still pressed
                    notify_event(t->action_code, KB_EV_HOLD);
                    t->state = STATE_HELD;
                }
            } else if (t->state == STATE_RELEASED_WAIT_DOUBLE) {
                if (now - t->press_time_us >= DOUBLE_TAP_TIMEOUT_US) {
                    // Double tap timeout reached after releasing
                    notify_event(t->action_code, KB_EV_SINGLE_TAP);
                    free_tracker(t);
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10)); // Check every 10ms
    }
}

void kb_system_action_init(void) {
    memset(s_trackers, 0, sizeof(s_trackers));
    if (s_task_handle == NULL) {
        xTaskCreate(sys_action_task, "kb_sys_action", 4096, NULL, 5, &s_task_handle);
    }
}

void kb_system_action_register_cb(kb_sys_action_cb_t cb) {
    s_action_cb = cb;
}

void kb_system_action_process(uint16_t action_code, bool is_pressed) {
    // Always notify raw press/release events
    notify_event(action_code, is_pressed ? KB_EV_PRESS : KB_EV_RELEASE);

    action_tracker_t *t = find_tracker(action_code);
    int64_t now = esp_timer_get_time();

    if (is_pressed) {
        if (t == NULL) {
            // First press
            t = alloc_tracker(action_code);
            if (t) {
                t->state = STATE_PRESSED_WAIT_HOLD;
                t->press_time_us = now;
            }
        } else {
            if (t->state == STATE_RELEASED_WAIT_DOUBLE) {
                // Second press within double tap window
                notify_event(action_code, KB_EV_DOUBLE_TAP);
                free_tracker(t);
            } else if (t->state == STATE_PRESSED_WAIT_HOLD || t->state == STATE_HELD) {
                // Duplicate press event (ignore or reset)
            }
        }
    } else {
        // Released
        if (t != NULL) {
            if (t->state == STATE_PRESSED_WAIT_HOLD) {
                // Released before hold timeout. Now wait to see if it's a double tap.
                t->state = STATE_RELEASED_WAIT_DOUBLE;
                // We keep the original press_time_us to measure the double tap window 
                // from the start of the first press. Alternatively, we could reset it 
                // to measure from release. Measuring from first press is standard.
            } else if (t->state == STATE_HELD) {
                // Was held, now released. We're done.
                free_tracker(t);
            }
        }
    }
}
