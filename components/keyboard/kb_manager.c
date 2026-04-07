#include <limits.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "kb_manager.h"

#include "kb_layout.h"
#include "kb_macro.h"
#include "kb_matrix.h"
#include "kb_report.h"
#include "kb_state.h"
#include "kb_system_action.h"
#include "kb_custom_key.h"
#include "kb_bitmap.h"

#include "cfg_layouts.h"

#include "class/hid/hid.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "usbmod.h"

static const char *TAG = "kb_manager";

/* ---- Tuning ---- */
static const uint32_t MAX_POLLING_RATE_HZ = 1200;
static const uint32_t MIN_REPORT_RATE_HZ  = 1;   // Minimum Hz for forced periodic reports
#define KB_DEBOUNCE_SCANS 5

/* ---- USB system command codes (received over custom HID channel) ---- */
#define SYS_CMD_INJECT_KEY    0x01
#define SYS_CMD_CLEAR_INJECTED 0x02

/* ---- Injected (test-mode) key matrix ---- */
static uint8_t s_injected_matrix[KB_MATRIX_BITMAP_BYTES];
static portMUX_TYPE s_injected_matrix_lock = portMUX_INITIALIZER_UNLOCKED;

/* ---- Remote half matrix (split keyboard MASTER mode) ---- */
static uint8_t s_remote_matrix[KB_MATRIX_BITMAP_BYTES];
static portMUX_TYPE s_remote_matrix_lock = portMUX_INITIALIZER_UNLOCKED;

/* ---- Matrix-change callback (split keyboard SLAVE mode) ---- */
typedef void (*matrix_cb_t)(const uint8_t *matrix, size_t len, uint8_t layer);
static volatile matrix_cb_t s_matrix_cb = NULL;

/* ---- Pause control ---- */
static volatile bool s_paused = false;

void kb_manager_set_paused(bool paused) { s_paused = paused; }

/* ---- Task handle (used to wake the task from kb_manager_set_remote_matrix) ---- */
static volatile TaskHandle_t s_kb_task_handle = NULL;

/* ---- Scan-rate divisor (battery-aware power saving) ---- */
static volatile uint8_t s_scan_divisor = 1;

void kb_manager_set_scan_divisor(uint8_t divisor)
{
    s_scan_divisor = divisor ? divisor : 1;
}

void kb_manager_set_matrix_cb(void (*cb)(const uint8_t *matrix, size_t len,
                                          uint8_t layer))
{
    s_matrix_cb = cb;
}

void kb_manager_set_remote_matrix(const uint8_t *bitmap)
{
    bool has_keys = false;
    portENTER_CRITICAL(&s_remote_matrix_lock);
    if (bitmap) {
        memcpy(s_remote_matrix, bitmap, KB_MATRIX_BITMAP_BYTES);
        for (size_t i = 0; i < KB_MATRIX_BITMAP_BYTES; i++) {
            if (s_remote_matrix[i]) { has_keys = true; break; }
        }
    } else {
        memset(s_remote_matrix, 0, KB_MATRIX_BITMAP_BYTES);
    }
    portEXIT_CRITICAL(&s_remote_matrix_lock);

    // Wake the kb_manager_task if it is sleeping in ulTaskNotifyTake.
    // Without this, incoming remote keystrokes from the slave are delayed
    // up to 100 ms (the ulTaskNotifyTake timeout) before the master scans
    // and processes them.  The notify is a no-op if the task is already awake.
    if (has_keys) {
        TaskHandle_t h = s_kb_task_handle;
        if (h) xTaskNotifyGive(h);
    }
}

/* ---- Debounce ---- */
static uint8_t s_debounce[KB_MATRIX_KEYS];

static void debounce_update(const uint8_t *raw, uint8_t *stable) {
    for (size_t i = 0; i < KB_MATRIX_KEYS; ++i) {
        bool raw_pressed = kb_bit_get(raw, i);

        if (raw_pressed) {
            if (s_debounce[i] < KB_DEBOUNCE_SCANS) s_debounce[i]++;
        } else {
            if (s_debounce[i] > 0) s_debounce[i]--;
        }

        if (s_debounce[i] == 0) {
            kb_bit_clear(stable, i);
        } else if (s_debounce[i] >= KB_DEBOUNCE_SCANS) {
            kb_bit_set(stable, i);
        }
    }
}

/* ---- USB system callback (test key injection) ---- */
static bool kb_system_usb_callback(uint8_t *data, uint16_t len) {
    if (len < 1) return false;

    uint8_t cmd = data[0];

    if (cmd == SYS_CMD_INJECT_KEY && len >= 4) {
        uint8_t row   = data[1];
        uint8_t col   = data[2];
        uint8_t state = data[3];
        if (row < KB_MATRIX_ROW_COUNT && col < KB_MATRIX_COL_COUNT) {
            size_t bit_index = (size_t)row * KB_MATRIX_COL_COUNT + col;
            portENTER_CRITICAL(&s_injected_matrix_lock);
            if (state) {
                kb_bit_set(s_injected_matrix, bit_index);
            } else {
                kb_bit_clear(s_injected_matrix, bit_index);
            }
            portEXIT_CRITICAL(&s_injected_matrix_lock);
        }
        return true;
    }

    if (cmd == SYS_CMD_CLEAR_INJECTED) {
        portENTER_CRITICAL(&s_injected_matrix_lock);
        memset(s_injected_matrix, 0, sizeof(s_injected_matrix));
        portEXIT_CRITICAL(&s_injected_matrix_lock);
        return true;
    }

    return false;
}

/* ---- Main keyboard task ---- */
static void kb_manager_task(void *arg) {
    (void)arg;

    /* Bitmaps */
    uint8_t s_raw_matrix[KB_MATRIX_BITMAP_BYTES];  // current hardware scan (pre-debounce)
    uint8_t s_matrix[KB_MATRIX_BITMAP_BYTES];      // debounced stable state
    uint8_t s_last_matrix[KB_MATRIX_BITMAP_BYTES]; // last state sent as a report

    /* Per-key action codes: remembered on press so release fires the same code
     * even if the layer changes in the meantime. */
    uint16_t s_active_action_codes[KB_MATRIX_ROW_COUNT][KB_MATRIX_COL_COUNT];

    /* Timing / benchmarking */
    int64_t s_seconds_timer        = esp_timer_get_time();
    int64_t s_last_scan_us         = s_seconds_timer;
    int64_t s_last_report_sent_us  = s_seconds_timer;
    int64_t s_prev_report_sent_us  = -1;
    int64_t s_min_report_interval_us = LLONG_MAX;
    uint32_t s_scan_count          = 0;
    uint32_t s_report_count        = 0;

    /* State flags */
    bool s_last_matrix_valid  = false;
    bool s_last_boot_protocol = false;
    bool s_matrix_nonempty    = false; // cached: true when s_matrix has any pressed keys

    /* Callback prev-state (split SLAVE mode delta tracking) */
    uint8_t s_cb_last[KB_MATRIX_BITMAP_BYTES];
    bool    s_cb_last_valid = false;
    memset(s_cb_last, 0, sizeof(s_cb_last));

    memset(s_matrix, 0, sizeof(s_matrix));
    memset(s_active_action_codes, 0, sizeof(s_active_action_codes));

    kb_matrix_init_isr(xTaskGetCurrentTaskHandle());
    s_kb_task_handle = xTaskGetCurrentTaskHandle(); // expose handle for remote-matrix wakeup

    const int64_t min_scan_interval_us = 1000000LL / (int64_t)MAX_POLLING_RATE_HZ;

    while (1) {
        int64_t now_us    = esp_timer_get_time();
        int64_t elapsed_us = now_us - s_seconds_timer;

        /* Rate-limit scanning to MAX_POLLING_RATE_HZ / s_scan_divisor */
        int64_t effective_interval_us = min_scan_interval_us * (int64_t)s_scan_divisor;
        if (now_us - s_last_scan_us < effective_interval_us) {
            /* If no keys are held, sleep in interrupt mode until a key wakes us.
             * s_matrix_nonempty is updated after every debounce; use the cached value. */
            if (!s_matrix_nonempty && !s_paused) {
                bool injected_empty = true;
                portENTER_CRITICAL(&s_injected_matrix_lock);
                for (size_t i = 0; i < KB_MATRIX_BITMAP_BYTES; i++) {
                    if (s_injected_matrix[i] != 0) { injected_empty = false; break; }
                }
                portEXIT_CRITICAL(&s_injected_matrix_lock);

                if (injected_empty) {
                    kb_matrix_set_interrupts_enabled(true);
                    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
                    kb_matrix_set_interrupts_enabled(false);
                    now_us = esp_timer_get_time();
                } else {
                    /* taskYIELD() would spin-starve IDLE (lower priority) and
                     * trigger the task watchdog.  Block for one tick instead. */
                    vTaskDelay(1);
                    continue;
                }
            } else {
                /* Same issue: yield-spinning starves IDLE1.  Block for one tick
                 * so the WDT can be fed before the next scan window opens. */
                vTaskDelay(1);
                continue;
            }
        }

        /* --- Scan hardware matrix --- */
        kb_matrix_scan(s_raw_matrix);

        /* Merge injected test keys (or clear them if USB is gone) */
        portENTER_CRITICAL(&s_injected_matrix_lock);
        if (tud_suspended() || !tud_ready()) {
            memset(s_injected_matrix, 0, sizeof(s_injected_matrix));
        } else {
            for (size_t i = 0; i < sizeof(s_raw_matrix); i++) {
                s_raw_matrix[i] |= s_injected_matrix[i];
            }
        }
        portEXIT_CRITICAL(&s_injected_matrix_lock);

        debounce_update(s_raw_matrix, s_matrix);

        /* Merge remote half matrix (split keyboard MASTER mode; zero when slave/disconnected) */
        portENTER_CRITICAL(&s_remote_matrix_lock);
        for (size_t i = 0; i < KB_MATRIX_BITMAP_BYTES; i++) {
            s_matrix[i] |= s_remote_matrix[i];
        }
        portEXIT_CRITICAL(&s_remote_matrix_lock);

        s_scan_count++;
        s_last_scan_us = now_us;

        /* Update cached emptiness flag for the rate-limiter sleep decision */
        s_matrix_nonempty = false;
        for (size_t i = 0; i < KB_MATRIX_BITMAP_BYTES; i++) {
            if (s_matrix[i] != 0) { s_matrix_nonempty = true; break; }
        }

        /* --- Periodic stats logging (errors only) --- */
        if (elapsed_us >= 1000000LL) {
            uint32_t scans_per_sec   = (uint32_t)((s_scan_count  * 1000000LL) / elapsed_us);
            uint32_t reports_per_sec = (uint32_t)((s_report_count * 1000000LL) / elapsed_us);
            uint32_t peak_hz = (s_min_report_interval_us > 0 && s_min_report_interval_us != LLONG_MAX)
                               ? (uint32_t)(1000000LL / s_min_report_interval_us) : 0;

            static bool s_logged_boot_protocol = false;
            bool boot_proto_now = usb_keyboard_use_boot_protocol();
            if (boot_proto_now != s_logged_boot_protocol) {
                ESP_LOGI(TAG, "Boot protocol: now %s", boot_proto_now ? "6KRO" : "NKRO");
                s_logged_boot_protocol = boot_proto_now;
            }

            if (reports_per_sec < MIN_REPORT_RATE_HZ / 2) {
                ESP_LOGE(TAG, "Low report rate — scans/s: %lu, reports/s: %lu, peak: %lu Hz",
                         (unsigned long)scans_per_sec,
                         (unsigned long)reports_per_sec,
                         (unsigned long)(peak_hz <= 1000 ? peak_hz : 1000));
            }

            s_scan_count          = 0;
            s_report_count        = 0;
            s_min_report_interval_us = LLONG_MAX;
            s_seconds_timer       = now_us;
        }

        /* --- Matrix-change callback (split SLAVE mode) --- */
        matrix_cb_t mcb = s_matrix_cb;
        if (mcb) {
            bool cb_changed = !s_cb_last_valid ||
                              memcmp(s_matrix, s_cb_last, KB_MATRIX_BITMAP_BYTES) != 0;
            if (cb_changed) {
                mcb(s_matrix, KB_MATRIX_BITMAP_BYTES, kb_macro_get_active_layer());
                memcpy(s_cb_last, s_matrix, KB_MATRIX_BITMAP_BYTES);
                s_cb_last_valid = true;
            }
        } else {
            // Reset tracking so next registration gets a fresh full-state send
            s_cb_last_valid = false;
        }

        /* --- Process matrix edges --- */
        bool boot_protocol = usb_keyboard_use_boot_protocol();
        bool matrix_changed = !s_last_matrix_valid ||
                              memcmp(s_matrix, s_last_matrix, KB_MATRIX_BITMAP_BYTES) != 0;

        if (matrix_changed) {
            /* XOR the bitmaps to find only the changed bits, then use __builtin_ctz
             * to visit only those positions.  For a typical single keypress this
             * iterates 1 bit instead of all KB_MATRIX_KEYS (108). */
            uint8_t diff[KB_MATRIX_BITMAP_BYTES];
            for (size_t i = 0; i < KB_MATRIX_BITMAP_BYTES; i++) {
                diff[i] = s_matrix[i] ^ (s_last_matrix_valid ? s_last_matrix[i] : 0);
            }

            for (size_t byte_idx = 0; byte_idx < KB_MATRIX_BITMAP_BYTES; byte_idx++) {
                uint8_t d = diff[byte_idx];
                while (d) {
                    int bit_pos = __builtin_ctz(d);
                    size_t bit  = byte_idx * 8 + (size_t)bit_pos;

                    if (bit >= KB_MATRIX_KEYS) break; /* ignore bitmap padding bits */

                    uint8_t r   = (uint8_t)(bit / KB_MATRIX_COL_COUNT);
                    uint8_t c   = (uint8_t)(bit % KB_MATRIX_COL_COUNT);
                    bool    curr = kb_bit_get(s_matrix, bit);

                    if (curr) {
                        /* Key down: resolve action on current layer and remember it */
                        uint8_t layer    = kb_macro_get_active_layer();
                        uint16_t action  = kb_layout_get_action_code(r, c, layer);
                        s_active_action_codes[r][c] = action;
                        kb_macro_process_action(action, true);
                    } else {
                        /* Key up: fire release on the same action code as the press */
                        uint16_t action = s_active_action_codes[r][c];
                        kb_macro_process_action(action, false);
                        s_active_action_codes[r][c] = ACTION_CODE_NONE;
                    }

                    d &= (uint8_t)(d - 1); /* clear lowest set bit */
                }
            }
        }

        /* --- Send report when needed --- */
        bool should_send = !s_paused && (
            matrix_changed ||
            (boot_protocol != s_last_boot_protocol) ||
            (now_us > s_last_report_sent_us + 1000000LL / MIN_REPORT_RATE_HZ)
        );

        if (should_send) {
            s_last_report_sent_us = now_us;
            esp_err_t result = kb_macro_send_report();

            if (result == ESP_OK) {
                s_report_count++;
                if (s_prev_report_sent_us >= 0) {
                    int64_t interval = now_us - s_prev_report_sent_us;
                    if (interval > 0 && interval < s_min_report_interval_us) {
                        s_min_report_interval_us = interval;
                    }
                }
                s_prev_report_sent_us = now_us;
            }

            memcpy(s_last_matrix, s_matrix, KB_MATRIX_BITMAP_BYTES);
            s_last_matrix_valid  = true;
            s_last_boot_protocol = boot_protocol;
        }

    }
}

/* ---- Public API ---- */

void kb_manager_start(void) {
    kb_state_init();
    kb_macro_init();
    kb_system_action_init();
    kb_custom_key_init();
    cfg_layout_load_all();

    memset(s_injected_matrix, 0, sizeof(s_injected_matrix));
    memset(s_remote_matrix,   0, sizeof(s_remote_matrix));
    usbmod_register_callback(MODULE_SYSTEM, kb_system_usb_callback);
    kb_matrix_gpio_init();

    vTaskDelay(pdMS_TO_TICKS(500)); // Allow USB/GPIO to settle before scanning

    BaseType_t ret = xTaskCreateWithCaps(
        kb_manager_task, "kb_mgr", 6144, NULL, 5, NULL,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create kb_manager_task: %d", (int)ret);
    }
}

void kb_manager_test_nkro_keypress(uint8_t row, uint8_t col) {
    uint16_t kc = kb_layout_get_action_code(row, col, KB_LAYER_BASE);
    if (kc == ACTION_CODE_NONE || kc >= NKRO_KEYS) return;

    uint8_t nkro[NKRO_BYTES];
    memset(nkro, 0, sizeof(nkro));
    nkro[kc >> 3] |= (uint8_t)(1U << (kc & 7U));
    usb_send_keyboard_nkro(0, nkro, sizeof(nkro));

    vTaskDelay(pdMS_TO_TICKS(20));

    memset(nkro, 0, sizeof(nkro));
    usb_send_keyboard_nkro(0, nkro, sizeof(nkro));
}
