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
  bool is_pressed;
} macro_queue_item_t;

// A hardcoded macro for testing since JSON loading isn't fully required yet
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

static void execute_macro_internal(uint16_t macro_id, uint8_t depth) {
  if (depth > 5) {
    ESP_LOGW(TAG, "Macro recursion depth exceeded (limit 5)");
    return;
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
    return;
  }

  ESP_LOGD(TAG, "Executing macro %d (depth %d): %s", m->id, (int)depth, m->name);

  for (size_t i = 0; i < m->event_count; i++) {
    uint16_t val = m->events[i].value;
    switch (m->events[i].type) {
    case MACRO_EVT_KEY_PRESS:
      if (val >= ACTION_CODE_MACRO_MIN && val <= ACTION_CODE_MACRO_MAX) {
        execute_macro_internal(val - ACTION_CODE_MACRO_MIN, depth + 1);
      } else {
        kb_macro_process_action(val, true);
        kb_macro_send_report();
      }
      break;
    case MACRO_EVT_KEY_RELEASE:
      if (val >= ACTION_CODE_MACRO_MIN && val <= ACTION_CODE_MACRO_MAX) {
        // Nested macros are treated as taps if triggered as "Press", 
        // "Release" on a nested macro ID is ignored to prevent double calls.
      } else {
        kb_macro_process_action(val, false);
        kb_macro_send_report();
      }
      break;
    case MACRO_EVT_DELAY_MS:
      vTaskDelay(pdMS_TO_TICKS(val));
      break;
    case MACRO_EVT_KEY_TAP:
      if (val >= ACTION_CODE_MACRO_MIN && val <= ACTION_CODE_MACRO_MAX) {
        execute_macro_internal(val - ACTION_CODE_MACRO_MIN, depth + 1);
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
}

static void macro_task(void *arg) {
  macro_queue_item_t item;
  while (1) {
    if (xQueueReceive(s_macro_queue, &item, portMAX_DELAY)) {
      if (item.is_pressed) {
        execute_macro_internal(item.macro_id, 0);
      }
    }
  }
}

// Reload macros from storage
static void on_macros_updated(const char *key) {
    ESP_LOGI(TAG, "Reloading macros from storage...");
    cfgmod_get_config(CFGMOD_KIND_MACRO, "macros", &s_macros);
}

// Forward definitions from cfg_macros.c are now in cfg_macros.h

void kb_macro_init(void) {
  memset(s_v_nkro, 0, sizeof(s_v_nkro));
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
    // Multi-step custom macro
    macro_queue_item_t item = {.macro_id = action_code - ACTION_CODE_MACRO_MIN,
                               .is_pressed = is_pressed};
    if (xQueueSend(s_macro_queue, &item, 0) != pdTRUE) {
      ESP_LOGW(TAG, "Macro queue full, skipping trigger %d", (int)item.macro_id);
    }
  }
}
