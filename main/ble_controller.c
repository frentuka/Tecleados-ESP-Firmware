#include "ble_controller.h"

#include "esp_log.h"
#include "kb_system_action.h"
#include "kb_layout.h"
#include "blemod.h"

static const char *TAG = "ble_ctrl";

static void on_kb_sys_action(uint16_t action_code, kb_action_ev_t event) {
    if (action_code == SYS_ACTION_BLE_TOGGLE) {
        if (event == KB_EV_SINGLE_TAP) {
            ESP_LOGI(TAG, "BLE Toggle Pressed");
            ble_hid_set_routing_active(!ble_hid_is_routing_active());
        }
        return;
    }

    if (action_code >= SYS_ACTION_BLE_1 && action_code <= SYS_ACTION_BLE_9) {
        uint8_t profile_id = action_code - SYS_ACTION_BLE_1; // 0 to 8

        switch (event) {
            case KB_EV_HOLD:
                ESP_LOGI(TAG, "BLE: Pairing Mode (P%d)", profile_id);
                ble_hid_profile_pair(profile_id);
                break;
            case KB_EV_SINGLE_TAP:
                ESP_LOGI(TAG, "BLE: Connecting (P%d)", profile_id);
                ble_hid_profile_connect_and_select(profile_id);
                break;
            case KB_EV_DOUBLE_TAP:
                ESP_LOGI(TAG, "BLE: Toggling (P%d)", profile_id);
                ble_hid_profile_toggle_connection(profile_id);
                break;
            default:
                break;
        }
    }
}

void ble_controller_init(void) {
    ESP_LOGI(TAG, "Initializing BLE Application Controller");
    kb_system_action_register_cb(on_kb_sys_action);
}
