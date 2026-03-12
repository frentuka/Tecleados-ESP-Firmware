#include "statusmod.h"
#include "blemod.h"
#include "cfg_ble.h"
#include "usbmod.h"
#include "usb_send.h"
#include "usb_callbacks_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include <string.h>

#define TAG "StatusModule"

static status_msg_t last_status = {
    .transport_mode = 0xFF,
    .selected_profile = 0xFF,
    .pairing_profile = 0xFF,
    .connected_profiles_bitmap = 0xFFFF
}; // Initializing with invalid values to force first send

static bool send_status_push() {
    const cfg_ble_state_t *ble_st = cfg_ble_get_state();
    uint16_t connected_bitmap = ble_hid_get_connected_profiles_bitmap();
    int pairing_profile = ble_hid_get_pairing_profile();
    
    char json_buf[128];
    snprintf(json_buf, sizeof(json_buf), 
             "{\"mode\":%d,\"profile\":%d,\"pairing\":%d,\"bitmap\":%u}",
             ble_st->ble_routing_enabled ? 1 : 0,
             ble_st->selected_profile,
             pairing_profile,
             connected_bitmap);

    ESP_LOGI(TAG, "Pushing status: P=%d, Pair=%d, Bm=%04X", ble_st->selected_profile, pairing_profile, connected_bitmap);

    // Protocol: [Module][Cmd][Key][Status(4)] + JSON
    uint8_t header[7] = {MODULE_STATUS, 0, 0, 0, 0, 0, 0}; 
    size_t json_len = strlen(json_buf);
    uint8_t full_resp[7 + json_len];
    memcpy(full_resp, header, 7);
    memcpy(full_resp + 7, json_buf, json_len);

    return send_payload(full_resp, sizeof(full_resp));
}

static bool status_module_callback(uint8_t *data, uint16_t data_len) {
    ESP_LOGI(TAG, "Manual status request received");
    send_status_push();
    return true;
}

static void status_task(void *pvParameters) {
    while (1) {
        const cfg_ble_state_t *ble_st = cfg_ble_get_state();
        uint16_t connected_bitmap = ble_hid_get_connected_profiles_bitmap();
        
        int pairing_profile = ble_hid_get_pairing_profile();
        
        bool changed = (last_status.transport_mode != (ble_st->ble_routing_enabled ? 1 : 0)) ||
                       (last_status.selected_profile != ble_st->selected_profile) ||
                       (last_status.pairing_profile != (uint8_t)pairing_profile) ||
                       (last_status.connected_profiles_bitmap != connected_bitmap);

        if (changed) {
            ESP_LOGI(TAG, "Status change detected: P(%d->%d) Pair(%d->%d) Mode(%d->%d) Bm(%04X->%04X)", 
                     last_status.selected_profile, ble_st->selected_profile,
                     (int8_t)last_status.pairing_profile, (int8_t)pairing_profile,
                     last_status.transport_mode, (ble_st->ble_routing_enabled ? 1 : 0),
                     last_status.connected_profiles_bitmap, connected_bitmap);
            if (send_status_push()) {
                last_status.transport_mode = ble_st->ble_routing_enabled ? 1 : 0;
                last_status.selected_profile = ble_st->selected_profile;
                last_status.pairing_profile = (uint8_t)pairing_profile;
                last_status.connected_profiles_bitmap = connected_bitmap;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void status_module_init(void) {
    usbmod_register_callback(MODULE_STATUS, status_module_callback);
    xTaskCreateWithCaps(status_task, "status_task", 4096, NULL, 5, NULL, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "Status module initialized (Push mode enabled)");
}
