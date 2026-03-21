#include "event_bus.h"

#include "esp_event.h"
#include "esp_log.h"

static const char *TAG = "event_bus";

ESP_EVENT_DEFINE_BASE(KB_EVENTS);
ESP_EVENT_DEFINE_BASE(BLE_EVENTS);
ESP_EVENT_DEFINE_BASE(CONFIG_EVENTS);

esp_err_t event_bus_init(void) {
    esp_err_t err = esp_event_loop_create_default();
    if (err == ESP_ERR_INVALID_STATE) {
        // Already created (e.g. by ESP-IDF internals) — not an error.
        ESP_LOGI(TAG, "Default event loop already exists, reusing.");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create default event loop: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Initialized.");
    return ESP_OK;
}
