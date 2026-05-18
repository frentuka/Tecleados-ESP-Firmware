#include "split_dispatch.h"

#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "esp_random.h"

#include "event_bus.h"
#include "kb_manager.h"
#include "usbmod.h"
#include "blemod.h"

#include "split_session.h"
#include "split_transport.h"
#include "split_protocol.h"
#include "split_pair.h"
#include "split_role.h"
#include "split_sync.h"
#include "split_config_sync.h"
#include "split_bridge.h"
#include "split_bench.h"
#include "split_task.h"
#include "cfg_ble.h"
#include "tusb.h"

#define TAG "SPLIT_DP"

/* =========================================================================
 * Remote matrix helper — invoked after KEY_STATE_FULL / KEY_STATE_DELTA.
 * ========================================================================= */

static void apply_remote_matrix_if_changed(void)
{
    if (!split_sync_remote_matrix_changed()) return;
    uint8_t rm[SPLIT_MATRIX_BYTES];
    split_sync_get_remote_matrix(rm);
    kb_manager_set_remote_matrix(rm);
}

/* =========================================================================
 * Pairing completion — shared between PAIR_REQUEST and PAIR_RESPONSE handlers.
 * ========================================================================= */

static void on_pairing_complete(void)
{
    split_task_set_pairing_deadline(0);

    split_pair_data_t pd;
    split_pair_get_data(&pd);

    split_session_set_peer_mac(pd.peer_mac);
    split_session_set_stored_key(pd.shared_key);

    // TSK-first architecture: the paired key lives only in the handshake slot.
    // The active session key starts NULL and is replaced by a derived TSK during
    // the first successful ROLE_NEGOTIATE exchange. Setting it directly here would
    // cause a key mismatch immediately after the TSK is activated by both sides.
    split_transport_set_session_key(NULL);
    split_transport_set_handshake_key(pd.shared_key);

    // Hard-reset sequence numbers and auth failures for the fresh session.
    split_session_reset_tx_seq();
    split_session_reset_rx_seq();
    split_session_reset_auth_failures();

    // Force a new salt regardless of the stickiness guard — this is a brand-new
    // pairing, so the old salt (if any) must not carry over.
    split_session_force_local_salt(esp_random());

    split_session_set_state(SPLIT_STATE_CONNECTING);

    // Save the new pairing data to NVS immediately.
    split_pair_save();

    split_peer_info_t info = {.role = (uint8_t)split_session_get_role()};
    memcpy(info.mac, pd.peer_mac, 6);
    esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_PAIR_COMPLETE, &info, sizeof(info), 0);

    split_task_send_role_negotiate();
}


/* =========================================================================
 * Message-specific handlers
 * ========================================================================= */

static void on_role_negotiate(const uint8_t *src_mac, const uint8_t *payload, size_t len);

static void handle_role_negotiate_msg(const uint8_t *src_mac, const uint8_t *payload, size_t len)
{
    if (len < sizeof(split_role_negotiate_payload_t)) return;
    const split_role_negotiate_payload_t *p = (const split_role_negotiate_payload_t *)payload;

    if (split_session_get_state() == SPLIT_STATE_CONNECTED) {
        if (p->random_salt == split_session_get_last_peer_salt()) {
             on_role_negotiate(src_mac, payload, len);
             return;
        }

        // Peer re-sent ROLE_NEGOTIATE with NEW salt; they likely rebooted.
        ESP_LOGI(TAG, "peer reset detected — restarting session");
        split_task_handle_disconnect("remote re-negotiation");
        split_session_set_state(SPLIT_STATE_CONNECTING);
    }

    on_role_negotiate(src_mac, payload, len);
}

static void on_role_negotiate(const uint8_t *src_mac, const uint8_t *payload, size_t len)
{
    const split_role_negotiate_payload_t *p = (const split_role_negotiate_payload_t *)payload;

    if (split_session_get_state() == SPLIT_STATE_CONNECTED && p->random_salt == split_session_get_last_peer_salt()) {
        // Salt matched: link already active but peer missed our switch or sequences drifted.
        // RESET sequences to 0 to allow recovery from MIC failures.
        split_session_reset_tx_seq();
        split_session_reset_rx_seq();
        split_session_reset_auth_failures();
        
        // Respond to ensure the peer (which might have missed our previous response) sees our salt.
        // Now safe because send_role_negotiate is throttled to 500ms.
        split_task_send_role_negotiate();
        return;
    }

    split_role_t decided = SPLIT_ROLE_NONE;
    esp_err_t ret = split_role_on_negotiate(src_mac, payload, len,
                                             split_session_own_mac(),
                                             tud_mounted() ? 1u : 0u,
                                             ble_hid_get_connected_profiles_bitmap(),
                                             cfg_ble_get_state()->has_unsynced_updates,
                                             split_role_load_last(),
                                             &decided);
    
    if (ret != ESP_OK || decided == SPLIT_ROLE_NONE) return;

    // TSK activation: Derive key from local and peer salts
    uint8_t tsk[SPLIT_CRYPTO_KEY_SIZE];
    if (split_crypto_derive_session_key(split_session_stored_key(),
                                        split_session_get_local_salt(),
                                        p->random_salt, tsk) == ESP_OK) {
        
        split_transport_set_session_key(tsk);
        split_session_reset_tx_seq();
        split_session_reset_rx_seq();
        split_session_reset_auth_failures();
        split_session_set_last_peer_salt(p->random_salt);
        split_session_set_grace(1500);

        ESP_LOGI(TAG, "TSK activated (Local Salt: 0x%08lX, Peer Salt: 0x%08lX)",
                 (unsigned long)split_session_get_local_salt(),
                 (unsigned long)p->random_salt);
    }

    split_session_set_role(decided);
    split_role_save_last(decided);
    split_session_set_state(SPLIT_STATE_CONNECTED);
    split_session_mark_peer_seen();
    split_session_mark_connected_now();
    split_task_reset_reconnect_backoff();
    
    // Respond to ensure the peer sees our salt and can also activate TSK.
    // Throttled to 500ms in split_task.c
    split_task_send_role_negotiate();

    split_peer_info_t info = {.role = (uint8_t)decided};
    memcpy(info.mac, split_session_peer_mac(), 6);
    esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_CONNECTED,    &info,    sizeof(info),    0);
    esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_ROLE_CHANGED, &decided, sizeof(decided), 0);

    ESP_LOGI(TAG, "CONNECTED as %s", decided == SPLIT_ROLE_MASTER ? "MASTER" : "SLAVE");

    split_bridge_apply_routing_for_role(decided);
    if (decided == SPLIT_ROLE_MASTER) {
        ble_hid_seed_handover_state(p->ble_connected_bitmap, p->selected_profile);
        split_task_request_config_sync_initial();
        split_bridge_send_ble_status_to_slave();
    }
}

static void handle_role_swap_req(const uint8_t *payload, size_t len)
{
    // ---- F9: Guard against replayed/reordered swap requests during CONNECTING.
    // Without this, a stale ROLE_SWAP_REQ from a previous session arriving
    // during reconnect would flip ROLE_NONE -> MASTER and corrupt routing.
    if (split_session_get_state() != SPLIT_STATE_CONNECTED) return;
    if (len < sizeof(split_role_payload_t)) return;

    const split_role_payload_t *req = (const split_role_payload_t *)payload;
    split_role_t new_role;
    split_role_on_swap_req(split_session_get_role(), &new_role);

    split_role_payload_t ack = {
        .proposed_role = (uint8_t)new_role,
        .ble_connected_bitmap = ble_hid_get_connected_profiles_bitmap(),
        .selected_profile = (int8_t)cfg_ble_get_state()->selected_profile,
    };
    memcpy(ack.device_id, split_session_own_mac(), 6);
    split_transport_send(split_session_peer_mac(), SPLIT_PROTO_SPLIT,
                         SPLIT_MSG_ROLE_SWAP_ACK, split_session_next_seq(),
                         (const uint8_t *)&ack, sizeof(ack));

    if (new_role == SPLIT_ROLE_MASTER) {
        ble_hid_seed_handover_state(req->ble_connected_bitmap, req->selected_profile);
    }

    split_session_set_role(new_role);
    split_role_save_last(new_role);
    esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_ROLE_CHANGED, &new_role, sizeof(new_role), 0);
    ESP_LOGI(TAG, "role swap: now %s", new_role == SPLIT_ROLE_MASTER ? "MASTER" : "SLAVE");

    split_bridge_apply_routing_for_role(new_role);
    if (new_role == SPLIT_ROLE_MASTER) {
        split_bridge_send_ble_status_to_slave();
        // New master must push full config so another swap leaves the peer armed.
        split_task_request_config_sync_initial();
    }
}

static void handle_role_swap_ack(const uint8_t *payload, size_t len)
{
    if (split_session_get_state() != SPLIT_STATE_CONNECTED) return;
    if (len < sizeof(split_role_payload_t)) return;

    const split_role_payload_t *ack = (const split_role_payload_t *)payload;
    split_role_t new_role;
    split_role_on_swap_ack(split_session_get_role(), &new_role);

    if (new_role == SPLIT_ROLE_MASTER) {
        ble_hid_seed_handover_state(ack->ble_connected_bitmap, ack->selected_profile);
    }

    split_session_set_role(new_role);
    split_role_save_last(new_role);
    esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_ROLE_CHANGED, &new_role, sizeof(new_role), 0);
    ESP_LOGI(TAG, "role swap ACK: now %s", new_role == SPLIT_ROLE_MASTER ? "MASTER" : "SLAVE");

    split_bridge_apply_routing_for_role(new_role);
    if (new_role == SPLIT_ROLE_MASTER) split_bridge_send_ble_status_to_slave();
}

static void handle_heartbeat_msg(const uint8_t *payload, size_t len)
{
    if (len < sizeof(split_heartbeat_payload_t)) return;

    const split_heartbeat_payload_t *hb = (const split_heartbeat_payload_t *)payload;
    split_session_set_rssi(hb->rssi);

    if (split_session_get_state() != SPLIT_STATE_CONNECTED) return;

    split_role_t role = split_session_get_role();
    if (role == SPLIT_ROLE_MASTER) {
        // Echo sent_us so the slave can compute RTT = (now - sent_us) / 2.
        split_heartbeat_payload_t resp = {
            .state       = (uint8_t)split_session_get_state(),
            .role        = (uint8_t)role,
            .battery_pct = 0xFF,
            .rssi        = split_session_get_rssi(),
            .sent_us     = hb->sent_us,
        };
        split_transport_send(split_session_peer_mac(), SPLIT_PROTO_SPLIT,
                             SPLIT_MSG_HEARTBEAT, split_session_next_seq(),
                             (const uint8_t *)&resp, sizeof(resp));
    } else if (role == SPLIT_ROLE_SLAVE && hb->sent_us != 0) {
        uint32_t rtt_us = (uint32_t)esp_timer_get_time() - hb->sent_us;
        uint32_t half   = rtt_us / 2;
        split_session_set_latency_us((uint16_t)(half > 65535u ? 65535u : half));
        ESP_LOGD(TAG, "RTT=%lu us  latency=%u us",
                 (unsigned long)rtt_us, split_session_get_latency_us());
    }
}

static void handle_ble_status_msg(const uint8_t *payload, size_t len)
{
    if (split_session_get_state() != SPLIT_STATE_CONNECTED ||
        split_session_get_role()  != SPLIT_ROLE_SLAVE     ||
        len < sizeof(split_ble_status_payload_t)) {
        return;
    }
    const split_ble_status_payload_t *p = (const split_ble_status_payload_t *)payload;
    split_ble_status_t ev = {
        .routing_active   = p->routing_active != 0,
        .selected_profile = p->selected_profile,
        .connected_bitmap = p->connected_bitmap,
        .pairing_profile  = p->pairing_profile,
    };
    esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_BLE_STATUS_UPDATED,
                   &ev, sizeof(ev), 0);
}

/* =========================================================================
 * Anti-replay + stale-recovery gate — shared prelude for every inbound frame.
 *
 * Returns false when the frame should be dropped (replay / out-of-window).
 * ========================================================================= */

static bool gate_incoming_frame(const uint8_t *src_mac, uint8_t type, uint64_t seq)
{
    bool is_pairing_msg = (type == SPLIT_MSG_DISCOVERY   ||
                           type == SPLIT_MSG_PAIR_REQUEST ||
                           type == SPLIT_MSG_PAIR_RESPONSE);

    if (!is_pairing_msg) {
        if (!split_session_check_rx_seq(seq)) {
            ESP_LOGD(TAG, "dropped replay seq=%llu", (unsigned long long)seq);
            return false;
        }

        bool from_peer = (memcmp(src_mac, split_session_peer_mac(), 6) == 0);
        bool connecting = (split_session_get_state() == SPLIT_STATE_CONNECTING &&
                           memcmp(split_session_peer_mac(), "\x00\x00\x00\x00\x00\x00", 6) == 0);

        if (from_peer || (type == SPLIT_MSG_ROLE_NEGOTIATE && connecting)) {
            split_session_mark_peer_seen();
        }
    }


    if (split_session_is_link_stale() &&
        split_session_get_state() == SPLIT_STATE_CONNECTED &&
        memcmp(src_mac, split_session_peer_mac(), 6) == 0) {
        split_session_set_link_stale(false);
        ESP_LOGI(TAG, "link recovered after stale");
        esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_STALE_RECOVERED, NULL, 0, 0);
    }

    return true;
}

/* =========================================================================
 * Public receive callback
 * ========================================================================= */

void split_dispatch_on_message(const uint8_t *src_mac,
                                uint8_t type, uint64_t seq,
                                const uint8_t *payload, size_t len,
                                const uint8_t *mic)
{
    // MIC was authenticated by split_transport before this callback runs.
    (void)mic;
    ESP_LOGD(TAG, "rx 0x%02X seq=%llu from " MACSTR " (%u B)",
             type, (unsigned long long)seq, MAC2STR(src_mac), (unsigned)len);

    if (!gate_incoming_frame(src_mac, type, seq)) return;

    const uint8_t *peer_mac = split_session_peer_mac();

    switch ((split_msg_type_t)type) {

    /* ---- Pairing ---- */
    case SPLIT_MSG_DISCOVERY:
        if (split_session_get_state() == SPLIT_STATE_PAIRING) {
            esp_err_t ret = split_pair_on_discovery(src_mac, payload, len,
                                                     split_session_own_mac());
            if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "discovery handler: %s", esp_err_to_name(ret));
            }
        }
        break;

    case SPLIT_MSG_PAIR_REQUEST:
        if (split_session_get_state() == SPLIT_STATE_PAIRING) {
            esp_err_t ret = split_pair_on_pair_request(src_mac, payload, len,
                                                        split_session_own_mac());
            if (ret == ESP_OK) {
                on_pairing_complete();
            } else if (ret != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "pair_request handler: %s", esp_err_to_name(ret));
            }
        }
        break;

    case SPLIT_MSG_PAIR_RESPONSE:
        if (split_session_get_state() == SPLIT_STATE_PAIRING) {
            esp_err_t ret = split_pair_on_pair_response(src_mac, payload, len);
            if (ret == ESP_OK) {
                on_pairing_complete();
            } else {
                ESP_LOGW(TAG, "pair_response handler: %s", esp_err_to_name(ret));
            }
        }
        break;

    /* ---- Role negotiation ---- */
    case SPLIT_MSG_ROLE_NEGOTIATE:
        handle_role_negotiate_msg(src_mac, payload, len);
        break;

    case SPLIT_MSG_ROLE_SWAP_REQ:
        handle_role_swap_req(payload, len);
        break;

    case SPLIT_MSG_ROLE_SWAP_ACK:
        handle_role_swap_ack(payload, len);
        break;

    /* ---- Key state (MASTER receives from SLAVE) ---- */
    case SPLIT_MSG_KEY_STATE_FULL:
        if (split_session_get_state() == SPLIT_STATE_CONNECTED &&
            split_session_get_role()  == SPLIT_ROLE_MASTER) {
            split_sync_on_key_state_full(payload, len);
            apply_remote_matrix_if_changed();
        }
        break;

    case SPLIT_MSG_KEY_STATE_DELTA:
        if (split_session_get_state() == SPLIT_STATE_CONNECTED &&
            split_session_get_role()  == SPLIT_ROLE_MASTER) {
            split_sync_on_key_state_delta(payload, len);
            apply_remote_matrix_if_changed();
        }
        break;

    /* ---- Heartbeat ---- */
    case SPLIT_MSG_HEARTBEAT:
        handle_heartbeat_msg(payload, len);
        break;

    case SPLIT_MSG_DISCONNECT: {
        split_state_t st = split_session_get_state();
        if (st == SPLIT_STATE_CONNECTED || st == SPLIT_STATE_CONNECTING) {
            split_task_handle_disconnect("graceful peer DISCONNECT");
        }
        break;
    }

    /* ---- Config sync ---- */
    case SPLIT_MSG_CONFIG_SYNC: {
        bool reverse = false;
        split_config_sync_on_fragment(src_mac, payload, len, peer_mac,
                                      split_session_next_seq, &reverse);
        if (reverse) split_task_request_reverse_ble_sync();
        break;
    }

    case SPLIT_MSG_CONFIG_SYNC_ACK:
        split_config_sync_on_ack(payload, len);
        break;

    /* ---- RTT benchmark ---- */
    case SPLIT_MSG_PING:
        if (split_session_get_state() == SPLIT_STATE_CONNECTED &&
            split_session_get_role()  == SPLIT_ROLE_SLAVE) {
            split_bench_handle_ping(peer_mac, payload, len);
        }
        break;

    case SPLIT_MSG_PONG:
        if (split_session_get_state() == SPLIT_STATE_CONNECTED &&
            split_session_get_role()  == SPLIT_ROLE_MASTER) {
            split_bench_handle_pong(payload, len);
        }
        break;

    /* ---- Poll-rate measurement ---- */
    case SPLIT_MSG_POLL_RATE_REQ:
        /* SLAVE: read own kb_manager snapshot and send it back. */
        if (split_session_get_state() == SPLIT_STATE_CONNECTED &&
            split_session_get_role()  == SPLIT_ROLE_SLAVE) {
            kb_poll_rate_snapshot_t snap = {0};
            kb_manager_get_poll_rate(&snap);
            split_poll_rate_payload_t resp = {
                .scan_hz        = snap.scan_hz,
                .floor_scan_hz  = snap.floor_scan_hz,
                .peak_scan_hz   = snap.peak_scan_hz,
                .peak_report_hz = snap.peak_report_hz,
            };
            split_transport_send(peer_mac, SPLIT_PROTO_SPLIT,
                                 SPLIT_MSG_POLL_RATE_RESP, split_session_next_seq(),
                                 (const uint8_t *)&resp, sizeof(resp));
            ESP_LOGD(TAG, "POLL_RATE_REQ: replied scan=%lu Hz peak=%lu Hz",
                     (unsigned long)snap.scan_hz, (unsigned long)snap.peak_report_hz);
        }
        break;

    case SPLIT_MSG_POLL_RATE_RESP:
        /* MASTER: store slave's snapshot in bench state. */
        if (split_session_get_state() == SPLIT_STATE_CONNECTED &&
            split_session_get_role()  == SPLIT_ROLE_MASTER) {
            split_bench_handle_poll_rate_resp(payload, len);
        }
        break;

    case SPLIT_MSG_TEST_BEEP:
        if (split_session_get_state() == SPLIT_STATE_CONNECTED) {
            esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_TEST_BEEP, NULL, 0, 0);
        }
        break;

    /* ---- BLE proxy ---- */
    case SPLIT_MSG_BLE_CMD:
        if (split_session_get_state() == SPLIT_STATE_CONNECTED &&
            split_session_get_role()  == SPLIT_ROLE_MASTER     &&
            len >= sizeof(split_ble_cmd_payload_t)) {
            const split_ble_cmd_payload_t *p = (const split_ble_cmd_payload_t *)payload;
            ESP_LOGI(TAG, "BLE cmd from slave: 0x%02X arg=%u", p->cmd, p->arg);
            split_bridge_execute_ble_cmd(p->cmd, p->arg);
        }
        break;

    case SPLIT_MSG_BLE_STATUS:
        handle_ble_status_msg(payload, len);
        break;

    default:
        ESP_LOGD(TAG, "unhandled msg type 0x%02X", type);
        break;
    }
}
