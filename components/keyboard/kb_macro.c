#include "kb_macro.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

#include "cfgmod.h"
#include "cJSON.h"
#include "cfg_macros.h"
#include "class/hid/hid.h"
#include "kb_layout.h"
#include "kb_report.h"

static const char *TAG = "kb_macro";

static uint8_t s_v_nkro[32];       // 256 bits for any standard HID keycode
static uint8_t s_active_layer = 0; // KB_LAYER_BASE
static bool s_is_fn1_held = false;
static bool s_is_fn2_held = false;
static SemaphoreHandle_t s_v_nkro_mutex = NULL;
static QueueHandle_t s_macro_queue = NULL;

typedef struct {
  uint16_t macro_id;
  bool is_pressed;   // true=key down, false=key up
} macro_queue_item_t;

// Per-macro runtime state for execution mode logic
typedef struct {
  volatile bool is_running;       // Currently executing
  volatile uint8_t pending_count; // Queued executions waiting
  volatile bool key_held;         // Physical key currently held
  volatile bool toggle_active;    // Toggle repeat is active
  volatile bool cancel_requested; // Abort current execution
  volatile uint8_t burst_remaining; // Remaining burst iterations
} macro_rt_state_t;

static macro_rt_state_t s_rt_state[CFG_MACROS_MAX_COUNT];
static cfg_macro_list_t s_macros;

static inline void set_bit(uint8_t *bitmap, size_t bit_index) {
  bitmap[bit_index >> 3] |= (uint8_t)(1U << (bit_index & 7U));
}

static inline void clear_bit(uint8_t *bitmap, size_t bit_index) {
  bitmap[bit_index >> 3] &= (uint8_t)~(1U << (bit_index & 7U));
}

void kb_macro_virtual_press(uint8_t hid_keycode) {
  if (xSemaphoreTake(s_v_nkro_mutex, portMAX_DELAY)) {
    set_bit(s_v_nkro, hid_keycode);
    xSemaphoreGive(s_v_nkro_mutex);
  }
}

void kb_macro_virtual_release(uint8_t hid_keycode) {
  if (xSemaphoreTake(s_v_nkro_mutex, portMAX_DELAY)) {
    clear_bit(s_v_nkro, hid_keycode);
    xSemaphoreGive(s_v_nkro_mutex);
  }
}

uint8_t kb_macro_get_active_layer(void) { return s_active_layer; }

esp_err_t kb_macro_send_report(void) {
  esp_err_t err = ESP_FAIL;
  if (xSemaphoreTake(s_v_nkro_mutex, portMAX_DELAY)) {
    // Retry up to 100 times if reporting fails (endpoint busy)
    for (int i = 0; i < 100; i++) {
      err = kb_send_report(s_v_nkro);
      if (err == ESP_OK) break;
      vTaskDelay(pdMS_TO_TICKS(1)); 
    }
    xSemaphoreGive(s_v_nkro_mutex);
  }
  return err;
}

void kb_macro_force_clear(void) {
  if (xSemaphoreTake(s_v_nkro_mutex, portMAX_DELAY)) {
    memset(s_v_nkro, 0, sizeof(s_v_nkro));
    xSemaphoreGive(s_v_nkro_mutex);
    kb_macro_send_report();
  }
}

// Execute a macro's events, checking cancel_requested for cancellable modes.
// Returns true if completed normally, false if cancelled.
static bool execute_macro_cancellable(uint16_t macro_id, uint8_t depth,
                                      bool is_cancellable,
                                      macro_rt_state_t *rt) {
  if (depth > 5) {
    ESP_LOGW(TAG, "Macro recursion depth exceeded (limit 5)");
    return true;
  }

  const cfg_macro_t *m = NULL;
  for (size_t i = 0; i < s_macros.count; i++) {
    if (s_macros.macros[i].id == macro_id) {
      m = &s_macros.macros[i];
      break;
    }
  }

  if (!m) {
    ESP_LOGW(TAG, "Macro ID %d not found in storage", macro_id);
    return true;
  }

  ESP_LOGD(TAG, "Executing macro %d (depth %d): %s", m->id, (int)depth,
           m->name);

  for (size_t i = 0; i < m->event_count; i++) {
    // Check cancel between each step for cancellable modes
    if (is_cancellable && rt && rt->cancel_requested) {
      ESP_LOGD(TAG, "Macro %d cancelled mid-execution", macro_id);
      kb_macro_force_clear();
      return false;
    }

    uint16_t val = m->events[i].value;
    switch (m->events[i].type) {
    case MACRO_EVT_KEY_PRESS:
      if (val >= ACTION_CODE_MACRO_MIN && val <= ACTION_CODE_MACRO_MAX) {
        if (!execute_macro_cancellable(val - ACTION_CODE_MACRO_MIN, depth + 1,
                                       is_cancellable, rt))
          return false;
      } else {
        kb_macro_process_action(val, true);
        kb_macro_send_report();
      }
      break;
    case MACRO_EVT_KEY_RELEASE:
      if (val >= ACTION_CODE_MACRO_MIN && val <= ACTION_CODE_MACRO_MAX) {
        // Release on nested macro is ignored
      } else {
        kb_macro_process_action(val, false);
        kb_macro_send_report();
      }
      break;
    case MACRO_EVT_DELAY_MS:
      // For cancellable modes, split long delays into small chunks
      if (is_cancellable && rt) {
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
    case MACRO_EVT_KEY_TAP:
      if (val >= ACTION_CODE_MACRO_MIN && val <= ACTION_CODE_MACRO_MAX) {
        if (!execute_macro_cancellable(val - ACTION_CODE_MACRO_MIN, depth + 1,
                                       is_cancellable, rt))
          return false;
      } else {
        kb_macro_process_action(val, true);
        kb_macro_send_report();
        vTaskDelay(pdMS_TO_TICKS(10));
        kb_macro_process_action(val, false);
        kb_macro_send_report();
      }
      break;
    default:
      break;
    }
  }
  return true;
}

// Find a macro's config and its runtime state index
static const cfg_macro_t *find_macro(uint16_t macro_id, size_t *out_idx) {
  for (size_t i = 0; i < s_macros.count; i++) {
    if (s_macros.macros[i].id == macro_id) {
      if (out_idx) *out_idx = i;
      return &s_macros.macros[i];
    }
  }
  return NULL;
}

static void macro_task(void *arg) {
  macro_queue_item_t item;
  while (1) {
    if (xQueueReceive(s_macro_queue, &item, portMAX_DELAY)) {
      if (!item.is_pressed) continue; // Release is handled inline below

      size_t idx = 0;
      const cfg_macro_t *m = find_macro(item.macro_id, &idx);
      if (!m) continue;

      macro_rt_state_t *rt = &s_rt_state[idx];
      uint8_t mode = m->exec_mode;
      bool is_cancellable = (mode == MACRO_EXEC_HOLD_REPEAT_CANCEL ||
                             mode == MACRO_EXEC_TOGGLE_REPEAT_CANCEL);

      rt->is_running = true;
      rt->cancel_requested = false;

      switch (mode) {
      case MACRO_EXEC_ONCE_STACK_ONCE:
      case MACRO_EXEC_ONCE_NO_STACK:
      case MACRO_EXEC_ONCE_STACK_N:
        // Execute once, then drain pending
        execute_macro_cancellable(item.macro_id, 0, false, rt);
        while (rt->pending_count > 0) {
          rt->pending_count--;
          execute_macro_cancellable(item.macro_id, 0, false, rt);
        }
        break;

      case MACRO_EXEC_HOLD_REPEAT:
      case MACRO_EXEC_HOLD_REPEAT_CANCEL:
        // Loop while key is held
        while (rt->key_held) {
          if (!execute_macro_cancellable(item.macro_id, 0, is_cancellable, rt))
            break;
          if (!rt->key_held) break; // Released during execution
          vTaskDelay(pdMS_TO_TICKS(5)); // Small gap between iterations
        }
        break;

      case MACRO_EXEC_TOGGLE_REPEAT:
      case MACRO_EXEC_TOGGLE_REPEAT_CANCEL:
        // Loop while toggle is active
        while (rt->toggle_active) {
          if (!execute_macro_cancellable(item.macro_id, 0, is_cancellable, rt))
            break;
          if (!rt->toggle_active) break; // Toggled off during execution
          vTaskDelay(pdMS_TO_TICKS(5));
        }
        break;

      case MACRO_EXEC_BURST_N:
        // Execute repeat_count times
        for (uint8_t i = 0; i < rt->burst_remaining; i++) {
          execute_macro_cancellable(item.macro_id, 0, false, rt);
          if (i + 1 < rt->burst_remaining) {
            vTaskDelay(pdMS_TO_TICKS(5));
          }
        }
        rt->burst_remaining = 0;
        break;

      default:
        execute_macro_cancellable(item.macro_id, 0, false, rt);
        break;
      }

      rt->is_running = false;
      rt->cancel_requested = false;
    }
  }
}

// Reload macros from storage
static void on_macros_updated(const char *key) {
    ESP_LOGI(TAG, "Reloading macros from storage...");
    cfgmod_get_config(CFGMOD_KIND_MACRO, "macros", &s_macros);
    // Reset all runtime state on reload
    memset(s_rt_state, 0, sizeof(s_rt_state));
}

// Forward definitions from cfg_macros.c are now in cfg_macros.h

void kb_macro_init(void) {
  memset(s_v_nkro, 0, sizeof(s_v_nkro));
  memset(s_rt_state, 0, sizeof(s_rt_state));
  s_active_layer = KB_LAYER_BASE;
  s_v_nkro_mutex = xSemaphoreCreateMutex();
  s_macro_queue = xQueueCreate(32, sizeof(macro_queue_item_t)); // Reverted to 32 to prevent massive "trailing" execution 
  
  // Re-register macro handler with our update callback
  cfgmod_register_kind(CFGMOD_KIND_MACRO, macros_default, macros_deserialize,
                       macros_serialize, on_macros_updated, sizeof(cfg_macro_list_t));

  // Load initial macros
  cfgmod_get_config(CFGMOD_KIND_MACRO, "macros", &s_macros);
  ESP_LOGI(TAG, "Loaded %d macros from storage", (int)s_macros.count);

  xTaskCreate(macro_task, "kb_macro", 4096, NULL, 4, NULL);
  ESP_LOGI(TAG, "Macro engine initialized");
}

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

static void process_system_action(uint16_t action, bool is_pressed) {
  // 1. Handle Layer keys (stateful)
  if (action == SYS_ACTION_LAYER_FN1) {
    s_is_fn1_held = is_pressed;
    update_layer_state();
    return;
  } else if (action == SYS_ACTION_LAYER_FN2) {
    s_is_fn2_held = is_pressed;
    update_layer_state();
    return;
  }

  // 1.5 Handle Media / Volume Actions (needs to send 0 on release)
  if (action >= SYS_ACTION_VOLUME_UP && action <= MEDIA_ACTION_TOGGLE) {
    uint16_t media_code = 0;
    if (is_pressed) {
      switch (action) {
      case MEDIA_ACTION_TOGGLE:
        media_code = HID_USAGE_CONSUMER_PLAY_PAUSE; // 0x00CD
        break;
      case MEDIA_ACTION_NEXT:
        media_code = HID_USAGE_CONSUMER_SCAN_NEXT_TRACK; // 0x00B5
        break;
      case MEDIA_ACTION_PREV:
        media_code = HID_USAGE_CONSUMER_SCAN_PREVIOUS_TRACK; // 0x00B6
        break;
      case SYS_ACTION_VOLUME_UP:
        media_code = HID_USAGE_CONSUMER_VOLUME_INCREMENT; // 0x00E9
        break;
      case SYS_ACTION_VOLUME_DOWN:
        media_code = HID_USAGE_CONSUMER_VOLUME_DECREMENT; // 0x00EA
        break;
      case SYS_ACTION_MUTE:
        media_code = HID_USAGE_CONSUMER_MUTE; // 0x00E2
        break;
      default:
        break;
      }
    }
    kb_send_consumer_report(media_code);
    return;
  }

  // 2. Handle Action Triggers (usually on press)
  if (is_pressed) {
    switch (action) {
    // BLE Actions
    case SYS_ACTION_BLE_TOGGLE:
    case SYS_ACTION_BLE_ON:
    case SYS_ACTION_BLE_OFF:
    case SYS_ACTION_BLE_1:
    case SYS_ACTION_BLE_2:
    case SYS_ACTION_BLE_3:
      ESP_LOGI(TAG, "BLE action %04x not yet implemented", action);
      // TODO: call blemod functions
      break;


    // Brightness Actions
    case SYS_ACTION_BRIGHTNESS_UP:
    case SYS_ACTION_BRIGHTNESS_DOWN:
      ESP_LOGI(TAG, "Brightness action %04x not yet implemented", action);
      break;

    // RGB Actions
    case SYS_ACTION_RGB_MODE_NEXT:
    case SYS_ACTION_RGB_MODE_PREV:
    case SYS_ACTION_RGB_SPEED_NEXT:
    case SYS_ACTION_RGB_SPEED_PREV:
    case SYS_ACTION_RGB_BRIGHTNESS_UP:
    case SYS_ACTION_RGB_BRIGHTNESS_DOWN:
      ESP_LOGI(TAG, "RGB action %04x not yet implemented", action);
      // TODO: Call rgb_module functions
      break;

    default:
      ESP_LOGI(TAG, "Unknown system action %04x", action);
      break;
    }
  }
}

void kb_macro_process_action(uint16_t action_code, bool is_pressed) {
  if (action_code >= ACTION_CODE_HID_MIN &&
      action_code <= ACTION_CODE_HID_MAX) {
    // Standard HID key
    if (is_pressed) {
      kb_macro_virtual_press((uint8_t)action_code);
    } else {
      kb_macro_virtual_release((uint8_t)action_code);
    }
  } else if (action_code >= ACTION_CODE_SYSTEM_MIN &&
             action_code <= ACTION_CODE_SYSTEM_MAX) {
    // System / Layer changes
    process_system_action(action_code, is_pressed);
  } else if (action_code >= ACTION_CODE_MACRO_MIN &&
             action_code <= ACTION_CODE_MACRO_MAX) {
    // Multi-step custom macro — mode-aware press/release handling
    uint16_t macro_id = action_code - ACTION_CODE_MACRO_MIN;
    
    size_t idx = 0;
    const cfg_macro_t *m = find_macro(macro_id, &idx);
    if (!m) return;
    
    macro_rt_state_t *rt = &s_rt_state[idx];
    uint8_t mode = m->exec_mode;
    
    if (is_pressed) {
      // ── KEY PRESS ──
      switch (mode) {
      case MACRO_EXEC_ONCE_STACK_ONCE:
        if (rt->is_running) {
          if (rt->pending_count < 1) rt->pending_count = 1;
        } else {
          rt->pending_count = 0;
          macro_queue_item_t item = {.macro_id = macro_id, .is_pressed = true};
          xQueueSend(s_macro_queue, &item, 0);
        }
        break;
        
      case MACRO_EXEC_ONCE_NO_STACK:
        if (!rt->is_running) {
          macro_queue_item_t item = {.macro_id = macro_id, .is_pressed = true};
          xQueueSend(s_macro_queue, &item, 0);
        }
        break;
        
      case MACRO_EXEC_ONCE_STACK_N:
        if (rt->is_running) {
          uint8_t max_pending = m->stack_max > 0 ? m->stack_max : 1;
          if (rt->pending_count < max_pending) rt->pending_count++;
        } else {
          rt->pending_count = 0;
          macro_queue_item_t item = {.macro_id = macro_id, .is_pressed = true};
          xQueueSend(s_macro_queue, &item, 0);
        }
        break;
        
      case MACRO_EXEC_HOLD_REPEAT:
      case MACRO_EXEC_HOLD_REPEAT_CANCEL:
        rt->key_held = true;
        rt->cancel_requested = false;
        if (!rt->is_running) {
          macro_queue_item_t item = {.macro_id = macro_id, .is_pressed = true};
          xQueueSend(s_macro_queue, &item, 0);
        }
        break;
        
      case MACRO_EXEC_TOGGLE_REPEAT:
      case MACRO_EXEC_TOGGLE_REPEAT_CANCEL:
        if (rt->toggle_active) {
          // Toggle OFF
          rt->toggle_active = false;
          if (mode == MACRO_EXEC_TOGGLE_REPEAT_CANCEL) {
            rt->cancel_requested = true;
          }
        } else {
          // Toggle ON
          rt->toggle_active = true;
          rt->cancel_requested = false;
          if (!rt->is_running) {
            macro_queue_item_t item = {.macro_id = macro_id, .is_pressed = true};
            xQueueSend(s_macro_queue, &item, 0);
          }
        }
        break;
        
      case MACRO_EXEC_BURST_N: {
        if (!rt->is_running) {
          rt->burst_remaining = m->repeat_count > 0 ? m->repeat_count : 1;
          macro_queue_item_t item = {.macro_id = macro_id, .is_pressed = true};
          xQueueSend(s_macro_queue, &item, 0);
        }
        break;
      }
      
      default: {
        macro_queue_item_t item = {.macro_id = macro_id, .is_pressed = true};
        xQueueSend(s_macro_queue, &item, 0);
        break;
      }
      }
    } else {
      // ── KEY RELEASE ──
      switch (mode) {
      case MACRO_EXEC_HOLD_REPEAT:
        rt->key_held = false;
        break;
      case MACRO_EXEC_HOLD_REPEAT_CANCEL:
        rt->key_held = false;
        rt->cancel_requested = true;
        break;
      default:
        // Other modes don't react to release
        break;
      }
    }
  }
}
