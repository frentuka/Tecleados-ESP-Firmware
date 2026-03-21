#include "statusmod.h"
#include "cfg_ble.h"
#include "cfgmod.h"
#include "usbmod.h"
#include "usb_send.h"
#include "usb_callbacks_tx.h"
#include "event_bus.h"
#include "esp_log.h"
#include <string.h>

#define TAG "StatusModule"

/* =========================================================================
 * Cached state — updated by event handlers, read by send_status_push().
 * ========================================================================= */

static struct {
    uint8_t  transport_mode;        // 1 = BLE routing active, 0 = USB
    uint8_t  selected_profile;
    uint8_t  pairing_profile;       // 0xFF = not pairing
    uint16_t connected_bitmap;
} s_cache = {
    .transport_mode    = 0xFF,  // Invalid — forces first push
    .selected_profile  = 0xFF,
    .pairing_profile   = 0xFF,
    .connected_bitmap  = 0xFFFF,
};

/* =========================================================================
 * Send
 * ========================================================================= */

static bool send_status_push(void) {
    char json_buf[128];
    snprintf(json_buf, sizeof(json_buf),
             "{\"mode\":%d,\"profile\":%d,\"pairing\":%d,\"bitmap\":%u}",
             s_cache.transport_mode,
             s_cache.selected_profile,
             (int8_t)s_cache.pairing_profile,  // cast to show -1 for "none"
             s_cache.connected_bitmap);

    ESP_LOGI(TAG, "Pushing status: P=%d, Pair=%d, Mode=%d, Bm=%04X",
             s_cache.selected_profile, (int8_t)s_cache.pairing_profile,
             s_cache.transport_mode, s_cache.connected_bitmap);

    uint8_t header[7] = {MODULE_STATUS, 0, 0, 0, 0, 0, 0};
    size_t json_len = strlen(json_buf);
    uint8_t full_resp[7 + json_len];
    memcpy(full_resp, header, 7);
    memcpy(full_resp + 7, json_buf, json_len);

    return send_payload(full_resp, sizeof(full_resp));
}

/* =========================================================================
 * Event handlers
 * ========================================================================= */

static void status_on_ble_event(void *arg, esp_event_base_t base,
                                int32_t event_id, void *data) {
    switch ((ble_event_id_t)event_id) {
    case BLE_EVENT_PROFILE_CONNECTED: {
        int idx = *(int *)data;
        if (idx >= 0 && idx < 16) {
            s_cache.connected_bitmap |= (uint16_t)(1u << idx);
        }
        break;
    }
    case BLE_EVENT_PROFILE_DISCONNECTED: {
        int idx = *(int *)data;
        if (idx >= 0 && idx < 16) {
            s_cache.connected_bitmap &= (uint16_t)~(1u << idx);
        }
        break;
    }
    case BLE_EVENT_PAIRING_STARTED: {
        int profile = *(int *)data;
        s_cache.pairing_profile = (uint8_t)profile;
        break;
    }
    case BLE_EVENT_PAIRING_COMPLETE: {
        const ble_pairing_result_t *r = (const ble_pairing_result_t *)data;
        s_cache.pairing_profile   = 0xFF;
        s_cache.selected_profile  = (uint8_t)r->profile_idx;
        break;
    }
    case BLE_EVENT_PAIRING_FAILED:
    case BLE_EVENT_PAIRING_TIMEOUT:
        s_cache.pairing_profile = 0xFF;
        break;
    case BLE_EVENT_ROUTING_CHANGED:
        s_cache.transport_mode = *(bool *)data ? 1 : 0;
        break;
    default:
        return; // No state change — skip push.
    }
    send_status_push();
}

static void status_on_config_event(void *arg, esp_event_base_t base,
                                   int32_t event_id, void *data) {
    const config_update_event_t *ev = (const config_update_event_t *)data;
    if (ev->kind == (uint8_t)CFGMOD_KIND_CONNECTION) {
        // Refresh routing and selected_profile from config.
        const cfg_ble_state_t *st = cfg_ble_get_state();
        s_cache.transport_mode   = st->ble_routing_enabled ? 1 : 0;
        s_cache.selected_profile = st->selected_profile;
        send_status_push();
    }
}

/* Manual status request from configurator (USB callback) */
static bool status_module_callback(uint8_t *data, uint16_t data_len) {
    ESP_LOGI(TAG, "Manual status request received");
    send_status_push();
    return true;
}

/* =========================================================================
 * Init
 * ========================================================================= */

void status_module_init(void) {
    // Seed cache once from current config state.
    const cfg_ble_state_t *st = cfg_ble_get_state();
    s_cache.transport_mode   = st->ble_routing_enabled ? 1 : 0;
    s_cache.selected_profile = st->selected_profile;
    s_cache.pairing_profile  = 0xFF;
    s_cache.connected_bitmap = 0;

    // Subscribe to BLE events (all IDs) and config connection updates.
    esp_event_handler_register(BLE_EVENTS, ESP_EVENT_ANY_ID,
                               status_on_ble_event, NULL);
    esp_event_handler_register(CONFIG_EVENTS, CONFIG_EVENT_KIND_UPDATED,
                               status_on_config_event, NULL);

    // Register manual-poll USB callback.
    usbmod_register_callback(MODULE_STATUS, status_module_callback);

    ESP_LOGI(TAG, "Status module initialized (event-driven)");
}
