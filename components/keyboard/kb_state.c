#include "kb_state.h"
#include "esp_log.h"

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
             led_status, (led_status & KB_LED_CAPS_LOCK) ? 1 : 0,
             (led_status & KB_LED_NUM_LOCK) ? 1 : 0);

    // TODO: In the future, this is where we would trigger actual physical LEDs
    // rgb_module_set_caps_lock_led((led_status & KB_LED_CAPS_LOCK) != 0);
  }
}

uint8_t kb_state_get_leds(void) { return s_led_status; }
