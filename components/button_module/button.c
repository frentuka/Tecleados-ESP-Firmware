#include <stdio.h>

#include "button.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"

#define BOOT_BUTTON GPIO_NUM_0
#define DEBOUNCE_MS 30
#define DOUBLE_PRESS_MS 250

static const char *TAG = "BOOT_BUTTON";
static QueueHandle_t gpio_evt_queue;
static int64_t last_press_timestamp = 0;       // última pulsación aceptada
static bool waiting_for_double_press = false;

static button_callback_t single_press_callback;
static button_callback_t double_press_callback;

// ISR: mínima, solo encola el número de GPIO
static void IRAM_ATTR gpio_isr_handler(void *arg) {
    uint32_t gpio_num = (uint32_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

// Debounce + confirmación de nivel
static bool valid_press(uint32_t io_num) {
    int64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms - last_press_timestamp < DEBOUNCE_MS) return false; // rebote
    vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));                  // estabiliza
    if (gpio_get_level(io_num) == 0) {                       // aún presionado
        last_press_timestamp = now_ms;
        return true;
    }
    return false;
}

//

static void button_task(void *arg) {
    enum { IDLE, WAIT_SECOND } state = IDLE;
    uint32_t io_num;
    int64_t first_ms = 0;

    for (;;) {
        if (state == IDLE) {
            if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
                if (valid_press(io_num)) {
                    state = WAIT_SECOND;
                    first_ms = esp_timer_get_time() / 1000;
                }
            }
        } else { // WAIT_SECOND
            int64_t now_ms = esp_timer_get_time() / 1000;
            int64_t elapsed = now_ms - first_ms;
            int64_t remain_ms = DOUBLE_PRESS_MS - elapsed;
            if (remain_ms <= 0) {
                single_press_callback();
                state = IDLE;
                continue;
            }
            // Espera otro evento hasta agotar la ventana
            if (xQueueReceive(gpio_evt_queue, &io_num, pdMS_TO_TICKS(remain_ms))) {
                if (valid_press(io_num)) {
                    double_press_callback();
                    state = IDLE;
                }
                // si no fue válido, sigue esperando dentro de la misma ventana
            } else {
                single_press_callback(); // timeout sin segundo pulso
                state = IDLE;
            }
        }
    }
}

void button_init(button_callback_t on_single_press, button_callback_t on_double_press)
{
    // Configuración de GPIO como entrada con pull-up e interrupción por flanco de bajada
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Set callbacks
    single_press_callback = on_single_press;
    double_press_callback = on_double_press;

    // Cola y tarea de manejo
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);

    // Servicio ISR y registro del handler
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BOOT_BUTTON, gpio_isr_handler, (void *)BOOT_BUTTON));

    ESP_LOGI(TAG, "Listo para recibir BOOT.");
}