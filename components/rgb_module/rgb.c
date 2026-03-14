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

static const char *TAG = "RGB_Light";

// Estado lógico
static bool s_inited = false;
static volatile bool s_on = false;
static volatile RGBColor s_color = {0,0,0};

static led_strip_handle_t s_strip = NULL;

// Worker
typedef enum { CMD_SET_ON, CMD_SET_COLOR } cmd_type_t;
typedef struct {
    cmd_type_t type;
    union {
        bool on;
        RGBColor color;
    } u;
} rgb_cmd_t;

static QueueHandle_t s_q = NULL;
static TaskHandle_t  s_worker = NULL;

static void apply_color_locked(void)
{
    if (!s_inited || s_strip == NULL) return;

    uint8_t r = s_on ? s_color.r : 0;
    uint8_t g = s_on ? s_color.g : 0;
    uint8_t b = s_on ? s_color.b : 0;

    (void)led_strip_set_pixel(s_strip, 0, r, g, b);
    (void)led_strip_refresh(s_strip);
}

// RGB Worker task
static void rgb_worker_task(void *arg)
{
    rgb_cmd_t cmd;
    for (;;) {
        if (xQueueReceive(s_q, &cmd, portMAX_DELAY)) {
            switch (cmd.type) {
                case CMD_SET_ON:
                    s_on = cmd.u.on;
                    apply_color_locked();
                    break;
                case CMD_SET_COLOR:
                    s_color = cmd.u.color;
                    if (s_on) apply_color_locked();
                    break;
            }
        }
    }
}

int rgb_init(gpio_num_t data_gpio)
{
    if (s_inited) return 0;

    led_strip_config_t strip_cfg = {
        .strip_gpio_num = data_gpio,
        .max_leds = 1,                  // un solo LED integrado
        .led_pixel_format = LED_PIXEL_FORMAT_GRB, // WS2812 es GRB
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false
    };

    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10000000,      // 10 MHz estable para WS2812
        .mem_block_symbols = 64,
        .flags.with_dma = false,
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