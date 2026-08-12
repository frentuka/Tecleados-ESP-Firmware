#include "splitmod.h"

#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_random.h"

#include "event_bus.h"

#include "kb_system_action.h"
#include "kb_layout.h"
#include "kb_manager.h"
#include "comm_module.h"

#include "split_session.h"
#include "split_transport.h"
#include "split_protocol.h"
#include "split_pair.h"
#include "split_role.h"
#include "split_sync.h"
#include "blemod.h"
#include "split_config_sync.h"
#include "split_bridge.h"
#include "split_dispatch.h"
#include "split_task.h"
#include "cfg_ble.h"
#include "split_usb.h"

#define TAG "SPLIT"

/* =========================================================================
 * Event handlers — registered in splitmod_init
 * ========================================================================= */

static bool is_syncable_config(cfgmod_kind_t kind, const char *key)
{
    // Specific check for dynamic sub-keys and main indicators (Layouts, Macros and Custom Keys)
    if (kind == CFGMOD_KIND_LAYOUT && (strncmp(key, "ly_", 3) == 0 || strcmp(key, "lay_idx") == 0)) return true;
    if (kind == CFGMOD_KIND_MACRO && (strncmp(key, "mac_", 4) == 0 || strcmp(key, "macros") == 0)) return true;
    if (kind == CFGMOD_KIND_CKEY  && (strncmp(key, "ck_", 3) == 0  || strcmp(key, "ckeys") == 0)) return true;

    for (size_t i = 0; i < SPLIT_SYNC_ENTRY_COUNT; i++) {
        if (SPLIT_SYNC_ENTRIES[i].kind == kind &&
            strncmp(SPLIT_SYNC_ENTRIES[i].key, key, CFGMOD_MAX_KEY_LEN) == 0) {
            return true;
        }
    }
    return false;
}


static void on_config_updated(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)arg; (void)base; (void)event_id;
    if (split_session_get_state() != SPLIT_STATE_CONNECTED) return;

    const config_update_event_t *ev = (const config_update_event_t *)event_data;

    // Selecting an already-connected BLE profile fires CONFIG_EVENT but no
    // BLE_EVENT — push the updated BLE state so the slave widget stays in sync.
    if (ev->kind == (uint8_t)CFGMOD_KIND_CONNECTION) {
        split_bridge_send_ble_status_to_slave();
    }

    if (is_syncable_config((cfgmod_kind_t)ev->kind, ev->key)) {
        // Signal split_task to do the push. Calling split_config_sync_push
        // directly from the event-bus task context would block its receive
        // loop for N×10 ms via vTaskDelay inside the retry path.
        //
        // The sync receive path uses cfgmod_write_storage (not cfgmod_set_config),
        // so it doesn't fire CONFIG_EVENT_KIND_UPDATED — no ping-pong risk.
        split_task_request_config_sync_incremental(ev->kind);
    }
}

static void on_kb_system_action(void *arg, esp_event_base_t base,
                                 int32_t event_id, void *event_data)
{
    (void)arg; (void)base; (void)event_id;
    const kb_sys_action_event_t *ev = (const kb_sys_action_event_t *)event_data;

    if (ev->event != KB_EV_SINGLE_TAP) return;

    switch (ev->action_code) {
    case SYS_ACTION_SPLIT_PAIR:
        if (split_session_get_state() == SPLIT_STATE_PAIRING) {
            splitmod_cancel_pairing();
        } else {
            splitmod_start_pairing(0);
        }
        break;
    case SYS_ACTION_SPLIT_ROLE_SWAP:
        splitmod_request_role_swap();
        break;
    case SYS_ACTION_SPLIT_DISCONNECT:
        splitmod_unpair();
        break;
    default:
        break;
    }
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

esp_err_t splitmod_init(void)
{
    split_session_init();
    split_session_init_epoch();

    esp_err_t ret = split_transport_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "transport init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = split_transport_register_protocol(SPLIT_PROTO_SPLIT,
                                             split_dispatch_on_message);
    if (ret != ESP_OK) return ret;

    split_pair_init();

    split_pair_data_t pd;
    if (split_pair_get_data(&pd)) {
        split_session_set_peer_mac(pd.peer_mac);
        split_session_set_stored_key(pd.shared_key);
        split_transport_add_peer(pd.peer_mac, pd.channel);
        
        // Use Paired key ONLY for handshakes/discovery until TSK is activated
        split_transport_set_session_key(NULL);
        split_transport_set_handshake_key(pd.shared_key);

        split_session_set_local_salt(esp_random());

        split_session_set_state(SPLIT_STATE_CONNECTING);

        ESP_LOGI(TAG, "paired — reconnecting to " MACSTR, MAC2STR(pd.peer_mac));
    } else {

        split_session_set_state(SPLIT_STATE_IDLE);
        ESP_LOGI(TAG, "no pairing data — idle");
    }

    ret = split_task_start();
    if (ret != ESP_OK) return ret;

    esp_event_handler_register(KB_EVENTS,     KB_EVENT_SYSTEM_ACTION,    on_kb_system_action,         NULL);
    esp_event_handler_register(CONFIG_EVENTS, CONFIG_EVENT_KIND_UPDATED, on_config_updated,           NULL);
    esp_event_handler_register(BLE_EVENTS,    ESP_EVENT_ANY_ID,          split_bridge_on_ble_event,   NULL);
    comm_register_module(MODULE_SPLIT, split_usb_callback);
    comm_register_module(MODULE_BLE,   split_bridge_ble_usb_callback);

    ESP_LOGI(TAG, "splitmod initialised");
    return ESP_OK;
}

esp_err_t splitmod_deinit(void)
{
    if (split_session_get_state() == SPLIT_STATE_CONNECTED) {
        split_transport_send(split_session_peer_mac(), SPLIT_PROTO_SPLIT,
                             SPLIT_MSG_DISCONNECT, split_session_next_seq(),
                             NULL, 0);
    }

    split_task_stop();
    split_transport_deinit();
    kb_manager_set_remote_matrix(NULL);

    split_session_set_state(SPLIT_STATE_DISABLED);
    split_session_set_role(SPLIT_ROLE_NONE);
    split_session_set_stored_key(NULL);

    ESP_LOGI(TAG, "splitmod deinitialized");
    return ESP_OK;
}

/* =========================================================================
 * Pairing
 * ========================================================================= */

esp_err_t splitmod_start_pairing(uint32_t timeout_ms)
{
    if (split_session_get_state() == SPLIT_STATE_PAIRING) return ESP_OK;

    split_pair_start();
    split_task_reset_discovery_timer();     // force immediate first beacon
    split_session_set_state(SPLIT_STATE_PAIRING);
    split_task_set_pairing_deadline(
        (timeout_ms > 0) ? (xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms)) : 0);

    // Pairing frames (DISCOVERY, PAIR_REQUEST, PAIR_RESPONSE) are plaintext.
    // Clear any active session key so the transport doesn't try to decrypt them.
    split_transport_set_session_key(NULL);

    esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_PAIR_STARTED, NULL, 0, 0);
    ESP_LOGI(TAG, "entering pairing mode");
    return ESP_OK;
}

esp_err_t splitmod_cancel_pairing(void)
{
    if (split_session_get_state() != SPLIT_STATE_PAIRING) return ESP_ERR_INVALID_STATE;

    split_pair_cancel();
    split_session_set_state(SPLIT_STATE_IDLE);
    split_task_set_pairing_deadline(0);

    esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_PAIR_FAILED, NULL, 0, 0);
    ESP_LOGI(TAG, "pairing cancelled");
    return ESP_OK;
}

esp_err_t splitmod_unpair(void)
{
    if (split_session_get_state() == SPLIT_STATE_CONNECTED) {
        split_transport_send(split_session_peer_mac(), SPLIT_PROTO_SPLIT,
                             SPLIT_MSG_DISCONNECT, split_session_next_seq(),
                             NULL, 0);
        split_transport_remove_peer(split_session_peer_mac());
    }

    split_pair_clear();
    split_session_clear_peer_mac();
    split_session_set_stored_key(NULL);
    split_transport_set_session_key(NULL);
    split_session_set_state(SPLIT_STATE_IDLE);
    split_session_set_role(SPLIT_ROLE_NONE);
    split_session_reset_rx_seq();

    split_sync_clear_remote_matrix();
    kb_manager_set_remote_matrix(NULL);
    split_config_sync_reset();

    ESP_LOGI(TAG, "unpaired");
    return ESP_OK;
}

/* =========================================================================
 * Role
 * ========================================================================= */

esp_err_t splitmod_request_role_swap(void)
{
    if (split_session_get_state() != SPLIT_STATE_CONNECTED) return ESP_ERR_INVALID_STATE;

    split_role_t current = split_session_get_role();
    split_role_payload_t req = {
        .proposed_role = (current == SPLIT_ROLE_MASTER) ? SPLIT_ROLE_SLAVE : SPLIT_ROLE_MASTER,
        .ble_connected_bitmap = ble_hid_get_connected_profiles_bitmap(),
        .selected_profile = (int8_t)cfg_ble_get_state()->selected_profile,
    };
    memcpy(req.device_id, split_session_own_mac(), 6);

    return split_transport_send(split_session_peer_mac(), SPLIT_PROTO_SPLIT,
                                 SPLIT_MSG_ROLE_SWAP_REQ, split_session_next_seq(),
                                 (const uint8_t *)&req, sizeof(req));
}

/* =========================================================================
 * Status
 * ========================================================================= */

split_status_t splitmod_get_status(void)
{
    split_status_t st = {
        .state      = split_session_get_state(),
        .role       = split_session_get_role(),
        .rssi       = split_session_get_rssi(),
        .latency_us = split_session_get_latency_us(),
    };
    memcpy(st.peer_mac, split_session_peer_mac(), 6);
    return st;
}

bool         splitmod_is_enabled(void)   { return split_session_get_state() != SPLIT_STATE_DISABLED; }
bool         splitmod_is_connected(void) { return split_session_get_state() == SPLIT_STATE_CONNECTED; }
bool         splitmod_is_link_stale(void) { return split_session_is_link_stale(); }
split_role_t splitmod_get_role(void)     { return split_session_get_role(); }

/* =========================================================================
 * Testing
 * ========================================================================= */

esp_err_t splitmod_send_test_beep(void)
{
    // 1. Trigger local beep via event bus
    esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_TEST_BEEP, NULL, 0, 0);

    // 2. Send command to peer if connected
    if (split_session_get_state() == SPLIT_STATE_CONNECTED) {
        return split_transport_send(split_session_peer_mac(), SPLIT_PROTO_SPLIT,
                                    SPLIT_MSG_TEST_BEEP, split_session_next_seq(),
                                    NULL, 0);
    }
    
    return ESP_ERR_INVALID_STATE;
}
