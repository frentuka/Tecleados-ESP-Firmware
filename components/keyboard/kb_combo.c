#include "kb_combo.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "cfg_combos.h"
#include "kb_layout.h"
#include "kb_macro.h"
#include "event_bus.h"

static const char *TAG = "kb_combo";

// Loaded combo definitions
static cfg_combo_t *s_combos = NULL;
static size_t       s_combo_count = 0;

// Per-combo runtime state
typedef struct {
    uint8_t  matched_count;                      // Trigger keys currently pressed
    bool     keys_pressed[CFG_COMBO_MAX_KEYS];   // Which trigger keys are down
    bool     is_active;                          // Combo fully matched and action fired
} combo_rt_t;
static combo_rt_t *s_combo_rt = NULL;

// Suppressed key buffer (only used for delayedPress combos)
#define MAX_SUPPRESSED 16
typedef struct {
    uint8_t  row, col;
    int64_t  timestamp_us;
    uint16_t action_code;   // Resolved at suppression time
    uint8_t  layer;
} suppressed_key_t;
static suppressed_key_t s_suppressed[MAX_SUPPRESSED];
static uint8_t          s_suppressed_count = 0;

/* ================================================================
 * Internal Helpers
 * ================================================================ */

static bool is_subset(const cfg_combo_t *sub, const cfg_combo_t *super) {
    for (uint8_t i = 0; i < sub->key_count; i++) {
        bool found = false;
        for (uint8_t j = 0; j < super->key_count; j++) {
            if (sub->keys[i].row == super->keys[j].row && sub->keys[i].col == super->keys[j].col) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

static int compare_combo_desc(const void *a, const void *b) {
    const cfg_combo_t *ca = (const cfg_combo_t *)a;
    const cfg_combo_t *cb = (const cfg_combo_t *)b;
    return (int)cb->key_count - (int)ca->key_count;
}

static void flush_all_suppressed(void) {
    for (uint8_t i = 0; i < s_suppressed_count; i++) {
        // Fire the individual action retroactively
        kb_macro_process_action(s_suppressed[i].action_code, true);
    }
    s_suppressed_count = 0;
}

static void discard_all_suppressed_for_combo(const cfg_combo_t *combo) {
    // If a combo matched, we discard any suppressed keys that belong to it
    // so they never fire individually.
    for (uint8_t k = 0; k < combo->key_count; k++) {
        uint8_t cr = combo->keys[k].row;
        uint8_t cc = combo->keys[k].col;

        for (uint8_t i = 0; i < s_suppressed_count; ) {
            if (s_suppressed[i].row == cr && s_suppressed[i].col == cc) {
                // Remove from array by shifting
                for (uint8_t j = i; j < s_suppressed_count - 1; j++) {
                    s_suppressed[j] = s_suppressed[j + 1];
                }
                s_suppressed_count--;
            } else {
                i++;
            }
        }
    }
}

static void release_individual_keys_for_combo(const cfg_combo_t *combo, uint8_t layer) {
    // Used when cancelKeys is true and a combo triggers.
    // Retroactively release the individual keys that were already sent.
    for (uint8_t k = 0; k < combo->key_count; k++) {
        uint8_t cr = combo->keys[k].row;
        uint8_t cc = combo->keys[k].col;
        uint16_t action = kb_layout_get_action_code(cr, cc, layer);
        if (action != ACTION_CODE_NONE) {
            kb_macro_process_action(action, false);
        }
    }
}

/* ================================================================
 * Initialization & Reload
 * ================================================================ */

static void kb_combo_reload(const char *key) {
    ESP_LOGI(TAG, "Reloading combos due to update on key: %s", key ? key : "NULL");
    s_combo_count = 0;
    esp_err_t err = combos_load_all(s_combos, &s_combo_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load combos from NVS: 0x%X", (unsigned)err);
    } else {
        ESP_LOGI(TAG, "Loaded %u combo(s) from NVS", (unsigned)s_combo_count);
        if (s_combo_count > 1) {
            qsort(s_combos, s_combo_count, sizeof(cfg_combo_t), compare_combo_desc);
        }
    }
    
    // Reset runtime state
    if (s_combo_rt) {
        memset(s_combo_rt, 0, sizeof(combo_rt_t) * CFG_COMBOS_MAX_COUNT);
    }
    s_suppressed_count = 0;
}

static void combo_config_update_handler(void *arg, esp_event_base_t base,
                                        int32_t event_id, void *data) {
    const config_update_event_t *ev = (const config_update_event_t *)data;
    if (ev->kind == (uint8_t)CFGMOD_KIND_COMBO) {
        kb_combo_reload(ev->key);
    }
}

void kb_combo_init(void) {
    s_combos = heap_caps_malloc(sizeof(cfg_combo_t) * CFG_COMBOS_MAX_COUNT, MALLOC_CAP_SPIRAM);
    s_combo_rt = heap_caps_malloc(sizeof(combo_rt_t) * CFG_COMBOS_MAX_COUNT, MALLOC_CAP_SPIRAM);

    if (!s_combos || !s_combo_rt) {
        ESP_LOGE(TAG, "Failed to allocate combo memory in PSRAM");
        if (!s_combos) s_combos = malloc(sizeof(cfg_combo_t) * CFG_COMBOS_MAX_COUNT);
        if (!s_combo_rt) s_combo_rt = malloc(sizeof(combo_rt_t) * CFG_COMBOS_MAX_COUNT);
    }

    if (s_combo_rt) {
        memset(s_combo_rt, 0, sizeof(combo_rt_t) * CFG_COMBOS_MAX_COUNT);
    }
    s_suppressed_count = 0;

    // Register config update callback
    esp_event_handler_register(CONFIG_EVENTS, CONFIG_EVENT_KIND_UPDATED,
                               combo_config_update_handler, NULL);

    // Initial load
    kb_combo_reload("init");
}

/* ================================================================
 * Runtime Engine
 * ================================================================ */

bool kb_combo_process_key(uint8_t row, uint8_t col, bool is_pressed, uint8_t layer) {
    if (s_combo_count == 0) return false;

    bool is_delayed_trigger = false;

    for (size_t i = 0; i < s_combo_count; i++) {
        const cfg_combo_t *cmb = &s_combos[i];
        combo_rt_t *rt = &s_combo_rt[i];

        // 1. Layer filtering
        if ((cmb->active_layers & (1 << layer)) == 0) continue;

        // 2. Check if this key is part of the combo
        int trigger_idx = -1;
        for (uint8_t k = 0; k < cmb->key_count; k++) {
            if (cmb->keys[k].row == row && cmb->keys[k].col == col) {
                trigger_idx = k;
                break;
            }
        }

        if (trigger_idx == -1) continue; // Not a trigger for this combo

        if (is_pressed) {
            // Key Down

            // Strict order check
            if (cmb->strict_order && trigger_idx != rt->matched_count) {
                // Pressed out of order; reset this combo's progress
                rt->matched_count = 0;
                memset(rt->keys_pressed, 0, sizeof(rt->keys_pressed));
                continue;
            }

            if (!rt->keys_pressed[trigger_idx]) {
                rt->keys_pressed[trigger_idx] = true;
                rt->matched_count++;
            }

            if (cmb->delayed_press) {
                is_delayed_trigger = true;
            }

            if (rt->matched_count == cmb->key_count) {
                // Check if this combo is subsumed by an already active longer combo
                bool is_subsumed = false;
                for (size_t j = 0; j < s_combo_count; j++) {
                    if (s_combo_rt[j].is_active && s_combos[j].key_count > cmb->key_count) {
                        if (is_subset(cmb, &s_combos[j])) {
                            is_subsumed = true;
                            break;
                        }
                    }
                }

                if (is_subsumed) {
                    continue; // Skip firing this combo
                }

                // Combo triggered!
                // Cancel any active smaller combos that are subsets of this new combo
                for (size_t j = 0; j < s_combo_count; j++) {
                    if (s_combo_rt[j].is_active && s_combos[j].key_count < cmb->key_count) {
                        if (is_subset(&s_combos[j], cmb)) {
                            kb_macro_process_action(s_combos[j].action, false);
                            s_combo_rt[j].is_active = false;
                        }
                    }
                }

                if (cmb->delayed_press) {
                    discard_all_suppressed_for_combo(cmb);
                } else if (cmb->cancel_keys) {
                    release_individual_keys_for_combo(cmb, layer);
                }

                // Fire combo action
                kb_macro_process_action(cmb->action, true);
                rt->is_active = true;
                
                // If it's a delayed press combo, we consider the key consumed
                if (cmb->delayed_press) {
                    return true;
                }
            }

        } else {
            // Key Up
            if (rt->keys_pressed[trigger_idx]) {
                rt->keys_pressed[trigger_idx] = false;
                if (rt->matched_count > 0) rt->matched_count--;

                if (rt->is_active) {
                    if (cmb->release_on_first_key || rt->matched_count == 0) {
                        // All combo keys released, or release on first key, release combo action
                        kb_macro_process_action(cmb->action, false);
                        rt->is_active = false;
                    }
                }
            }
        }
    }

    if (is_pressed) {
        if (is_delayed_trigger) {
            // Key is part of a delayedPress combo and hasn't triggered the combo yet.
            // Suppress it.
            if (s_suppressed_count < MAX_SUPPRESSED) {
                s_suppressed[s_suppressed_count].row = row;
                s_suppressed[s_suppressed_count].col = col;
                s_suppressed[s_suppressed_count].timestamp_us = esp_timer_get_time();
                s_suppressed[s_suppressed_count].action_code = kb_layout_get_action_code(row, col, layer);
                s_suppressed[s_suppressed_count].layer = layer;
                s_suppressed_count++;
            }
            return true; // Consumed (suppressed)
        } else {
            // Not a delayed trigger, flush any existing suppressed keys and proceed normally
            flush_all_suppressed();
        }
    } else {
        // Key Up: Remove from suppressed buffer if it was there (never fired)
        for (uint8_t j = 0; j < s_suppressed_count; ) {
            if (s_suppressed[j].row == row && s_suppressed[j].col == col) {
                // Was suppressed and now released before triggering a combo.
                // Fire the original press and immediate release.
                kb_macro_process_action(s_suppressed[j].action_code, true);
                kb_macro_process_action(s_suppressed[j].action_code, false);

                for (uint8_t m = j; m < s_suppressed_count - 1; m++) {
                    s_suppressed[m] = s_suppressed[m + 1];
                }
                s_suppressed_count--;
            } else {
                j++;
            }
        }
    }

    return false;
}

void kb_combo_tick(int64_t now_us) {
    if (s_suppressed_count == 0) return;

    // Check for timeouts
    for (uint8_t i = 0; i < s_suppressed_count; ) {
        // Find the maximum delay window across all active combos this key belongs to
        int max_delay_ms = 0;
        
        for (size_t c = 0; c < s_combo_count; c++) {
            const cfg_combo_t *cmb = &s_combos[c];
            if (!cmb->delayed_press || (cmb->active_layers & (1 << s_suppressed[i].layer)) == 0) continue;
            
            bool is_trigger = false;
            for (uint8_t k = 0; k < cmb->key_count; k++) {
                if (cmb->keys[k].row == s_suppressed[i].row && cmb->keys[k].col == s_suppressed[i].col) {
                    is_trigger = true;
                    break;
                }
            }
            if (is_trigger && cmb->delay_ms > max_delay_ms) {
                max_delay_ms = cmb->delay_ms;
            }
        }

        // If no combo wants this key anymore, or it timed out, flush it
        if (max_delay_ms == 0 || (now_us - s_suppressed[i].timestamp_us) > (max_delay_ms * 1000LL)) {
            kb_macro_process_action(s_suppressed[i].action_code, true);
            
            // Remove from array
            for (uint8_t j = i; j < s_suppressed_count - 1; j++) {
                s_suppressed[j] = s_suppressed[j + 1];
            }
            s_suppressed_count--;
        } else {
            i++;
        }
    }
}
