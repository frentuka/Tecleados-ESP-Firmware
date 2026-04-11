#include "statusmod.h"
#include "cfg_ble.h"
#include "cfgmod.h"
#include "usbmod.h"
#include "usb_send.h"
#include "usb_callbacks_tx.h"
#include "event_bus.h"
#include "splitmod.h"
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
    uint8_t  split_state;           // split_state_t
    uint8_t  split_role;            // split_role_t
} s_cache = {
    .transport_mode    = 0xFF,  // Invalid — forces first push
    .selected_profile  = 0xFF,
    .pairing_profile   = 0xFF,
    .connected_bitmap  = 0xFFFF,
    .split_state       = (uint8_t)SPLIT_STATE_DISABLED,
    .split_role        = (uint8_t)SPLIT_ROLE_NONE,
};

/* =========================================================================
 * Send
 * ========================================================================= */

static bool send_status_push(void) {
    char json_buf[160];
    snprintf(json_buf, sizeof(json_buf),
             "{\"mode\":%d,\"profile\":%d,\"pairing\":%d,\"bitmap\":%u"
             ",\"split_state\":%u,\"split_role\":%u}",
             s_cache.transport_mode,
             s_cache.selected_profile,
             (int8_t)s_cache.pairing_profile,  // cast to show -1 for "none"
             s_cache.connected_bitmap,
             s_cache.split_state,
             s_cache.split_role);

    ESP_LOGI(TAG, "Pushing status: P=%d, Pair=%d, Mode=%d, Bm=%04X",
             s_cache.selected_profile, (int8_t)s_cache.pairing_profile,
             s_cache.transport_mode, s_cache.connected_bitmap);

    uint8_t header[7] = {MODULE_STATUS, 0, 0, 0, 0, 0, 0};
    size_t json_len = strlen(json_buf);
    uint8_t full_resp[7 + 160]; /* 7-byte header + max JSON */
    memcpy(full_resp, header, 7);
    memcpy(full_resp + 7, json_buf, json_len);

    return send_payload(full_resp, (uint16_t)(7 + json_len));
}

/* =========================================================================
 * Event handlers
 * ========================================================================= */

static void status_on_ble_event(void *arg, esp_event_base_t base,
                                int32_t event_id, void *data) {
    // When this device is a split SLAVE, the master's authoritative BLE state
    // arrives via SPLIT_EVENT_BLE_STATUS_UPDATED.  Local BLE events on the slave
    // (e.g. profile disconnects during suspension) are stale and must not
    // overwrite the master-pushed cache.
    if (splitmod_get_role() == SPLIT_ROLE_SLAVE) return;

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
    // Slave receives authoritative BLE state from master via
    // SPLIT_EVENT_BLE_STATUS_UPDATED — local config reads would be stale.
    if (splitmod_get_role() == SPLIT_ROLE_SLAVE) return;

    const config_update_event_t *ev = (const config_update_event_t *)data;
    if (ev->kind == (uint8_t)CFGMOD_KIND_CONNECTION) {
        // Refresh routing and selected_profile from config.
        const cfg_ble_state_t *st = cfg_ble_get_state();
        s_cache.transport_mode   = st->ble_routing_enabled ? 1 : 0;
        s_cache.selected_profile = st->selected_profile;
        send_status_push();
    }
}

static void status_on_split_event(void *arg, esp_event_base_t base,
                                   int32_t event_id, void *data) {
    switch ((split_event_id_t)event_id) {
    case SPLIT_EVENT_CONNECTED: {
        const split_peer_info_t *info = (const split_peer_info_t *)data;
        s_cache.split_state = (uint8_t)SPLIT_STATE_CONNECTED;
        s_cache.split_role  = info->role;
        break;
    }
    case SPLIT_EVENT_DISCONNECTED:
        s_cache.split_state = (uint8_t)SPLIT_STATE_DISCONNECTED;
        s_cache.split_role  = (uint8_t)SPLIT_ROLE_NONE;
        break;
    case SPLIT_EVENT_ROLE_CHANGED:
        s_cache.split_role = *(const uint8_t *)data;
        // The connected-bitmap cached from the previous role is no longer valid:
        //   - new master: was caching the old master's live BLE state via
        //     SPLIT_EVENT_BLE_STATUS_UPDATED; its own BLE just resumed with 0 connections.
        //   - new slave:  BLE_EVENT_PROFILE_DISCONNECTED will also clear this, but
        //     resetting here prevents a brief stale-green flash before that fires.
        s_cache.connected_bitmap = 0;
        break;
    case SPLIT_EVENT_PAIR_STARTED:
        s_cache.split_state = (uint8_t)SPLIT_STATE_PAIRING;
        break;
    case SPLIT_EVENT_PAIR_COMPLETE:
        s_cache.split_state = (uint8_t)SPLIT_STATE_CONNECTING;
        break;
    case SPLIT_EVENT_PAIR_FAILED:
        s_cache.split_state = (uint8_t)SPLIT_STATE_IDLE;
        break;
    case SPLIT_EVENT_BLE_STATUS_UPDATED: {
        // Master pushed its live BLE state — use it so the slave configurator
        // shows the correct mode/profile/bitmap instead of stale local values.
        const split_ble_status_t *bst = (const split_ble_status_t *)data;
        s_cache.transport_mode   = bst->routing_active ? 1 : 0;
        s_cache.selected_profile = bst->selected_profile;
        s_cache.connected_bitmap = bst->connected_bitmap;
        s_cache.pairing_profile  = (bst->pairing_profile < 0)
                                   ? 0xFF : (uint8_t)bst->pairing_profile;
        break;
    }
    case SPLIT_EVENT_STALE:
    case SPLIT_EVENT_STALE_RECOVERED:
        // State hasn't changed — just re-push so configurator sees it
        break;
    default:
        return; // Unknown event — skip push
    }
    send_status_push();
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
    // Seed cache once from current config / module state.
    const cfg_ble_state_t *st = cfg_ble_get_state();
    s_cache.transport_mode   = st->ble_routing_enabled ? 1 : 0;
    s_cache.selected_profile = st->selected_profile;
    s_cache.pairing_profile  = 0xFF;
    s_cache.connected_bitmap = 0;

    split_status_t sst = splitmod_get_status();
    s_cache.split_state = (uint8_t)sst.state;
    s_cache.split_role  = (uint8_t)sst.role;

    // Subscribe to BLE events (all IDs), config connection updates, and split events.
    esp_event_handler_register(BLE_EVENTS, ESP_EVENT_ANY_ID,
                               status_on_ble_event, NULL);
    esp_event_handler_register(CONFIG_EVENTS, CONFIG_EVENT_KIND_UPDATED,
                               status_on_config_event, NULL);
    esp_event_handler_register(SPLIT_EVENTS, ESP_EVENT_ANY_ID,
                               status_on_split_event, NULL);

    // Register manual-poll USB callback.
    usbmod_register_callback(MODULE_STATUS, status_module_callback);

    ESP_LOGI(TAG, "Status module initialized (event-driven)");
}
