#include "kb_macro.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "cfgmod.h"
#include "cfg_macros.h"
#include "class/hid/hid.h"
#include "kb_bitmap.h"
#include "kb_custom_key.h"
#include "kb_layout.h"
#include "kb_report.h"
#include "kb_system_action.h"

static const char *TAG = "kb_macro";

/* ---- Virtual NKRO state (256-bit keycode bitmap) ---- */
static uint8_t            s_v_nkro[32];
static SemaphoreHandle_t  s_v_nkro_mutex = NULL;

/* ---- Active layer state ---- */
static uint8_t s_active_layer  = KB_LAYER_BASE;
static bool    s_is_fn1_held   = false;
static bool    s_is_fn2_held   = false;

/* ---- Macro task queues ---- */
static QueueHandle_t s_macro_queue = NULL;
static QueueHandle_t s_tap_queue   = NULL;

/* Sentinel value: queue item with this macro_id is a fire-tap request */
#define MACRO_ID_FIRE_TAP 0xFFFF

typedef struct {
    uint16_t macro_id;
    bool     is_pressed;
    /* fire-tap payload (used when macro_id == MACRO_ID_FIRE_TAP) */
    uint16_t tap_action;
    uint32_t tap_duration_ms;
    uint32_t tap_delay_ms;
} macro_queue_item_t;

/* ---- Per-macro runtime state ---- */
typedef struct {
    volatile bool    is_running;
    volatile uint8_t pending_count;
    volatile bool    key_held;
    volatile bool    toggle_active;
    volatile bool    cancel_requested;
    volatile uint8_t burst_remaining;
} macro_rt_state_t;

static macro_rt_state_t   s_rt_state[CFG_MACROS_MAX_COUNT];
static cfg_macro_list_t  *s_macros = NULL;

/* ============================================================
   Virtual NKRO state helpers
   ============================================================ */

void kb_macro_virtual_press(uint8_t hid_keycode) {
    if (xSemaphoreTake(s_v_nkro_mutex, portMAX_DELAY)) {
        kb_bit_set(s_v_nkro, hid_keycode);
        xSemaphoreGive(s_v_nkro_mutex);
    }
}

void kb_macro_virtual_release(uint8_t hid_keycode) {
    if (xSemaphoreTake(s_v_nkro_mutex, portMAX_DELAY)) {
        kb_bit_clear(s_v_nkro, hid_keycode);
        xSemaphoreGive(s_v_nkro_mutex);
    }
}

esp_err_t kb_macro_send_report(void) {
    esp_err_t err = ESP_FAIL;
    if (xSemaphoreTake(s_v_nkro_mutex, portMAX_DELAY)) {
        /* Retry up to 100 times when the HID endpoint is busy */
        for (int i = 0; i < 100; i++) {
            err = kb_send_report(s_v_nkro);
            if (err == ESP_OK) break;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        xSemaphoreGive(s_v_nkro_mutex);
    }
    return err;
}

/* Clear all virtual keys and send an empty report.
 * Called by the macro executor on cancellation to avoid stuck keys. */
static void kb_macro_force_clear(void) {
    if (xSemaphoreTake(s_v_nkro_mutex, portMAX_DELAY)) {
        memset(s_v_nkro, 0, sizeof(s_v_nkro));
        xSemaphoreGive(s_v_nkro_mutex);
        kb_macro_send_report();
    }
}

uint8_t kb_macro_get_active_layer(void) { return s_active_layer; }

/* ============================================================
   Layer state
   ============================================================ */

static void update_layer_state(void) {
    if (s_is_fn1_held && s_is_fn2_held) {
        s_active_layer = KB_LAYER_FN3;
    } else if (s_is_fn2_held) {
        s_active_layer = KB_LAYER_FN2;
    } else if (s_is_fn1_held) {
        s_active_layer = KB_LAYER_FN1;
    } else {
        s_active_layer = KB_LAYER_BASE;
    }
}

/* ============================================================
   Macro execution engine
   ============================================================ */

static const cfg_macro_t *find_macro(uint16_t macro_id, size_t *out_idx) {
    for (size_t i = 0; i < s_macros->count; i++) {
        if (s_macros->macros[i].id == macro_id) {
            if (out_idx) *out_idx = i;
            return &s_macros->macros[i];
        }
    }
    return NULL;
}

/* Execute all events of a macro.  Returns true if completed normally,
 * false if cancelled (only possible when is_cancellable is true). */
static bool execute_macro_cancellable(uint16_t macro_id, uint8_t depth,
                                      bool is_cancellable, macro_rt_state_t *rt) {
    if (depth > 5) {
        ESP_LOGW(TAG, "Macro %u: recursion depth limit reached", macro_id);
        return true;
    }

    const cfg_macro_t *m = NULL;
    for (size_t i = 0; i < s_macros->count; i++) {
        if (s_macros->macros[i].id == macro_id) {
            m = &s_macros->macros[i];
            break;
        }
    }
    if (!m) {
        ESP_LOGW(TAG, "Macro ID %u not found", macro_id);
        return true;
    }

    ESP_LOGD(TAG, "Executing macro %u depth=%u: %s", m->id, (unsigned)depth, m->name);

    for (size_t i = 0; i < m->event_count; i++) {
        if (is_cancellable && rt && rt->cancel_requested) {
            ESP_LOGD(TAG, "Macro %u cancelled", macro_id);
            kb_macro_force_clear();
            return false;
        }

        uint16_t val = m->events[i].value;

        switch (m->events[i].type) {
        case MACRO_EVT_KEY_PRESS:
            if (val >= ACTION_CODE_MACRO_MIN && val <= ACTION_CODE_MACRO_MAX) {
                if (!execute_macro_cancellable(val - ACTION_CODE_MACRO_MIN,
                                               depth + 1, is_cancellable, rt)) {
                    return false;
                }
            } else {
                kb_macro_process_action(val, true);
                kb_macro_send_report();
            }
            break;

        case MACRO_EVT_KEY_RELEASE:
            if (val < ACTION_CODE_MACRO_MIN || val > ACTION_CODE_MACRO_MAX) {
                kb_macro_process_action(val, false);
                kb_macro_send_report();
            }
            /* Release of a nested macro reference is a no-op */
            break;

        case MACRO_EVT_KEY_TAP:
            if (val >= ACTION_CODE_MACRO_MIN && val <= ACTION_CODE_MACRO_MAX) {
                if (!execute_macro_cancellable(val - ACTION_CODE_MACRO_MIN,
                                               depth + 1, is_cancellable, rt)) {
                    return false;
                }
            } else {
                kb_macro_process_action(val, true);
                kb_macro_send_report();
                vTaskDelay(pdMS_TO_TICKS(m->events[i].press_duration_ms));
                kb_macro_process_action(val, false);
                kb_macro_send_report();
            }
            break;

        case MACRO_EVT_DELAY_MS:
            if (is_cancellable && rt) {
                /* Split into 10 ms chunks so cancellation stays responsive */
                uint16_t remaining = val;
                while (remaining > 0) {
                    if (rt->cancel_requested) {
                        kb_macro_force_clear();
                        return false;
                    }
                    uint16_t chunk = remaining > 10 ? 10 : remaining;
                    vTaskDelay(pdMS_TO_TICKS(chunk));
                    remaining -= chunk;
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(val));
            }
            break;

        default:
            break;
        }

        /* Inter-event delay */
        uint32_t delay_ms = m->events[i].delay_ms;
        if (delay_ms > 0) {
            if (is_cancellable && rt) {
                uint32_t remaining = delay_ms;
                while (remaining > 0) {
                    if (rt->cancel_requested) {
                        kb_macro_force_clear();
                        return false;
                    }
                    uint32_t chunk = remaining > 10 ? 10 : remaining;
                    vTaskDelay(pdMS_TO_TICKS(chunk));
                    remaining -= chunk;
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(delay_ms));
            }
        }
    }
    return true;
}

/* ============================================================
   Macro task (one-shot + stacking modes)
   ============================================================ */

static void macro_task(void *arg) {
    macro_queue_item_t item;
    while (1) {
        if (!xQueueReceive(s_macro_queue, &item, portMAX_DELAY)) continue;
        if (!item.is_pressed) continue;

        size_t idx = 0;
        const cfg_macro_t *m = find_macro(item.macro_id, &idx);
        if (!m) continue;

        macro_rt_state_t *rt = &s_rt_state[idx];
        uint8_t mode = m->exec_mode;
        bool is_cancellable = (mode == MACRO_EXEC_HOLD_REPEAT_CANCEL ||
                               mode == MACRO_EXEC_TOGGLE_REPEAT_CANCEL);

        rt->is_running       = true;
        rt->cancel_requested = false;

        switch (mode) {
        case MACRO_EXEC_ONCE_STACK_ONCE:
        case MACRO_EXEC_ONCE_NO_STACK:
        case MACRO_EXEC_ONCE_STACK_N:
            execute_macro_cancellable(item.macro_id, 0, false, rt);
            while (rt->pending_count > 0) {
                rt->pending_count--;
                execute_macro_cancellable(item.macro_id, 0, false, rt);
            }
            break;

        case MACRO_EXEC_HOLD_REPEAT:
        case MACRO_EXEC_HOLD_REPEAT_CANCEL:
            while (rt->key_held) {
                if (!execute_macro_cancellable(item.macro_id, 0, is_cancellable, rt)) break;
                if (!rt->key_held) break;
                vTaskDelay(pdMS_TO_TICKS(5));
            }
            break;

        case MACRO_EXEC_TOGGLE_REPEAT:
        case MACRO_EXEC_TOGGLE_REPEAT_CANCEL:
            while (rt->toggle_active) {
                if (!execute_macro_cancellable(item.macro_id, 0, is_cancellable, rt)) break;
                if (!rt->toggle_active) break;
                vTaskDelay(pdMS_TO_TICKS(5));
            }
            break;

        case MACRO_EXEC_BURST_N:
            for (uint8_t i = 0; i < rt->burst_remaining; i++) {
                execute_macro_cancellable(item.macro_id, 0, false, rt);
                if (i + 1 < rt->burst_remaining) vTaskDelay(pdMS_TO_TICKS(5));
            }
            rt->burst_remaining = 0;
            break;

        default:
            execute_macro_cancellable(item.macro_id, 0, false, rt);
            break;
        }

        rt->is_running       = false;
        rt->cancel_requested = false;
    }
}

/* ============================================================
   Tap worker tasks (fire-tap from custom keys / system actions)
   ============================================================ */

static void tap_worker_task(void *arg) {
    macro_queue_item_t item;
    while (1) {
        if (!xQueueReceive(s_tap_queue, &item, portMAX_DELAY)) continue;

        if (item.tap_delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(item.tap_delay_ms));
        }

        uint32_t dur = item.tap_duration_ms > 0 ? item.tap_duration_ms : 10;
        kb_macro_process_action(item.tap_action, true);
        kb_macro_send_report();
        vTaskDelay(pdMS_TO_TICKS(dur));
        kb_macro_process_action(item.tap_action, false);
        kb_macro_send_report();
    }
}

/* ============================================================
   System and media action dispatch
   ============================================================ */

static void process_media_action(uint16_t action, bool is_pressed) {
    uint16_t media_code = 0;
    if (is_pressed) {
        switch (action) {
        case MEDIA_ACTION_TOGGLE:    media_code = HID_USAGE_CONSUMER_PLAY_PAUSE;          break;
        case MEDIA_ACTION_NEXT:      media_code = HID_USAGE_CONSUMER_SCAN_NEXT_TRACK;     break;
        case MEDIA_ACTION_PREV:      media_code = HID_USAGE_CONSUMER_SCAN_PREVIOUS_TRACK; break;
        case SYS_ACTION_VOLUME_UP:   media_code = HID_USAGE_CONSUMER_VOLUME_INCREMENT;    break;
        case SYS_ACTION_VOLUME_DOWN: media_code = HID_USAGE_CONSUMER_VOLUME_DECREMENT;    break;
        case SYS_ACTION_MUTE:        media_code = HID_USAGE_CONSUMER_MUTE;                break;
        default: break;
        }
    }
    /* Always send (including zero on release to signal key-up to the host) */
    kb_send_consumer_report(media_code);
}

static void process_system_action(uint16_t action, bool is_pressed) {
    /* Layer keys — stateful, handled immediately */
    if (action == SYS_ACTION_LAYER_FN1) {
        s_is_fn1_held = is_pressed;
        update_layer_state();
        return;
    }
    if (action == SYS_ACTION_LAYER_FN2) {
        s_is_fn2_held = is_pressed;
        update_layer_state();
        return;
    }

    /* Media / volume — forward to HID consumer endpoint */
    if (action >= SYS_ACTION_VOLUME_UP && action <= MEDIA_ACTION_TOGGLE) {
        process_media_action(action, is_pressed);
        return;
    }

    /* BLE actions — routed through the tap/hold engine on press; release also forwarded */
    bool is_ble_action = (action == SYS_ACTION_BLE_TOGGLE) ||
                         (action >= SYS_ACTION_BLE_ON && action <= SYS_ACTION_BLE_9);
    if (is_ble_action) {
        kb_system_action_process(action, is_pressed);
        return;
    }

    /* Brightness and RGB — stubs for future implementation */
    if (is_pressed) {
        if (action == SYS_ACTION_BRIGHTNESS_UP || action == SYS_ACTION_BRIGHTNESS_DOWN) {
            ESP_LOGD(TAG, "Brightness action 0x%04X not yet implemented", action);
        } else if (action >= SYS_ACTION_RGB_MODE_NEXT && action <= SYS_ACTION_RGB_BRIGHTNESS_DOWN) {
            ESP_LOGD(TAG, "RGB action 0x%04X not yet implemented", action);
        } else {
            ESP_LOGW(TAG, "Unknown system action 0x%04X", action);
        }
    }
}

/* ============================================================
   Public action dispatcher
   ============================================================ */

void kb_macro_process_action(uint16_t action_code, bool is_pressed) {
    /* Standard HID key */
    if (action_code >= ACTION_CODE_HID_MIN && action_code <= ACTION_CODE_HID_MAX) {
        if (is_pressed) {
            kb_macro_virtual_press((uint8_t)action_code);
        } else {
            kb_macro_virtual_release((uint8_t)action_code);
        }
        return;
    }

    /* System / layer / media action */
    if (action_code >= ACTION_CODE_SYSTEM_MIN && action_code <= ACTION_CODE_SYSTEM_MAX) {
        process_system_action(action_code, is_pressed);
        return;
    }

    /* Multi-step macro */
    if (action_code >= ACTION_CODE_MACRO_MIN && action_code <= ACTION_CODE_MACRO_MAX) {
        uint16_t macro_id = action_code - ACTION_CODE_MACRO_MIN;

        size_t idx = 0;
        const cfg_macro_t *m = find_macro(macro_id, &idx);
        if (!m) return;

        macro_rt_state_t *rt  = &s_rt_state[idx];
        uint8_t           mode = m->exec_mode;

        if (is_pressed) {
            switch (mode) {
            case MACRO_EXEC_ONCE_STACK_ONCE:
                if (rt->is_running) {
                    if (rt->pending_count < 1) rt->pending_count = 1;
                } else {
                    rt->pending_count = 0;
                    macro_queue_item_t item = { .macro_id = macro_id, .is_pressed = true };
                    xQueueSend(s_macro_queue, &item, 0);
                }
                break;

            case MACRO_EXEC_ONCE_NO_STACK:
                if (!rt->is_running) {
                    macro_queue_item_t item = { .macro_id = macro_id, .is_pressed = true };
                    xQueueSend(s_macro_queue, &item, 0);
                }
                break;

            case MACRO_EXEC_ONCE_STACK_N: {
                uint8_t max_pending = m->stack_max > 0 ? m->stack_max : 1;
                if (rt->is_running) {
                    if (rt->pending_count < max_pending) rt->pending_count++;
                } else {
                    rt->pending_count = 0;
                    macro_queue_item_t item = { .macro_id = macro_id, .is_pressed = true };
                    xQueueSend(s_macro_queue, &item, 0);
                }
                break;
            }

            case MACRO_EXEC_HOLD_REPEAT:
            case MACRO_EXEC_HOLD_REPEAT_CANCEL:
                rt->key_held         = true;
                rt->cancel_requested = false;
                if (!rt->is_running) {
                    macro_queue_item_t item = { .macro_id = macro_id, .is_pressed = true };
                    xQueueSend(s_macro_queue, &item, 0);
                }
                break;

            case MACRO_EXEC_TOGGLE_REPEAT:
            case MACRO_EXEC_TOGGLE_REPEAT_CANCEL:
                if (rt->toggle_active) {
                    rt->toggle_active = false;
                    if (mode == MACRO_EXEC_TOGGLE_REPEAT_CANCEL) {
                        rt->cancel_requested = true;
                    }
                } else {
                    rt->toggle_active    = true;
                    rt->cancel_requested = false;
                    if (!rt->is_running) {
                        macro_queue_item_t item = { .macro_id = macro_id, .is_pressed = true };
                        xQueueSend(s_macro_queue, &item, 0);
                    }
                }
                break;

            case MACRO_EXEC_BURST_N:
                if (!rt->is_running) {
                    rt->burst_remaining = m->repeat_count > 0 ? m->repeat_count : 1;
                    macro_queue_item_t item = { .macro_id = macro_id, .is_pressed = true };
                    xQueueSend(s_macro_queue, &item, 0);
                }
                break;

            default: {
                macro_queue_item_t item = { .macro_id = macro_id, .is_pressed = true };
                xQueueSend(s_macro_queue, &item, 0);
                break;
            }
            }
        } else {
            /* Key release */
            switch (mode) {
            case MACRO_EXEC_HOLD_REPEAT:
                rt->key_held = false;
                break;
            case MACRO_EXEC_HOLD_REPEAT_CANCEL:
                rt->key_held         = false;
                rt->cancel_requested = true;
                break;
            default:
                break;
            }
        }
        return;
    }

    /* Custom key */
    if (action_code >= ACTION_CODE_CKEY_MIN && action_code <= ACTION_CODE_CKEY_MAX) {
        kb_custom_key_process_action(action_code, is_pressed);
        return;
    }
}

void kb_macro_fire_tap(uint16_t action_code, uint32_t duration_ms, uint32_t delay_ms) {
    if (!s_tap_queue) return;
    macro_queue_item_t item = {
        .macro_id        = MACRO_ID_FIRE_TAP,
        .is_pressed      = false,
        .tap_action      = action_code,
        .tap_duration_ms = duration_ms,
        .tap_delay_ms    = delay_ms,
    };
    xQueueSend(s_tap_queue, &item, 0);
}

/* ============================================================
   Reload callback (called by cfgmod on NVS update)
   ============================================================ */

static void on_macros_updated(const char *key) {
    (void)key;
    ESP_LOGI(TAG, "Reloading macros from NVS");
    macros_load_all(s_macros);
    memset(s_rt_state, 0, sizeof(s_rt_state));
}

/* ============================================================
   Initialization
   ============================================================ */

void kb_macro_init(void) {
    memset(s_v_nkro, 0, sizeof(s_v_nkro));
    memset(s_rt_state, 0, sizeof(s_rt_state));
    s_active_layer = KB_LAYER_BASE;

    s_v_nkro_mutex = xSemaphoreCreateMutex();
    s_macro_queue  = xQueueCreate(32, sizeof(macro_queue_item_t));
    s_tap_queue    = xQueueCreate(32, sizeof(macro_queue_item_t));

    /* Allocate macro cache in PSRAM, fall back to internal RAM */
    s_macros = heap_caps_malloc(sizeof(cfg_macro_list_t), MALLOC_CAP_SPIRAM);
    if (!s_macros) {
        ESP_LOGE(TAG, "PSRAM unavailable for macro cache, using internal RAM");
        s_macros = malloc(sizeof(cfg_macro_list_t));
    }
    if (!s_macros) {
        ESP_LOGE(TAG, "FATAL: cannot allocate macro cache");
        return;
    }
    memset(s_macros, 0, sizeof(cfg_macro_list_t));
    macros_load_all(s_macros);

    cfgmod_register_kind(CFGMOD_KIND_MACRO, macros_default, macros_deserialize,
                         macros_serialize, on_macros_updated, sizeof(cfg_macro_list_t));

    xTaskCreateWithCaps(macro_task, "kb_macro", 5120, NULL, 4, NULL,
                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    for (int i = 0; i < 4; i++) {
        char name[12];
        snprintf(name, sizeof(name), "kb_tap_%d", i);
        xTaskCreateWithCaps(tap_worker_task, name, 3072, NULL, 4, NULL,
                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

    ESP_LOGI(TAG, "Macro engine initialized");
}
