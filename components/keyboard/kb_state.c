#include "kb_state.h"
#include "esp_log.h"

#include "rgb.h"

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

    bool caps_on = (led_status & KB_LED_CAPS_LOCK) != 0;
    if (caps_on) {
      rgb_set_color((RGBColor){25, 0, 0});
      rgb_set(true);
    } else {
      rgb_set(false);
    }
  }
}

uint8_t kb_state_get_leds(void) { return s_led_status; }
