#include "kb_state.h"
#include "esp_log.h"

#include "event_bus.h"

static const char *TAG = "kb_state";
static uint8_t s_led_status = 0;

void kb_state_init(void) {
  s_led_status = 0;
  ESP_LOGI(TAG, "Keyboard state initialized.");
}

void kb_state_update_leds(uint8_t led_status) {
  if (s_led_status != led_status) {
    s_led_status = led_status;
    ESP_LOGI(TAG, "Keyboard LED state updated: %02X (Caps: %d, Num: %d)",
             led_status, (led_status & KB_LED_BIT_CAPS_LOCK) ? 1 : 0,
             (led_status & KB_LED_BIT_NUM_LOCK) ? 1 : 0);

    esp_event_post(KB_EVENTS, KB_EVENT_LED_STATE, &led_status, sizeof(uint8_t), 0);
  }
}

uint8_t kb_state_get_leds(void) { return s_led_status; }
