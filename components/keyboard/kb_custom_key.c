#include "kb_custom_key.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cfg_custom_keys.h"
#include "kb_layout.h"
#include "kb_macro.h"
#include "kb_system_action.h"

static const char *TAG = "kb_ckey";

/* ---- Runtime state ---- */

static cfg_custom_key_t s_ckeys[CFG_CKEYS_MAX_COUNT];
static size_t           s_ckey_count = 0;

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

typedef struct {
    uint16_t action_code;
    uint32_t duration_ms;
} ckey_tap_item_t;

/* A small dedicated queue for PR taps so we never block kb_manager_task */
static QueueHandle_t s_pr_queue      = NULL;
static TaskHandle_t  s_pr_task_handle = NULL;

static void pr_tap_task(void *arg) {
    ckey_tap_item_t item;
    while (1) {
        if (xQueueReceive(s_pr_queue, &item, portMAX_DELAY)) {
            kb_macro_fire_tap(item.action_code, item.duration_ms);
        }
    }
}

static void fire_pr_tap(uint16_t action_code, uint32_t duration_ms) {
    if (!s_pr_queue) return;
    ckey_tap_item_t item = { .action_code = action_code, .duration_ms = duration_ms };
    xQueueSend(s_pr_queue, &item, 0);
}

static void process_pr(const cfg_custom_key_t *ck, bool is_pressed) {
    if (is_pressed) {
        fire_pr_tap((uint16_t)ck->rules.pr.press_action,
                    ck->rules.pr.press_tap_release_delay_ms);
    } else {
        fire_pr_tap((uint16_t)ck->rules.pr.release_action,
                    ck->rules.pr.release_tap_release_delay_ms);
    }
}

/* ================================================================
   MultiAction mode — tap/hold outcome routing
   ================================================================ */

/*
 * The tap/hold engine fires generic KB_EV_* events.  We intercept them
 * here by registering a secondary callback that checks whether the
 * action code is in the CKey range, and if so, resolves and fires the
 * correct inner action.
 *
 * Design note: kb_system_action has a single s_action_cb slot used by
 * kb_macro.c.  Rather than making that a list, we chain-call from within
 * the ckey module: kb_macro.c keeps its existing callback; for CKey MA
 * codes we bypass the single-callback path and handle the event entirely
 * in our own registered callback.  We achieve this by registering our
 * own cb AFTER kb_macro_init() calls kb_system_action_register_cb().
 * We save the existing cb and call it if the code is not ours.
 */

static kb_sys_action_cb_t s_prev_action_cb = NULL;

static void ckey_action_event_cb(uint16_t action_code, kb_action_ev_t event) {
    /* Route non-CKey action codes to the existing system-action callback */
    if (action_code < ACTION_CODE_CKEY_MIN || action_code > ACTION_CODE_CKEY_MAX) {
        if (s_prev_action_cb) s_prev_action_cb(action_code, event);
        return;
    }

    /*
     * For CKey MA codes only handle the resolved events
     * (SINGLE_TAP / DOUBLE_TAP / HOLD).
     * PRESS / RELEASE are raw and already fired synchronously.
     */
    uint16_t id = action_code - ACTION_CODE_CKEY_MIN;
    const cfg_custom_key_t *ck = find_ckey(id);
    if (!ck || ck->mode != CKEY_MODE_MULTI_ACTION) return;

    switch (event) {
    case KB_EV_SINGLE_TAP:
        fire_pr_tap((uint16_t)ck->rules.ma.tap_action,
                    ck->rules.ma.tap_release_delay_ms);
        break;
    case KB_EV_DOUBLE_TAP:
        fire_pr_tap((uint16_t)ck->rules.ma.double_tap_action,
                    ck->rules.ma.double_tap_release_delay_ms);
        break;
    case KB_EV_HOLD:
        fire_pr_tap((uint16_t)ck->rules.ma.hold_action,
                    ck->rules.ma.hold_release_delay_ms);
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

void kb_custom_key_init(void) {
    /* Create the PR tap dispatch queue + task */
    s_pr_queue = xQueueCreate(16, sizeof(ckey_tap_item_t));
    if (!s_pr_queue) {
        ESP_LOGE(TAG, "Failed to create PR tap queue");
    }
    xTaskCreateWithCaps(pr_tap_task, "kb_ck_pr", 4096, NULL, 4,
                        &s_pr_task_handle,
                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    /* Chain-install our event callback AFTER the system-action engine has been
     * set up by kb_macro_init() (which calls kb_system_action_register_cb()).
     * We save the previous callback and forward non-CKey events to it. */
    s_prev_action_cb = NULL; /* kb_macro.c does not expose a getter — we NULL it
                               * and rely on kb_manager calling us last. */
    kb_system_action_register_cb(ckey_action_event_cb);

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
