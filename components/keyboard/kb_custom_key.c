#include "kb_custom_key.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

#include "cfg_custom_keys.h"
#include "kb_layout.h"
#include "kb_macro.h"
#include "kb_system_action.h"
#include "event_bus.h"

static const char *TAG = "kb_ckey";

/* ---- Runtime state ---- */

static size_t           s_ckey_count = 0;
static cfg_custom_key_t *s_ckeys = NULL;
static TickType_t       *s_press_end_tick = NULL;

/* ---- Lookup ---- */

static const cfg_custom_key_t *find_ckey(uint16_t id) {
    for (size_t i = 0; i < s_ckey_count; i++) {
        if (s_ckeys[i].id == id) return &s_ckeys[i];
    }
    ESP_LOGW(TAG, "find_ckey(%u) FAILED. Search space: %u items.", id, (unsigned)s_ckey_count);
    return NULL;
}

/* ================================================================
   PressRelease mode
   ================================================================ */

static void fire_pr_tap(uint16_t action_code, uint32_t duration_ms, uint32_t delay_ms) {
    kb_macro_fire_tap(action_code, duration_ms, delay_ms);
}

static void process_pr(const cfg_custom_key_t *ck, bool is_pressed) {
    uint16_t id = ck->id;
    if (is_pressed) {
        uint32_t dur = ck->rules.pr.press_tap_release_delay_ms;
        s_press_end_tick[id] = xTaskGetTickCount() + pdMS_TO_TICKS(dur);
        fire_pr_tap((uint16_t)ck->rules.pr.press_action, dur, 0);
    } else {
        uint32_t delay = 0;
        if (ck->rules.pr.wait_for_finish) {
            TickType_t now = xTaskGetTickCount();
            if (now < s_press_end_tick[id]) {
                delay = pdTICKS_TO_MS(s_press_end_tick[id] - now);
            }
        }
        fire_pr_tap((uint16_t)ck->rules.pr.release_action,
                    ck->rules.pr.release_tap_release_delay_ms,
                    delay);
    }
}

/* ================================================================
   MultiAction mode — tap/hold outcome routing
   ================================================================ */

static void ckey_action_event_handler(void *arg, esp_event_base_t base,
                                      int32_t id, void *data) {
    const kb_sys_action_event_t *ev = (const kb_sys_action_event_t *)data;
    uint16_t action_code = ev->action_code;
    kb_action_ev_t event = (kb_action_ev_t)ev->event;

    if (action_code < ACTION_CODE_CKEY_MIN || action_code > ACTION_CODE_CKEY_MAX) {
        return; // Not our event — other subscribers handle non-CKey codes.
    }

    /*
     * For CKey MA codes only handle the resolved events
     * (SINGLE_TAP / DOUBLE_TAP / HOLD).
     * PRESS / RELEASE are raw and already fired synchronously.
     */
    uint16_t ckey_id = action_code - ACTION_CODE_CKEY_MIN;
    const cfg_custom_key_t *ck = find_ckey(ckey_id);
    if (!ck || ck->mode != CKEY_MODE_MULTI_ACTION) return;

    switch (event) {
    case KB_EV_SINGLE_TAP:
        fire_pr_tap((uint16_t)ck->rules.ma.tap_action,
                    ck->rules.ma.tap_release_delay_ms, 0);
        break;
    case KB_EV_DOUBLE_TAP:
        fire_pr_tap((uint16_t)ck->rules.ma.double_tap_action,
                    ck->rules.ma.double_tap_release_delay_ms, 0);
        break;
    case KB_EV_HOLD:
        fire_pr_tap((uint16_t)ck->rules.ma.hold_action,
                    ck->rules.ma.hold_release_delay_ms, 0);
        break;
    default:
        break;
    }
}

static void process_ma(const cfg_custom_key_t *ck, bool is_pressed) {
    kb_sys_action_timing_t timing = {
        .double_tap_threshold_ms = ck->rules.ma.double_tap_threshold_ms,
        .hold_threshold_ms       = ck->rules.ma.hold_threshold_ms,
    };
    uint16_t action_code = (uint16_t)(ACTION_CODE_CKEY_MIN + ck->id);
    kb_system_action_process_ex(action_code, is_pressed, &timing);
}

/* ================================================================
   Public API
   ================================================================ */

void kb_custom_key_reload(const char *key) {
    ESP_LOGI(TAG, "Reloading custom keys due to update on key: %s", key ? key : "NULL");
    s_ckey_count = 0;
    esp_err_t err = ckeys_load_all(s_ckeys, &s_ckey_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load custom keys from NVS: 0x%X", (unsigned)err);
    } else {
        ESP_LOGI(TAG, "Loaded %u custom key(s) from NVS", (unsigned)s_ckey_count);
        for (size_t i = 0; i < s_ckey_count; i++) {
            ESP_LOGI(TAG, "  [%u] ID=%u, Mode=%d, Name='%s'", (unsigned)i, s_ckeys[i].id, (int)s_ckeys[i].mode, s_ckeys[i].name);
        }
    }
}

static void ckey_config_update_handler(void *arg, esp_event_base_t base,
                                       int32_t event_id, void *data) {
    const config_update_event_t *ev = (const config_update_event_t *)data;
    if (ev->kind == (uint8_t)CFGMOD_KIND_CKEY) {
        kb_custom_key_reload(ev->key);
    }
}

void kb_custom_key_init(void) {
    /* Allocate runtime state in PSRAM */
    s_ckeys = heap_caps_malloc(sizeof(cfg_custom_key_t) * CFG_CKEYS_MAX_COUNT, MALLOC_CAP_SPIRAM);
    s_press_end_tick = heap_caps_malloc(sizeof(TickType_t) * CFG_CKEYS_MAX_COUNT, MALLOC_CAP_SPIRAM);

    if (!s_ckeys || !s_press_end_tick) {
        ESP_LOGE(TAG, "Failed to allocate custom key memory in PSRAM");
        if (!s_ckeys) s_ckeys = malloc(sizeof(cfg_custom_key_t) * CFG_CKEYS_MAX_COUNT);
        if (!s_press_end_tick) s_press_end_tick = malloc(sizeof(TickType_t) * CFG_CKEYS_MAX_COUNT);
    }

    if (s_press_end_tick) {
        memset(s_press_end_tick, 0, sizeof(TickType_t) * CFG_CKEYS_MAX_COUNT);
    }

    /* Subscribe to system action events for MultiAction CKey processing.
     * Each subscriber registers independently — no manual chain-forwarding needed. */
    esp_event_handler_register(KB_EVENTS, KB_EVENT_SYSTEM_ACTION,
                               ckey_action_event_handler, NULL);

    /* Subscribe to config update events to reload custom keys when they change. */
    esp_event_handler_register(CONFIG_EVENTS, CONFIG_EVENT_KIND_UPDATED,
                               ckey_config_update_handler, NULL);

    /* Load initial table */
    kb_custom_key_reload("init");
}

void kb_custom_key_process_action(uint16_t action_code, bool is_pressed) {
    uint16_t id = action_code - ACTION_CODE_CKEY_MIN;
    const cfg_custom_key_t *ck = find_ckey(id);
    if (!ck) {
        ESP_LOGW(TAG, "Custom key ID %u not found", id);
        return;
    }

    ESP_LOGI(TAG, "CKey %u ('%s') %s", id, ck->name, is_pressed ? "DOWN" : "UP");

    switch (ck->mode) {
    case CKEY_MODE_PRESS_RELEASE:
        process_pr(ck, is_pressed);
        break;
    case CKEY_MODE_MULTI_ACTION:
        process_ma(ck, is_pressed);
        break;
    default:
        ESP_LOGW(TAG, "Unknown mode %d for custom key %u", (int)ck->mode, id);
        break;
    }
}
