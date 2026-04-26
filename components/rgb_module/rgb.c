#include <stdio.h>
#include <string.h>

#include "rgb.h"
#include "led_strip.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/portmacro.h"

#include "esp_check.h"
#include "esp_log.h"

#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

#include "event_bus.h"
#include "splitmod.h"

static const char *TAG = "RGB_Light";

// Estado lógico
static bool s_inited = false;
static volatile bool s_on = false;
static volatile RGBColor s_color = {0,0,0};

// When true, the split state owns the LED and caps-lock events are ignored
static volatile bool s_split_override = false;
static uint8_t s_last_kb_led_state = 0;

static led_strip_handle_t s_strip = NULL;

// Worker
typedef enum { CMD_SET_ON, CMD_SET_COLOR, CMD_SYNC, CMD_TEST_BEEP } cmd_type_t;
typedef struct {
    cmd_type_t type;
    union {
        bool on;
        RGBColor color;
    } u;
} rgb_cmd_t;

static QueueHandle_t s_q = NULL;
static TaskHandle_t  s_worker = NULL;

static inline void rgb_post(const rgb_cmd_t *cmd);

static void apply_color_locked(void)
{
    if (!s_inited || s_strip == NULL) return;

    uint8_t r = s_on ? s_color.r : 0;
    uint8_t g = s_on ? s_color.g : 0;
    uint8_t b = s_on ? s_color.b : 0;

    (void)led_strip_set_pixel(s_strip, 0, r, g, b);
    (void)led_strip_refresh(s_strip);
}

static void rgb_sync_to_system_state(void)
{
    split_status_t status = splitmod_get_status();
    
    // Logic: Decide what the LED SHOULD be
    bool target_on = false;
    RGBColor target_color = {0,0,0};
    bool target_override = false;

    if (status.state == SPLIT_STATE_PAIRING) {
        target_override = true;
        target_color = (RGBColor){0, 0, 40}; // Solid blue
        target_on = true;
    } else if (status.state == SPLIT_STATE_CONNECTED) {
        // Master/Slave bridge: check for stale link
        if (splitmod_is_link_stale()) {
             target_override = true;
             target_color = (RGBColor){20, 10, 0}; // Dim yellow
             target_on = true;
        } else {
             target_override = false;
             target_on = (s_last_kb_led_state & KB_LED_BIT_CAPS_LOCK) != 0;
             target_color = (RGBColor){25, 0, 0}; // Red for caps
        }
    } else if (status.state == SPLIT_STATE_DISCONNECTED) {
        target_override = true;
        target_color = (RGBColor){15, 0, 0}; // Dim red
        target_on = true;
    }

    // Force assertion
    s_split_override = target_override;
    s_color = target_color;
    s_on = target_on;
    apply_color_locked();
}

// RGB Worker task
static void rgb_worker_task(void *arg)
{
    rgb_cmd_t cmd;
    for (;;) {
        // Wait up to 2 seconds for a command. If timeout, refresh state anyway.
        if (xQueueReceive(s_q, &cmd, pdMS_TO_TICKS(2000))) {
            switch (cmd.type) {
                case CMD_SET_ON:
                    s_on = cmd.u.on;
                    apply_color_locked();
                    break;
                case CMD_SET_COLOR:
                    s_color = cmd.u.color;
                    if (s_on) apply_color_locked();
                    break;
                case CMD_SYNC:
                    rgb_sync_to_system_state();
                    break;
                case CMD_TEST_BEEP:
                    s_split_override = true;
                    s_color = (RGBColor){0, 40, 0}; // Green for perfect connection test
                    s_on = true;
                    apply_color_locked();
                    vTaskDelay(pdMS_TO_TICKS(150));
                    rgb_sync_to_system_state();
                    break;
            }
        } else {
            // Periodic refresh to prevent drift or stuck LEDs
            rgb_sync_to_system_state();
        }
    }
}

static void rgb_led_state_handler(void *arg, esp_event_base_t base,
                                  int32_t event_id, void *data) {
    s_last_kb_led_state = *(uint8_t *)data;
    rgb_cmd_t cmd = {.type = CMD_SYNC};
    rgb_post(&cmd);
}

static void rgb_split_event_handler(void *arg, esp_event_base_t base,
                                     int32_t event_id, void *data) {
    // Immediate response to event by triggering a sync
    rgb_cmd_t cmd = {.type = CMD_SYNC};
    
    // For some events, we might want to set the transient state immediately 
    // to avoid the 10ms task latency if desired, but CMD_SYNC is very fast.
    
    // Explicit overrides for events to ensure they take effect immediately
    switch ((split_event_id_t)event_id) {
        case SPLIT_EVENT_CONNECTED:
        case SPLIT_EVENT_STALE_RECOVERED:
            s_split_override = false;
            break;
        case SPLIT_EVENT_DISCONNECTED:
        case SPLIT_EVENT_STALE:
        case SPLIT_EVENT_PAIR_STARTED:
            s_split_override = true;
            break;
        case SPLIT_EVENT_TEST_BEEP:
            cmd.type = CMD_TEST_BEEP;
            break;
        default:
            break;
    }

    rgb_post(&cmd);
}

int rgb_init(gpio_num_t data_gpio)
{
    if (s_inited) return 0;

    led_strip_config_t strip_cfg = {
        .strip_gpio_num = data_gpio,
        .max_leds = 1,                  // un solo LED integrado
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB, // WS2812 es GRB
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false
    };

    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10000000,      // 10 MHz estable para WS2812
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = false,
        }
    };

    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip),
                        TAG, "led_strip_new_rmt_device failed");

    ESP_RETURN_ON_ERROR(led_strip_clear(s_strip), TAG, "clear failed"); // apaga

    // Queue + worker propios del módulo
    s_q = xQueueCreate(8, sizeof(rgb_cmd_t));
    ESP_RETURN_ON_FALSE(s_q != NULL, ESP_ERR_NO_MEM, TAG, "queue");

    // Stack generoso para evitar desbordes en refresh
    BaseType_t ok = xTaskCreateWithCaps(
        rgb_worker_task, "rgb_worker",
        4096, NULL, 5, &s_worker, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "worker");

    s_on = false;
    s_color = (RGBColor){40,0,0};
    s_inited = true;

    esp_event_handler_register(KB_EVENTS, KB_EVENT_LED_STATE, rgb_led_state_handler, NULL);
    esp_event_handler_register(SPLIT_EVENTS, ESP_EVENT_ANY_ID, rgb_split_event_handler, NULL);

    ESP_LOGI(TAG, "Init on GPIO %d: OK", (int)data_gpio);
    return 0;
}

// Encolado seguro (desde tarea o ISR)
static inline void rgb_post(const rgb_cmd_t *cmd)
{
    if (!s_inited) return;
#if (configUSE_PORT_OPTIMISED_TASK_SELECTION == 1) || defined(portINLINE) || defined(xPortInIsrContext)
    if (xPortInIsrContext()) {
        BaseType_t hpw = pdFALSE;
        xQueueSendFromISR(s_q, cmd, &hpw);
        if (hpw) portYIELD_FROM_ISR();
        return;
    }
#endif
    (void)xQueueSend(s_q, cmd, 0);
}

void rgb_set(bool state)
{
    rgb_cmd_t cmd = {.type = CMD_SET_ON};
    cmd.u.on = state;
    rgb_post(&cmd);
}

void rgb_toggle()
{
    rgb_cmd_t cmd = {.type = CMD_SET_ON };
    cmd.u.on = !s_on;
    rgb_post(&cmd);
}

void rgb_set_color(RGBColor color)
{
    rgb_cmd_t cmd = {.type = CMD_SET_COLOR};
    cmd.u.color = color;
    rgb_post(&cmd);
}