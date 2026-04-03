#include "include/splitmod.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_event.h"
#if CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif

#include "event_bus.h"
#include "kb_layout.h"
#include "kb_system_action.h"
#include "kb_manager.h"
#include "usbmod.h"
#include "usb_send.h"
#include "battery.h"

#include "split_transport.h"
#include "split_protocol.h"
#include "split_crypto.h"
#include "split_pair.h"
#include "split_role.h"
#include "split_sync.h"
#include "split_config_sync.h"

#define TAG "SPLIT"

/* =========================================================================
 * Timing constants
 * ========================================================================= */

#define SPLIT_HEARTBEAT_MS      200   // Slave → Master keepalive interval
#define SPLIT_STALE_MS          500   // Silence this long → link stale (warn)
#define SPLIT_DISCONNECT_MS    2000   // Silence this long → peer disconnected
#define SPLIT_DISCOVERY_MS      500   // Interval between DISCOVERY broadcasts
#define SPLIT_ROLE_NEG_MS       500   // Retransmit ROLE_NEGOTIATE at this rate
#define SPLIT_RECONNECT_MS_MIN  500   // Initial reconnect backoff
#define SPLIT_RECONNECT_MS_MAX 5000   // Maximum reconnect backoff

/* =========================================================================
 * State
 * ========================================================================= */

static split_state_t  s_state  = SPLIT_STATE_DISABLED;
static split_role_t   s_role   = SPLIT_ROLE_NONE;
static uint8_t        s_own_mac[6]  = {0};
static uint8_t        s_peer_mac[6] = {0};
static int8_t         s_peer_rssi   = 0;
static uint16_t       s_latency_us  = 0;
static uint16_t       s_tx_seq      = 0;

static uint8_t        s_session_key[SPLIT_CRYPTO_KEY_SIZE] = {0};
static bool           s_session_key_set = false;

// Anti-replay: last seen sequence number from the peer (reset on connect/disconnect)
static uint16_t       s_peer_seq_last   = 0;
static bool           s_peer_seq_valid  = false;

// Task-local timing (only accessed from split_task)
static TickType_t     s_peer_last_seen     = 0;
static TickType_t     s_last_discovery_tx  = 0;
static TickType_t     s_last_role_neg_tx   = 0;
static TickType_t     s_last_reconnect_at  = 0;
static TickType_t     s_last_heartbeat_tx  = 0;
static uint32_t       s_reconnect_interval = SPLIT_RECONNECT_MS_MIN;

static bool           s_link_stale     = false;

static TaskHandle_t   s_task_handle = NULL;

/* =========================================================================
 * Power Management helpers
 *
 * MASTER / standalone: full performance (no sleep).
 * SLAVE idle:          reduced min-freq + light sleep between FreeRTOS ticks.
 * ========================================================================= */

#if CONFIG_PM_ENABLE
static void pm_apply_active(void)
{
    esp_pm_config_t cfg = {
        .max_freq_mhz      = 240,
        .min_freq_mhz      = 240,
        .light_sleep_enable = false,
    };
    esp_err_t ret = esp_pm_configure(&cfg);
    if (ret != ESP_OK) ESP_LOGW(TAG, "pm_active: %s", esp_err_to_name(ret));
}

static void pm_apply_slave_idle(void)
{
    esp_pm_config_t cfg = {
        .max_freq_mhz      = 240,
        .min_freq_mhz      = 80,   // modem stays at 80 MHz min for ESP-NOW
        .light_sleep_enable = true, // FreeRTOS tickless idle enters light sleep
    };
    esp_err_t ret = esp_pm_configure(&cfg);
    if (ret != ESP_OK) ESP_LOGW(TAG, "pm_slave_idle: %s", esp_err_to_name(ret));
}
#endif  // CONFIG_PM_ENABLE

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static void send_role_negotiate(void)
{
    split_pair_data_t pd;
    uint8_t pref = split_pair_get_data(&pd) ? pd.preferred_role : 0;

    split_role_payload_t rp = {.proposed_role = pref};
    memcpy(rp.device_id, s_own_mac, 6);

    split_transport_send(s_peer_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_ROLE_NEGOTIATE,
                         s_tx_seq++, (const uint8_t *)&rp, sizeof(rp));
}

static void on_disconnect(const char *reason)
{
    ESP_LOGW(TAG, "disconnected: %s", reason);
    s_state = SPLIT_STATE_DISCONNECTED;
    s_reconnect_interval = SPLIT_RECONNECT_MS_MIN;
    s_last_reconnect_at  = xTaskGetTickCount();
    s_role = SPLIT_ROLE_NONE;
    split_sync_clear_remote_matrix();
    kb_manager_set_remote_matrix(NULL);
    kb_manager_set_matrix_cb(NULL);   // stop sending deltas (was slave)
    kb_manager_set_paused(false);     // restore report generation
    split_config_sync_reset();        // discard any in-progress reassembly
    s_peer_seq_valid = false;         // reset anti-replay window
    s_link_stale     = false;
    kb_manager_set_scan_divisor(1);   // restore full scan rate

#if CONFIG_PM_ENABLE
    pm_apply_active();  // restore full performance when running standalone
#endif

    uint8_t reason_byte = 0;
    esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_DISCONNECTED,
                   &reason_byte, sizeof(reason_byte), 0);
}

/* =========================================================================
 * Matrix-change callback — registered when SLAVE, sends deltas to master
 * ========================================================================= */

static uint8_t s_prev_matrix[SPLIT_MATRIX_BYTES];
static uint8_t s_prev_layer = 0;

static void on_matrix_change(const uint8_t *matrix, size_t len, uint8_t layer)
{
    if (s_state != SPLIT_STATE_CONNECTED || s_role != SPLIT_ROLE_SLAVE) return;

    // Wake to full performance when a key is pressed; relax when idle.
#if CONFIG_PM_ENABLE
    bool any_pressed = false;
    for (size_t i = 0; i < (len < SPLIT_MATRIX_BYTES ? len : SPLIT_MATRIX_BYTES); i++) {
        if (matrix[i]) { any_pressed = true; break; }
    }
    if (any_pressed) pm_apply_active();
    // else             pm_apply_slave_idle();
#endif

    bool layer_changed = (layer != s_prev_layer);

    if (layer_changed) {
        // Layer change: send full state so master has accurate context
        split_sync_send_full_state(s_peer_mac, matrix, layer, &s_tx_seq);
    } else {
        split_sync_send_delta(s_peer_mac, s_prev_matrix, matrix, layer, &s_tx_seq);
    }

    memcpy(s_prev_matrix, matrix, SPLIT_MATRIX_BYTES < len ? SPLIT_MATRIX_BYTES : len);
    s_prev_layer = layer;
}

/* =========================================================================
 * System action event handler (KB_EVENT_SYSTEM_ACTION)
 * ========================================================================= */

static void on_config_updated(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)arg; (void)base; (void)event_id;
    if (s_state != SPLIT_STATE_CONNECTED || s_role != SPLIT_ROLE_MASTER) return;

    const config_update_event_t *ev = (const config_update_event_t *)event_data;

    // Only push kinds that are in the sync table
    for (size_t i = 0; i < SPLIT_SYNC_ENTRY_COUNT; i++) {
        if (SPLIT_SYNC_ENTRIES[i].kind == (cfgmod_kind_t)ev->kind &&
            strncmp(SPLIT_SYNC_ENTRIES[i].key, ev->key, CFGMOD_MAX_KEY_LEN) == 0) {
            split_config_sync_push(s_peer_mac, &s_tx_seq,
                                   (cfgmod_kind_t)ev->kind, ev->key);
            break;
        }
    }
}

static void on_kb_system_action(void *arg, esp_event_base_t base,
                                 int32_t event_id, void *event_data)
{
    (void)arg; (void)base; (void)event_id;
    const kb_sys_action_event_t *ev = (const kb_sys_action_event_t *)event_data;

    // Only act on single-tap (one press + release = deliberate action)
    if (ev->event != KB_EV_SINGLE_TAP) return;

    switch (ev->action_code) {
    case SYS_ACTION_SPLIT_PAIR:
        if (s_state == SPLIT_STATE_PAIRING) {
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
 * Protocol receive callback (runs in WiFi task context)
 * ========================================================================= */

static void on_split_message(const uint8_t *src_mac,
                              uint8_t type, uint16_t seq,
                              const uint8_t *payload, size_t len,
                              const uint8_t *mic)
{
    ESP_LOGD(TAG, "rx 0x%02X seq=%u from " MACSTR " (%u B)",
             type, seq, MAC2STR(src_mac), (unsigned)len);

    // Anti-replay: reject any packet whose seq is at or behind the last seen value
    // once the link is established. Pairing messages exempt (no session yet).
    bool is_pairing_msg = (type == SPLIT_MSG_DISCOVERY   ||
                           type == SPLIT_MSG_PAIR_REQUEST ||
                           type == SPLIT_MSG_PAIR_RESPONSE);
    if (!is_pairing_msg && s_peer_seq_valid) {
        // Allow a 16-bit wraparound window of up to 32768 (half the range)
        int16_t delta = (int16_t)(seq - s_peer_seq_last);
        if (delta <= 0) {
            ESP_LOGD(TAG, "dropped replay seq=%u (last=%u)", seq, s_peer_seq_last);
            return;
        }
    }
    if (!is_pairing_msg) {
        s_peer_seq_last  = seq;
        s_peer_seq_valid = true;
    }

    // Clear stale flag on any traffic from our connected peer
    if (s_link_stale && s_state == SPLIT_STATE_CONNECTED &&
        memcmp(src_mac, s_peer_mac, 6) == 0) {
        s_link_stale = false;
        ESP_LOGI(TAG, "link recovered after stale");
        esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_STALE_RECOVERED, NULL, 0, 0);
    }

    switch ((split_msg_type_t)type) {

    /* ---- Pairing ---- */

    case SPLIT_MSG_DISCOVERY:
        if (s_state == SPLIT_STATE_PAIRING) {
            esp_err_t ret = split_pair_on_discovery(src_mac, payload, len, s_own_mac);
            if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "discovery handler: %s", esp_err_to_name(ret));
            }
        }
        break;

    case SPLIT_MSG_PAIR_REQUEST:
        if (s_state == SPLIT_STATE_PAIRING) {
            esp_err_t ret = split_pair_on_pair_request(src_mac, payload, len, s_own_mac);
            if (ret == ESP_OK) {
                split_pair_data_t pd;
                split_pair_get_data(&pd);
                memcpy(s_peer_mac, pd.peer_mac, 6);
                memcpy(s_session_key, pd.shared_key, SPLIT_CRYPTO_KEY_SIZE);
                s_session_key_set = true;
                s_state = SPLIT_STATE_CONNECTING;

                split_peer_info_t info = {.role = (uint8_t)s_role};
                memcpy(info.mac, s_peer_mac, 6);
                esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_PAIR_COMPLETE,
                               &info, sizeof(info), 0);

                send_role_negotiate();
                s_last_role_neg_tx = xTaskGetTickCount();
            } else if (ret != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "pair_request handler: %s", esp_err_to_name(ret));
            }
        }
        break;

    case SPLIT_MSG_PAIR_RESPONSE:
        if (s_state == SPLIT_STATE_PAIRING) {
            esp_err_t ret = split_pair_on_pair_response(src_mac, payload, len);
            if (ret == ESP_OK) {
                split_pair_data_t pd;
                split_pair_get_data(&pd);
                memcpy(s_peer_mac, pd.peer_mac, 6);
                memcpy(s_session_key, pd.shared_key, SPLIT_CRYPTO_KEY_SIZE);
                s_session_key_set = true;
                s_state = SPLIT_STATE_CONNECTING;

                split_peer_info_t info = {.role = (uint8_t)s_role};
                memcpy(info.mac, s_peer_mac, 6);
                esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_PAIR_COMPLETE,
                               &info, sizeof(info), 0);

                send_role_negotiate();
                s_last_role_neg_tx = xTaskGetTickCount();
            } else {
                ESP_LOGW(TAG, "pair_response handler: %s", esp_err_to_name(ret));
            }
        }
        break;

    /* ---- Role negotiation ---- */

    case SPLIT_MSG_ROLE_NEGOTIATE:
        if (s_state == SPLIT_STATE_CONNECTING) {
            split_pair_data_t pd;
            uint8_t own_pref = split_pair_get_data(&pd) ? pd.preferred_role : 0;

            split_role_t decided = SPLIT_ROLE_NONE;
            esp_err_t ret = split_role_on_negotiate(src_mac, payload, len,
                                                     s_own_mac, own_pref, &decided);
            if (ret == ESP_OK && decided != SPLIT_ROLE_NONE) {
                s_role = decided;
                s_state = SPLIT_STATE_CONNECTED;
                s_peer_last_seen = xTaskGetTickCount();
                s_reconnect_interval = SPLIT_RECONNECT_MS_MIN;

                split_peer_info_t info = {.role = (uint8_t)s_role};
                memcpy(info.mac, s_peer_mac, 6);
                esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_CONNECTED, &info, sizeof(info), 0);
                esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_ROLE_CHANGED,
                               &s_role, sizeof(s_role), 0);

                ESP_LOGI(TAG, "CONNECTED as %s",
                         s_role == SPLIT_ROLE_MASTER ? "MASTER" : "SLAVE");

                if (s_role == SPLIT_ROLE_SLAVE) {
                    kb_manager_set_remote_matrix(NULL);
                    kb_manager_set_paused(true);  // slave produces no HID reports
                    // Register matrix-change callback so slave sends deltas to master
                    memset(s_prev_matrix, 0, sizeof(s_prev_matrix));
                    s_prev_layer = 0;
                    kb_manager_set_matrix_cb(on_matrix_change);
                    // Trigger an immediate full-state send
                    uint8_t zero[SPLIT_MATRIX_BYTES] = {0};
                    split_sync_send_full_state(s_peer_mac, zero, 0, &s_tx_seq);
                    // Matrix is zero → start in idle power-save mode
// #if CONFIG_PM_ENABLE
//                     pm_apply_slave_idle();
// #endif
                } else {
                    // Master: restore report generation, disable slave callback
                    kb_manager_set_matrix_cb(NULL);
                    kb_manager_set_paused(false);
#if CONFIG_PM_ENABLE
                    pm_apply_active();
#endif
                    // Remote matrix starts zeroed until slave reports its state
                    uint8_t zero[SPLIT_MATRIX_BYTES] = {0};
                    kb_manager_set_remote_matrix(zero);
                    // Kick off config sync: push our config to the newly-connected slave
                    split_config_sync_push_all(s_peer_mac, &s_tx_seq);
                }
            }
        } else if (s_state == SPLIT_STATE_CONNECTED) {
            // Peer re-sent ROLE_NEGOTIATE (missed our transition); re-confirm
            send_role_negotiate();
        }
        break;

    case SPLIT_MSG_ROLE_SWAP_REQ:
        if (s_state == SPLIT_STATE_CONNECTED) {
            split_role_t new_role;
            split_role_on_swap_req(s_role, &new_role);

            split_role_payload_t ack = {.proposed_role = (uint8_t)new_role};
            memcpy(ack.device_id, s_own_mac, 6);
            split_transport_send(s_peer_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_ROLE_SWAP_ACK,
                                 s_tx_seq++, (const uint8_t *)&ack, sizeof(ack));

            s_role = new_role;
            esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_ROLE_CHANGED,
                           &s_role, sizeof(s_role), 0);
            ESP_LOGI(TAG, "role swap: now %s",
                     s_role == SPLIT_ROLE_MASTER ? "MASTER" : "SLAVE");
        }
        break;

    case SPLIT_MSG_ROLE_SWAP_ACK:
        if (s_state == SPLIT_STATE_CONNECTED) {
            split_role_t new_role;
            split_role_on_swap_ack(s_role, &new_role);
            s_role = new_role;
            esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_ROLE_CHANGED,
                           &s_role, sizeof(s_role), 0);
            ESP_LOGI(TAG, "role swap ACK: now %s",
                     s_role == SPLIT_ROLE_MASTER ? "MASTER" : "SLAVE");
        }
        break;

    /* ---- Key state (MASTER receives from SLAVE) ---- */

    case SPLIT_MSG_KEY_STATE_FULL:
        if (s_state == SPLIT_STATE_CONNECTED && s_role == SPLIT_ROLE_MASTER) {
            split_sync_on_key_state_full(payload, len);
            if (split_sync_remote_matrix_changed()) {
                uint8_t rm[SPLIT_MATRIX_BYTES];
                split_sync_get_remote_matrix(rm);
                kb_manager_set_remote_matrix(rm);
            }
        }
        break;

    case SPLIT_MSG_KEY_STATE_DELTA:
        if (s_state == SPLIT_STATE_CONNECTED && s_role == SPLIT_ROLE_MASTER) {
            split_sync_on_key_state_delta(payload, len);
            if (split_sync_remote_matrix_changed()) {
                uint8_t rm[SPLIT_MATRIX_BYTES];
                split_sync_get_remote_matrix(rm);
                kb_manager_set_remote_matrix(rm);
            }
        }
        break;

    /* ---- Heartbeat (Slave → Master, Master responds) ---- */

    case SPLIT_MSG_HEARTBEAT:
        s_peer_last_seen = xTaskGetTickCount();
        if (len >= sizeof(split_heartbeat_payload_t)) {
            const split_heartbeat_payload_t *hb =
                (const split_heartbeat_payload_t *)payload;
            s_peer_rssi = hb->rssi;

            if (s_state == SPLIT_STATE_CONNECTED && s_role == SPLIT_ROLE_MASTER) {
                // Echo sent_us back so SLAVE can compute RTT
                split_heartbeat_payload_t resp = {
                    .state       = (uint8_t)s_state,
                    .role        = (uint8_t)s_role,
                    .battery_pct = 0xFF,
                    .rssi        = s_peer_rssi,
                    .sent_us     = hb->sent_us,
                };
                split_transport_send(s_peer_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_HEARTBEAT,
                                     s_tx_seq++, (const uint8_t *)&resp, sizeof(resp));
            } else if (s_state == SPLIT_STATE_CONNECTED && s_role == SPLIT_ROLE_SLAVE) {
                // Measure RTT from the echoed timestamp
                if (hb->sent_us != 0) {
                    uint32_t rtt_us = (uint32_t)esp_timer_get_time() - hb->sent_us;
                    uint32_t half   = rtt_us / 2;
                    s_latency_us = (uint16_t)(half > 65535u ? 65535u : half);
                    ESP_LOGD(TAG, "RTT=%lu us  latency=%u us",
                             (unsigned long)rtt_us, s_latency_us);
                }
            }
        }
        break;

    case SPLIT_MSG_DISCONNECT:
        if (s_state == SPLIT_STATE_CONNECTED || s_state == SPLIT_STATE_CONNECTING) {
            on_disconnect("graceful peer DISCONNECT");
        }
        break;

    case SPLIT_MSG_CONFIG_SYNC:
        // SLAVE: receive fragmented config blob from master
        split_config_sync_on_fragment(src_mac, payload, len, s_peer_mac, &s_tx_seq);
        break;

    case SPLIT_MSG_CONFIG_SYNC_ACK:
        split_config_sync_on_ack(payload, len);
        break;

    default:
        ESP_LOGD(TAG, "unhandled msg type 0x%02X", type);
        break;
    }
}

/* =========================================================================
 * Main task — 100 Hz state machine / housekeeping
 * ========================================================================= */

static void split_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "split task started");

    const TickType_t tick_period = pdMS_TO_TICKS(10);

    for (;;) {
        vTaskDelay(tick_period);
        TickType_t now = xTaskGetTickCount();

        switch (s_state) {

        case SPLIT_STATE_PAIRING:
            if ((now - s_last_discovery_tx) >= pdMS_TO_TICKS(SPLIT_DISCOVERY_MS)) {
                split_pair_data_t pd;
                uint8_t pref = split_pair_get_data(&pd) ? pd.preferred_role : 0;
                uint8_t disc_buf[sizeof(split_discovery_payload_t)];
                size_t  disc_len = split_pair_build_discovery(disc_buf, sizeof(disc_buf),
                                                              s_own_mac, pref);
                if (disc_len > 0) {
                    static const uint8_t bcast[] = SPLIT_BROADCAST_MAC;
                    split_transport_send(bcast, SPLIT_PROTO_SPLIT,
                                         SPLIT_MSG_DISCOVERY, s_tx_seq++,
                                         disc_buf, disc_len);
                }
                s_last_discovery_tx = now;
            }
            break;

        case SPLIT_STATE_CONNECTING:
            if ((now - s_last_role_neg_tx) >= pdMS_TO_TICKS(SPLIT_ROLE_NEG_MS)) {
                if (split_pair_is_paired()) {
                    send_role_negotiate();
                }
                s_last_role_neg_tx = now;
            }
            break;

        case SPLIT_STATE_CONNECTED: {
            TickType_t since_hb = now - s_peer_last_seen;
            if (since_hb >= pdMS_TO_TICKS(SPLIT_DISCONNECT_MS)) {
                on_disconnect("heartbeat timeout");
                break;
            }
            if (since_hb >= pdMS_TO_TICKS(SPLIT_STALE_MS) && !s_link_stale) {
                s_link_stale = true;
                ESP_LOGW(TAG, "link stale — no heartbeat for >%u ms", SPLIT_STALE_MS);
                esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_STALE, NULL, 0, 0);
            }
            // Slave sends heartbeat; master responds inside on_split_message
            if (s_role == SPLIT_ROLE_SLAVE) {
                if ((now - s_last_heartbeat_tx) >= pdMS_TO_TICKS(SPLIT_HEARTBEAT_MS)) {
                    uint8_t bat_pct = battery_get_level_pct();
                    // Battery-aware scan rate: halve at <30%, quarter at <10%
                    if (bat_pct != 0xFF) {
                        if      (bat_pct < 10) kb_manager_set_scan_divisor(4);
                        else if (bat_pct < 30) kb_manager_set_scan_divisor(2);
                        else                   kb_manager_set_scan_divisor(1);
                    }
                    split_heartbeat_payload_t hb = {
                        .state       = (uint8_t)s_state,
                        .role        = (uint8_t)s_role,
                        .battery_pct = bat_pct,
                        .rssi        = s_peer_rssi,
                        .sent_us     = (uint32_t)esp_timer_get_time(),
                    };
                    split_transport_send(s_peer_mac, SPLIT_PROTO_SPLIT,
                                         SPLIT_MSG_HEARTBEAT, s_tx_seq++,
                                         (const uint8_t *)&hb, sizeof(hb));
                    s_last_heartbeat_tx = now;
                }
            }
            break;
        }

        case SPLIT_STATE_DISCONNECTED:
            if ((now - s_last_reconnect_at) >= pdMS_TO_TICKS(s_reconnect_interval)) {
                if (split_pair_is_paired()) {
                    ESP_LOGI(TAG, "reconnect attempt (backoff=%lu ms)",
                             (unsigned long)s_reconnect_interval);
                    s_state = SPLIT_STATE_CONNECTING;
                    s_last_role_neg_tx = 0; // trigger immediate ROLE_NEGOTIATE
                }
                s_last_reconnect_at = now;
                if (s_reconnect_interval < SPLIT_RECONNECT_MS_MAX) {
                    s_reconnect_interval *= 2;
                    if (s_reconnect_interval > SPLIT_RECONNECT_MS_MAX) {
                        s_reconnect_interval = SPLIT_RECONNECT_MS_MAX;
                    }
                }
            }
            break;

        default:
            break;
        }
    }
}

/* =========================================================================
 * USB configurator callback — receives split commands from the host tool
 *
 * Command byte layout (first byte of payload):
 *   0x01  START_PAIRING  [optional 4-byte LE timeout_ms]
 *   0x02  CANCEL_PAIRING
 *   0x03  UNPAIR
 *   0x04  GET_STATUS     (triggers a status module push)
 * ========================================================================= */

#define SPLIT_USB_CMD_START_PAIRING     0x01
#define SPLIT_USB_CMD_CANCEL_PAIRING    0x02
#define SPLIT_USB_CMD_UNPAIR            0x03
#define SPLIT_USB_CMD_GET_STATUS        0x04
#define SPLIT_USB_CMD_GET_REMOTE_MATRIX 0x05

static bool split_usb_callback(uint8_t *data, uint16_t data_len)
{
    if (!data || data_len == 0) return false;

    uint8_t cmd = data[0];
    switch (cmd) {
    case SPLIT_USB_CMD_START_PAIRING: {
        uint32_t timeout_ms = 0;
        if (data_len >= 5) {
            timeout_ms = (uint32_t)data[1]
                       | ((uint32_t)data[2] << 8)
                       | ((uint32_t)data[3] << 16)
                       | ((uint32_t)data[4] << 24);
        }
        splitmod_start_pairing(timeout_ms);
        break;
    }
    case SPLIT_USB_CMD_CANCEL_PAIRING:
        splitmod_cancel_pairing();
        break;
    case SPLIT_USB_CMD_UNPAIR:
        splitmod_unpair();
        break;
    case SPLIT_USB_CMD_GET_STATUS:
        // No-op — configurator polls status via MODULE_STATUS.
        break;
    case SPLIT_USB_CMD_GET_REMOTE_MATRIX: {
        // Return the current remote matrix as a JSON byte array.
        // Response format: [MODULE_SPLIT, 0, 0, ...json_payload]
        uint8_t rm[SPLIT_MATRIX_BYTES];
        split_sync_get_remote_matrix(rm);

        // Build JSON array "[b0,b1,...,b13]"
        char json[SPLIT_MATRIX_BYTES * 4 + 4]; // max "255," per byte
        int pos = 0;
        json[pos++] = '[';
        for (int i = 0; i < (int)SPLIT_MATRIX_BYTES; i++) {
            pos += snprintf(json + pos, sizeof(json) - (size_t)pos,
                            "%u%s", rm[i],
                            i < (int)SPLIT_MATRIX_BYTES - 1 ? "," : "");
        }
        json[pos++] = ']';
        json[pos]   = '\0';

        // Pack as a USB response: [MODULE_SPLIT, 0x00(ok), 0x00, ...json]
        size_t json_len = (size_t)pos;
        uint8_t *resp = malloc(3 + json_len);
        if (resp) {
            resp[0] = MODULE_SPLIT;
            resp[1] = 0x00; // status OK
            resp[2] = 0x00;
            memcpy(resp + 3, json, json_len);
            send_payload(resp, (uint16_t)(3 + json_len));
            free(resp);
        }
        break;
    }
    default:
        ESP_LOGW(TAG, "unknown USB split command 0x%02X", cmd);
        return false;
    }
    return true;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

esp_err_t splitmod_init(void)
{
    esp_read_mac(s_own_mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "own MAC: " MACSTR, MAC2STR(s_own_mac));

    esp_err_t ret = split_transport_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "transport init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = split_transport_register_protocol(SPLIT_PROTO_SPLIT, on_split_message);
    if (ret != ESP_OK) return ret;

    split_pair_init();

    split_pair_data_t pd;
    if (split_pair_get_data(&pd)) {
        memcpy(s_peer_mac, pd.peer_mac, 6);
        memcpy(s_session_key, pd.shared_key, SPLIT_CRYPTO_KEY_SIZE);
        s_session_key_set = true;
        split_transport_add_peer(pd.peer_mac, pd.channel);
        s_state = SPLIT_STATE_CONNECTING;
        ESP_LOGI(TAG, "paired — reconnecting to " MACSTR, MAC2STR(pd.peer_mac));
    } else {
        s_state = SPLIT_STATE_IDLE;
        ESP_LOGI(TAG, "no pairing data — idle");
    }

    BaseType_t xret = xTaskCreateWithCaps(
        split_task, "split", 6144, NULL, 5, &s_task_handle,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (xret != pdPASS) {
        ESP_LOGE(TAG, "failed to create split task");
        return ESP_FAIL;
    }

    // Subscribe to system action events (SPLIT_PAIR, SPLIT_ROLE_SWAP, SPLIT_DISCONNECT)
    esp_event_handler_register(KB_EVENTS, KB_EVENT_SYSTEM_ACTION,
                               on_kb_system_action, NULL);

    // Subscribe to config updates so MASTER can push changes to SLAVE in real-time
    esp_event_handler_register(CONFIG_EVENTS, CONFIG_EVENT_KIND_UPDATED,
                               on_config_updated, NULL);

    // Register USB configurator callback for pairing/unpairing commands
    usbmod_register_callback(MODULE_SPLIT, split_usb_callback);

    ESP_LOGI(TAG, "splitmod initialised");
    return ESP_OK;
}

esp_err_t splitmod_deinit(void)
{
    if (s_state == SPLIT_STATE_CONNECTED) {
        split_transport_send(s_peer_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_DISCONNECT,
                             s_tx_seq++, NULL, 0);
    }

    if (s_task_handle) {
        vTaskDelete(s_task_handle);
        s_task_handle = NULL;
    }

    split_transport_deinit();
    kb_manager_set_remote_matrix(NULL);

    s_state = SPLIT_STATE_DISABLED;
    s_role  = SPLIT_ROLE_NONE;
    s_session_key_set = false;

    ESP_LOGI(TAG, "splitmod deinitialized");
    return ESP_OK;
}

esp_err_t splitmod_start_pairing(uint32_t timeout_ms)
{
    (void)timeout_ms; // timeout enforcement in a future phase
    if (s_state == SPLIT_STATE_PAIRING) return ESP_OK;

    split_pair_start();
    s_last_discovery_tx = 0; // force immediate first beacon
    s_state = SPLIT_STATE_PAIRING;

    uint8_t zero = 0;
    esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_PAIR_STARTED, &zero, sizeof(zero), 0);
    ESP_LOGI(TAG, "entering pairing mode");
    return ESP_OK;
}

esp_err_t splitmod_cancel_pairing(void)
{
    if (s_state != SPLIT_STATE_PAIRING) return ESP_ERR_INVALID_STATE;
    split_pair_cancel();
    s_state = SPLIT_STATE_IDLE;
    ESP_LOGI(TAG, "pairing cancelled");
    return ESP_OK;
}

esp_err_t splitmod_unpair(void)
{
    if (s_state == SPLIT_STATE_CONNECTED) {
        split_transport_send(s_peer_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_DISCONNECT,
                             s_tx_seq++, NULL, 0);
        split_transport_remove_peer(s_peer_mac);
    }

    split_pair_clear();
    memset(s_peer_mac, 0, 6);
    memset(s_session_key, 0, SPLIT_CRYPTO_KEY_SIZE);
    s_session_key_set = false;
    s_state = SPLIT_STATE_IDLE;
    s_role  = SPLIT_ROLE_NONE;
    s_peer_seq_valid = false;
    split_sync_clear_remote_matrix();
    kb_manager_set_remote_matrix(NULL);
    split_config_sync_reset();

    ESP_LOGI(TAG, "unpaired");
    return ESP_OK;
}

esp_err_t splitmod_request_role_swap(void)
{
    if (s_state != SPLIT_STATE_CONNECTED) return ESP_ERR_INVALID_STATE;

    split_role_payload_t req = {
        .proposed_role = (s_role == SPLIT_ROLE_MASTER)
                         ? SPLIT_ROLE_SLAVE : SPLIT_ROLE_MASTER,
    };
    memcpy(req.device_id, s_own_mac, 6);

    return split_transport_send(s_peer_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_ROLE_SWAP_REQ,
                                s_tx_seq++, (const uint8_t *)&req, sizeof(req));
}

split_status_t splitmod_get_status(void)
{
    split_status_t st = {
        .state      = s_state,
        .role       = s_role,
        .rssi       = s_peer_rssi,
        .latency_us = s_latency_us,
    };
    memcpy(st.peer_mac, s_peer_mac, 6);
    return st;
}

bool splitmod_is_enabled(void)  { return s_state != SPLIT_STATE_DISABLED; }
bool splitmod_is_connected(void){ return s_state == SPLIT_STATE_CONNECTED; }
split_role_t splitmod_get_role(void) { return s_role; }
