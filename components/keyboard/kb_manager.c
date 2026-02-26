#include <limits.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "kb_manager.h"

#include "kb_layout.h"
#include "kb_matrix.h"
#include "kb_report.h"
#include "kb_state.h"

#include "class/hid/hid.h" // HID_KEY_* defines
#include "tusb.h"          // for HID protocol enums if needed
#include "usb_descriptors.h"
#include "usbmod.h"

static const char *TAG = "kb_manager";
static const uint32_t MAX_POLLING_RATE = 1200; // def 1000
static const uint32_t MIN_POLLING_RATE = 1;    // def 50
#define KB_DEBOUNCE_SCANS 5

static uint8_t
    s_debounce[KB_MATRIX_KEYS]; // per-key debounce integrator counters

static volatile bool s_paused = false;
void kb_manager_set_paused(bool paused) { s_paused = paused; }

static inline void set_bit(uint8_t *bitmap, size_t bit_index) {
  bitmap[bit_index >> 3] |= (uint8_t)(1U << (bit_index & 7U));
}

static inline void clear_bit(uint8_t *bitmap, size_t bit_index) {
  bitmap[bit_index >> 3] &= (uint8_t)~(1U << (bit_index & 7U));
}

static inline bool get_bit(const uint8_t *bitmap, size_t bit_index) {
  return (bitmap[bit_index >> 3] & (uint8_t)(1U << (bit_index & 7U))) != 0;
}

static void debounce_update(const uint8_t *raw, uint8_t *stable) {
  for (size_t i = 0; i < KB_MATRIX_KEYS; ++i) {
    bool raw_pressed = get_bit(raw, i);

    if (raw_pressed) {
      if (s_debounce[i] < KB_DEBOUNCE_SCANS) {
        s_debounce[i]++;
      }
    } else {
      if (s_debounce[i] > 0) {
        s_debounce[i]--;
      }
    }

    if (s_debounce[i] == 0) {
      clear_bit(stable, i);
    } else if (s_debounce[i] >= KB_DEBOUNCE_SCANS) {
      set_bit(stable, i);
    }
  }
}

static void kb_manager_task(void *arg) {
  (void)arg;

  int64_t s_seconds_timer =
      esp_timer_get_time(); // timestamp of last benchmark tick (microseconds)

  uint32_t s_scan_count = 0; // scans accumulated for per-second benchmark
  uint32_t s_report_count = 0;
  int64_t s_last_report_sent_us =
      s_seconds_timer; // timestamp of last report. used to comply
                       // MIN_POLLING_RATE
  int64_t s_last_scan_us = s_seconds_timer;
  int64_t s_prev_report_sent_us = -1;
  int64_t s_min_report_interval_us = LLONG_MAX;

  bool s_last_matrix_valid = false; // whether s_last_matrix contains valid data
  bool s_last_boot_protocol =
      false; // last USB HID protocol used (boot vs NKRO)
  uint32_t s_idle_yield_counter = 0; // counter for periodic idle yield

  uint8_t s_matrix[KB_MATRIX_BITMAP_BYTES]; // debounced/stable matrix bitmap
                                            // (row-major)
  uint8_t s_raw_matrix[KB_MATRIX_BITMAP_BYTES];  // raw scan bitmap (row-major,
                                                 // no debounce)
  uint8_t s_last_matrix[KB_MATRIX_BITMAP_BYTES]; // last sent debounced matrix
                                                 // bitmap

  while (1) {
    // benchmark
    int64_t now_us = esp_timer_get_time();
    int64_t elapsed_us = now_us - s_seconds_timer;

    // limit scan rate to MAX_POLLING_RATE
    int64_t min_interval_us = 1000000LL / (int64_t)MAX_POLLING_RATE;
    if (now_us - s_last_scan_us < min_interval_us) {
      taskYIELD();
      continue;
    }

    // scanning
    scan(s_raw_matrix);
    debounce_update(s_raw_matrix, s_matrix);
    s_scan_count++;
    s_last_scan_us = now_us;

    // log periodically
    if (elapsed_us >= 1000000) {
      uint32_t scans_per_sec =
          (uint32_t)((s_scan_count * 1000000LL) / elapsed_us);
      uint32_t reports_per_sec =
          (uint32_t)((s_report_count * 1000000LL) / elapsed_us);
      uint32_t closest_reports_per_sec =
          (s_min_report_interval_us == LLONG_MAX ||
           s_min_report_interval_us <= 0)
              ? 0
              : (uint32_t)(1000000LL / s_min_report_interval_us);

      // log boot protocol changes
      if (s_last_boot_protocol != usb_keyboard_use_boot_protocol()) {
        if (!s_last_boot_protocol) {
          ESP_LOGI(TAG, "BOOT PROTOCOL: now 6kro");
        } else {
          ESP_LOGI(TAG, "BOOT PROTOCOL: now nkro");
        }
      }

      // only log problems
      if (reports_per_sec < MIN_POLLING_RATE / 2) {
        ESP_LOGE(TAG, "last second stats:");
        ESP_LOGE(TAG, "matrix scans: %lu, reports: %lu, polling rate: %lu",
                 (unsigned long)scans_per_sec, (unsigned long)reports_per_sec,
                 (unsigned long)closest_reports_per_sec <= 1000
                     ? closest_reports_per_sec
                     : 1000);
      }

      s_scan_count = 0;
      s_report_count = 0;
      s_min_report_interval_us = LLONG_MAX;
      s_seconds_timer = now_us;
    }

    // prevent unnecessary usb reports
    bool boot_protocol = usb_keyboard_use_boot_protocol();
    bool matrix_changed =
        !s_last_matrix_valid ||
        (memcmp(s_matrix, s_last_matrix, KB_MATRIX_BITMAP_BYTES) != 0);

    bool should_send =
        !s_paused &&
        (matrix_changed || (boot_protocol != s_last_boot_protocol) ||
         now_us > (s_last_report_sent_us +
                   1000000LL / MIN_POLLING_RATE)); // ensure min polling rate

    // send report
    if (should_send) {
      s_last_report_sent_us = now_us;
      esp_err_t kb_report_result = kb_send_report(s_matrix);

      // save for logging
      if (kb_report_result == ESP_OK) {
        s_report_count++;
        if (s_prev_report_sent_us >= 0) {
          int64_t interval_us = now_us - s_prev_report_sent_us;
          if (interval_us > 0 && interval_us < s_min_report_interval_us) {
            s_min_report_interval_us = interval_us;
          }
        }
        s_prev_report_sent_us = s_last_report_sent_us;
      }

      memcpy(s_last_matrix, s_matrix, KB_MATRIX_BITMAP_BYTES);
      s_last_matrix_valid = true;
      s_last_boot_protocol = boot_protocol;
    }

    // take 1 tick break every 128 scans (prevents idle task starvation)
    if ((++s_idle_yield_counter & 0x7F) == 0) {
      vTaskDelay(1);
    }
  }
}

void kb_manager_start(void) {
  ESP_LOGI(TAG, "Starting keyboard manager...");
  kb_state_init();
  kb_matrix_gpio_init();
  vTaskDelay(pdMS_TO_TICKS(500));
  xTaskCreatePinnedToCore(kb_manager_task, "kb_mgr", 4096, NULL, 5, NULL, 1);
}

void kb_manager_test_nkro_keypress(uint8_t row, uint8_t col) {
  uint8_t kc = kb_layout_get_keycode(row, col, KB_LAYER_BASE);
  if (kc == HID_KEY_NONE || kc >= NKRO_KEYS) {
    return;
  }

  uint8_t nkro[NKRO_BYTES];
  memset(nkro, 0, sizeof(nkro));
  nkro[kc >> 3] |= (uint8_t)(1U << (kc & 7U));

  usb_send_keyboard_nkro(0, nkro, sizeof(nkro));
  vTaskDelay(pdMS_TO_TICKS(20));

  memset(nkro, 0, sizeof(nkro));
  usb_send_keyboard_nkro(0, nkro, sizeof(nkro));
}