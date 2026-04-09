#include "splitmod.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
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
#include "blemod.h"
#include "cfg_ble.h"
#include "cfg_system.h"

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

#define SPLIT_HEARTBEAT_MS       150   // Slave → Master keepalive interval
#define SPLIT_STALE_MS          1500   // Silence this long → link stale (warn)
#define SPLIT_DISCONNECT_MS     5000   // Silence this long → peer disconnected
#define SPLIT_DISCOVERY_MS       500   // Interval between DISCOVERY broadcasts
#define SPLIT_ROLE_NEG_MS        500   // Retransmit ROLE_NEGOTIATE at this rate
#define SPLIT_RECONNECT_MS_MIN   500   // Initial reconnect backoff
#define SPLIT_RECONNECT_MS_MAX  5000   // Maximum reconnect backoff
#define SPLIT_CONFIG_SYNC_SETTLE_MS 300 // Post-connect delay before config sync to avoid radio burst overlap

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
// s_tx_seq is accessed from split_task, kb_manager_task (via matrix_cb), and the WiFi
// task (heartbeat echo). On dual-core ESP32-S3 these run in parallel; all accesses
// must go through next_seq() to prevent sequence-number duplicates.
static portMUX_TYPE   s_seq_mux     = portMUX_INITIALIZER_UNLOCKED;

// Long-term AES-128 key derived from the X25519 exchange during pairing and
// stored in NVS.  Used directly for all post-pairing encrypted communication.
// (Per-session key derivation via nonce exchange is infrastructure-ready in
// split_crypto but requires a two-phase commit protocol not yet implemented.)
static uint8_t        s_stored_key[SPLIT_CRYPTO_KEY_SIZE] = {0};

// Anti-replay: last seen sequence number from the peer (reset on connect/disconnect).
static uint16_t       s_peer_seq_last  = 0;
static bool           s_peer_seq_valid = false;

// Task-local timing (only accessed from split_task).
static TickType_t     s_peer_last_seen    = 0;
static TickType_t     s_last_discovery_tx = 0;
static TickType_t     s_last_role_neg_tx  = 0;
static TickType_t     s_last_reconnect_at = 0;
static TickType_t     s_last_heartbeat_tx = 0;
static uint32_t       s_reconnect_interval = SPLIT_RECONNECT_MS_MIN;

static bool           s_link_stale = false;
static TickType_t     s_pairing_deadline = 0; // 0 = no timeout

static TaskHandle_t   s_task_handle = NULL;

// Config sync is deferred to split_task so neither the WiFi task nor the
// event-bus task is blocked by vTaskDelay calls inside split_config_sync_push.
// s_config_sync_pending   — full push after initial connect (with settle delay).
// s_config_sync_incremental — triggered by on_config_updated; processed immediately.
// s_reverse_ble_sync      — peer sent stale ble_cfg; push our newer ble_cfg + bond back.
static volatile bool  s_config_sync_pending     = false;
static volatile bool  s_config_sync_incremental = false;
static volatile bool  s_reverse_ble_sync        = false;
static TickType_t     s_connected_at            = 0;

/* Forward declarations — defined after message handlers */
static void apply_ble_routing_for_role(split_role_t role);
static void execute_ble_cmd(uint8_t cmd, uint8_t arg);
static void send_ble_status_to_slave(void);

/* =========================================================================
 * RTT benchmark — run once on every SPLIT_STATE_CONNECTED transition
 * (Master only; slave simply echoes PING as PONG)
 * ========================================================================= */

#define BENCH_PROBES      10
#define BENCH_PROBE_MS    50
#define BENCH_SETTLE_MS  1500   // Wait after connect before first probe (must be > config sync duration)
#define BENCH_TIMEOUT_MS (BENCH_PROBE_MS * (BENCH_PROBES + 6))

typedef struct {
    bool      active;
    uint8_t   probes_sent;
    uint8_t   pongs_received;
    uint32_t  rtt_us[BENCH_PROBES];
    TickType_t last_probe_tx;
    TickType_t started_at;
    uint32_t  result_min_us;
    uint32_t  result_avg_us;
    uint32_t  result_max_us;
    uint8_t   result_lost;
} bench_t;

static bench_t s_bench = {0};

static void bench_start(void)
{
    memset(&s_bench, 0, sizeof(s_bench));
    s_bench.active       = true;
    s_bench.started_at   = xTaskGetTickCount();
    s_bench.last_probe_tx = s_bench.started_at - pdMS_TO_TICKS(BENCH_PROBE_MS);
    ESP_LOGI(TAG, "RTT benchmark started (%d probes, %d ms apart)", BENCH_PROBES, BENCH_PROBE_MS);
}

static void bench_finish(void)
{
    s_bench.active = false;

    uint32_t min_us = UINT32_MAX, max_us = 0, sum_us = 0;
    uint8_t  valid  = 0;

    for (int i = 0; i < BENCH_PROBES; i++) {
        if (s_bench.rtt_us[i] == 0) continue;
        if (s_bench.rtt_us[i] < min_us) min_us = s_bench.rtt_us[i];
        if (s_bench.rtt_us[i] > max_us) max_us = s_bench.rtt_us[i];
        sum_us += s_bench.rtt_us[i];
        valid++;
    }

    if (valid == 0) {
        ESP_LOGW(TAG, "RTT benchmark: no pongs received (all %d timed out)", BENCH_PROBES);
        return;
    }

    s_bench.result_min_us = min_us;
    s_bench.result_avg_us = sum_us / valid;
    s_bench.result_max_us = max_us;
    s_bench.result_lost   = BENCH_PROBES - valid;

    ESP_LOGI(TAG, "RTT benchmark (%u/%u): min=%lu us  avg=%lu us  max=%lu us  lost=%u",
             valid, BENCH_PROBES,
             (unsigned long)min_us,
             (unsigned long)s_bench.result_avg_us,
             (unsigned long)max_us,
             s_bench.result_lost);

    if (s_bench.result_lost > 0) {
        ESP_LOGW(TAG, "  %u probe(s) lost — check for radio contention or BLE coexistence",
                 s_bench.result_lost);
    }
}

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
        .max_freq_mhz       = 240,
        .min_freq_mhz       = 240,
        .light_sleep_enable = false,
    };
    esp_err_t ret = esp_pm_configure(&cfg);
    if (ret != ESP_OK) ESP_LOGW(TAG, "pm_active: %s", esp_err_to_name(ret));
}
#endif

/* =========================================================================
 * Sequence number allocator
 *
 * All frames must use next_seq() instead of s_tx_seq++ directly.
 * s_tx_seq is accessed from multiple tasks/contexts concurrently.
 * ========================================================================= */

static uint16_t next_seq(void)
{
    portENTER_CRITICAL(&s_seq_mux);
    uint16_t s = s_tx_seq++;
    portEXIT_CRITICAL(&s_seq_mux);
    return s;
}

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
                         next_seq(), (const uint8_t *)&rp, sizeof(rp));
}

static void on_disconnect(const char *reason)
{
    ESP_LOGW(TAG, "disconnected: %s", reason);
    s_state              = SPLIT_STATE_DISCONNECTED;
    s_reconnect_interval = SPLIT_RECONNECT_MS_MIN;
    s_last_reconnect_at  = xTaskGetTickCount();
    s_role               = SPLIT_ROLE_NONE;
    s_link_stale         = false;
    s_peer_seq_valid     = false;

    split_sync_clear_remote_matrix();
    kb_manager_set_remote_matrix(NULL);
    kb_manager_set_matrix_cb(NULL);
    kb_manager_set_paused(false);
    // Restore BLE routing so either half works standalone after losing the link.
    apply_ble_routing_for_role(SPLIT_ROLE_NONE);
    kb_manager_set_scan_divisor(1);
    split_config_sync_reset();

#if CONFIG_PM_ENABLE
    pm_apply_active();
#endif

    uint8_t reason_byte = 0;
    esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_DISCONNECTED,
                   &reason_byte, sizeof(reason_byte), 0);
}

/* =========================================================================
 * Matrix-change callback — registered when SLAVE, sends state to master
 * ========================================================================= */

static void on_matrix_change(const uint8_t *matrix, size_t len, uint8_t layer)
{
    if (s_state != SPLIT_STATE_CONNECTED || s_role != SPLIT_ROLE_SLAVE) return;

#if CONFIG_PM_ENABLE
    bool any_pressed = false;
    for (size_t i = 0; i < (len < SPLIT_MATRIX_BYTES ? len : SPLIT_MATRIX_BYTES); i++) {
        if (matrix[i]) { any_pressed = true; break; }
    }
    if (any_pressed) pm_apply_active();
#else
    (void)len;
#endif

    // Always send FULL state on every event — never deltas.
    // Rationale: ESP-NOW is fire-and-forget (no ACK). A single dropped delta under
    // BLE/WiFi coexistence permanently corrupts the master's matrix view. The full
    // state packet is 14 bytes — negligible overhead — and lets the master resync
    // on every event without needing a separate recovery path.
    split_sync_send_full_state(s_peer_mac, matrix, layer, next_seq());
}

/* =========================================================================
 * Remote matrix helper — used by KEY_STATE_FULL and KEY_STATE_DELTA handlers
 * ========================================================================= */

static void apply_remote_matrix_if_changed(void)
{
    if (!split_sync_remote_matrix_changed()) return;
    uint8_t rm[SPLIT_MATRIX_BYTES];
    split_sync_get_remote_matrix(rm);
    kb_manager_set_remote_matrix(rm);
}

/* =========================================================================
 * Config update handler
 * ========================================================================= */

static bool is_syncable_config(cfgmod_kind_t kind, const char *key)
{
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
    if (s_state != SPLIT_STATE_CONNECTED) return;

    const config_update_event_t *ev = (const config_update_event_t *)event_data;
    if (is_syncable_config((cfgmod_kind_t)ev->kind, ev->key)) {
        // Signal split_task to do the push.  Calling split_config_sync_push directly
        // from here (event-bus task context) would block the event-bus receive loop
        // for up to N×10 ms due to vTaskDelay inside send_fragment_with_retry.
        // Note: the sync receive path uses cfgmod_write_storage (not cfgmod_set_config),
        // which does not fire CONFIG_EVENT_KIND_UPDATED — so there is no ping-pong risk.
        s_config_sync_incremental = true;
    }
}

/* =========================================================================
 * System action event handler (KB_EVENT_SYSTEM_ACTION)
 * ========================================================================= */

static void on_kb_system_action(void *arg, esp_event_base_t base,
                                 int32_t event_id, void *event_data)
{
    (void)arg; (void)base; (void)event_id;
    const kb_sys_action_event_t *ev = (const kb_sys_action_event_t *)event_data;

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
 * Pairing completion — shared between PAIR_REQUEST and PAIR_RESPONSE handlers
 * ========================================================================= */

static void on_pairing_complete(void)
{
    s_pairing_deadline = 0;

    split_pair_data_t pd;
    split_pair_get_data(&pd);
    memcpy(s_peer_mac,   pd.peer_mac,   6);
    memcpy(s_stored_key, pd.shared_key, SPLIT_CRYPTO_KEY_SIZE);
    split_transport_set_session_key(s_stored_key);

    s_state = SPLIT_STATE_CONNECTING;

    split_peer_info_t info = {.role = (uint8_t)s_role};
    memcpy(info.mac, s_peer_mac, 6);
    esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_PAIR_COMPLETE, &info, sizeof(info), 0);

    send_role_negotiate();
    s_last_role_neg_tx = xTaskGetTickCount();
}

/* =========================================================================
 * BLE routing helper — must be called whenever the local role changes.
 * MASTER activates BLE so it can receive/forward reports to the host.
 * SLAVE deactivates BLE so it doesn't compete with the MASTER.
 * On disconnect (role = NONE) BLE is re-enabled for standalone operation.
 * ========================================================================= */

static void apply_ble_routing_for_role(split_role_t role)
{
    bool should_suspend = (role == SPLIT_ROLE_SLAVE);
    if (ble_hid_is_suspended() != should_suspend) {
        ESP_LOGI(TAG, "BLE routing → %s (role=%u)", should_suspend ? "SUSPENDED" : "RESUMED", (unsigned)role);
        
        if (!should_suspend) {
            // We are becoming MASTER (or standalone): ensure we are using the 
            // most up-to-date synced profiles and bond keys.
            cfg_ble_reload();
            ble_hid_reinit_bonds();
        }

        ble_hid_set_suspended(should_suspend);
    }
}

// Apply keyboard-manager routing for the current role.  Must be called on
// every role transition (initial connect AND role swap) so the matrix callback
// and paused state are always consistent with who is master/slave.
static void apply_kb_routing_for_role(split_role_t role)
{
    if (role == SPLIT_ROLE_SLAVE) {
        kb_manager_set_matrix_cb(on_matrix_change);
        kb_manager_set_remote_matrix(NULL);
        kb_manager_set_paused(true);
        // Send current (zeroed) matrix to new master so it starts with clean state.
        uint8_t zero[SPLIT_MATRIX_BYTES] = {0};
        split_sync_send_full_state(s_peer_mac, zero, 0, next_seq());
    } else {
        kb_manager_set_matrix_cb(NULL);
        kb_manager_set_paused(false);
        uint8_t zero[SPLIT_MATRIX_BYTES] = {0};
        kb_manager_set_remote_matrix(zero);

        // Auto-populate ble_shared_addr the first time this device becomes the
        // split MASTER.  Both halves must advertise the same BLE address so the
        // host can reconnect after a role swap.  Using the master's own BT MAC as
        // the shared base is the right choice: the host already bonded to an
        // address derived from it.  We only write if the field is empty so that a
        // subsequent role swap (where the new master received this value via config
        // sync) does NOT overwrite it with the new master's own different MAC.
        cfg_system_t sys;
        if (cfg_system_get(&sys) == ESP_OK) {
            bool addr_empty = true;
            for (int i = 0; i < 6; i++) {
                if (sys.ble_shared_addr[i]) { addr_empty = false; break; }
            }
            if (addr_empty) {
                esp_read_mac(sys.ble_shared_addr, ESP_MAC_BT);
                cfg_system_set(&sys);
                ESP_LOGI(TAG, "Auto-set ble_shared_addr from own BT MAC: "
                         "%02X:%02X:%02X:%02X:%02X:%02X",
                         sys.ble_shared_addr[0], sys.ble_shared_addr[1],
                         sys.ble_shared_addr[2], sys.ble_shared_addr[3],
                         sys.ble_shared_addr[4], sys.ble_shared_addr[5]);
            }
        }
    }
}

/* =========================================================================
 * Message-specific handlers (called from on_split_message)
 * ========================================================================= */

static void handle_role_negotiate_msg(const uint8_t *src_mac,
                                       const uint8_t *payload, size_t len)
{
    if (s_state == SPLIT_STATE_CONNECTED) {
        // Peer re-sent ROLE_NEGOTIATE (missed our transition); re-confirm our role.
        send_role_negotiate();
        return;
    }

    if (s_state != SPLIT_STATE_CONNECTING) return;

    split_pair_data_t pd;
    uint8_t own_pref = split_pair_get_data(&pd) ? pd.preferred_role : 0;

    split_role_t decided = SPLIT_ROLE_NONE;
    esp_err_t ret = split_role_on_negotiate(src_mac, payload, len,
                                             s_own_mac, own_pref, &decided);
    if (ret != ESP_OK || decided == SPLIT_ROLE_NONE) return;

    s_role               = decided;
    s_state              = SPLIT_STATE_CONNECTED;
    s_peer_last_seen     = xTaskGetTickCount();
    s_connected_at       = xTaskGetTickCount();
    s_reconnect_interval = SPLIT_RECONNECT_MS_MIN;

    split_peer_info_t info = {.role = (uint8_t)s_role};
    memcpy(info.mac, s_peer_mac, 6);
    esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_CONNECTED,    &info,   sizeof(info),   0);
    esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_ROLE_CHANGED, &s_role, sizeof(s_role), 0);

    ESP_LOGI(TAG, "CONNECTED as %s", s_role == SPLIT_ROLE_MASTER ? "MASTER" : "SLAVE");

    apply_kb_routing_for_role(s_role);
    if (s_role == SPLIT_ROLE_MASTER) {
#if CONFIG_PM_ENABLE
        pm_apply_active();
#endif
        // Defer config sync to split_task: running it here (WiFi task) would block
        // the WiFi receive path via vTaskDelay inside split_config_sync_push_all.
        s_config_sync_pending = true;
    }
    apply_ble_routing_for_role(s_role);

    // If we became MASTER, immediately push our BLE state to the slave so its
    // configurator shows the correct mode/profile without waiting for a BLE event.
    if (s_role == SPLIT_ROLE_MASTER) {
        send_ble_status_to_slave();
    }
}

static void handle_heartbeat_msg(const uint8_t *src_mac,
                                  const uint8_t *payload, size_t len)
{
    (void)src_mac;
    s_peer_last_seen = xTaskGetTickCount();
    if (len < sizeof(split_heartbeat_payload_t)) return;

    const split_heartbeat_payload_t *hb = (const split_heartbeat_payload_t *)payload;
    s_peer_rssi = hb->rssi;

    if (s_state != SPLIT_STATE_CONNECTED) return;

    if (s_role == SPLIT_ROLE_MASTER) {
        // Echo sent_us back so slave can compute RTT = (now - sent_us) / 2.
        split_heartbeat_payload_t resp = {
            .state       = (uint8_t)s_state,
            .role        = (uint8_t)s_role,
            .battery_pct = 0xFF,
            .rssi        = s_peer_rssi,
            .sent_us     = hb->sent_us,
        };
        split_transport_send(s_peer_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_HEARTBEAT,
                             next_seq(), (const uint8_t *)&resp, sizeof(resp));
    } else if (s_role == SPLIT_ROLE_SLAVE && hb->sent_us != 0) {
        uint32_t rtt_us = (uint32_t)esp_timer_get_time() - hb->sent_us;
        uint32_t half   = rtt_us / 2;
        s_latency_us    = (uint16_t)(half > 65535u ? 65535u : half);
        ESP_LOGD(TAG, "RTT=%lu us  latency=%u us", (unsigned long)rtt_us, s_latency_us);
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
    // MIC has already been authenticated by split_transport before this callback;
    // it is not re-checked here.
    (void)mic;
    ESP_LOGD(TAG, "rx 0x%02X seq=%u from " MACSTR " (%u B)",
             type, seq, MAC2STR(src_mac), (unsigned)len);

    // Anti-replay: reject packets at or behind the last seen sequence number once
    // the link is established. Pairing messages are exempt (no session key yet).
    bool is_pairing_msg = (type == SPLIT_MSG_DISCOVERY   ||
                           type == SPLIT_MSG_PAIR_REQUEST ||
                           type == SPLIT_MSG_PAIR_RESPONSE);
    if (!is_pairing_msg && s_peer_seq_valid) {
        // Unsigned modular subtraction: gives the forward distance in the 16-bit
        // sequence space.  Values in [1, 32767] are "new"; 0 is a duplicate; values
        // in [32768, 65535] are "old" (more than half the sequence space behind).
        // This avoids implementation-defined behaviour from casting a negative int
        // to int16_t while achieving the same wraparound-aware window of 32768.
        uint16_t udelta = (uint16_t)(seq - s_peer_seq_last);
        if (udelta == 0 || udelta >= 0x8000u) {
            ESP_LOGD(TAG, "dropped replay seq=%u (last=%u)", seq, s_peer_seq_last);
            return;
        }
    }
    if (!is_pairing_msg) {
        s_peer_seq_last  = seq;
        s_peer_seq_valid = true;
    }

    // Clear stale flag on any traffic from our connected peer.
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
                on_pairing_complete();
            } else if (ret != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "pair_request handler: %s", esp_err_to_name(ret));
            }
        }
        break;

    case SPLIT_MSG_PAIR_RESPONSE:
        if (s_state == SPLIT_STATE_PAIRING) {
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
        if (s_state == SPLIT_STATE_CONNECTED) {
            split_role_t new_role;
            split_role_on_swap_req(s_role, &new_role);
            split_role_payload_t ack = {.proposed_role = (uint8_t)new_role};
            memcpy(ack.device_id, s_own_mac, 6);
            split_transport_send(s_peer_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_ROLE_SWAP_ACK,
                                 next_seq(), (const uint8_t *)&ack, sizeof(ack));
            s_role = new_role;
            esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_ROLE_CHANGED, &s_role, sizeof(s_role), 0);
            ESP_LOGI(TAG, "role swap: now %s", s_role == SPLIT_ROLE_MASTER ? "MASTER" : "SLAVE");
            apply_kb_routing_for_role(s_role);
            apply_ble_routing_for_role(s_role);
            if (s_role == SPLIT_ROLE_MASTER) {
                send_ble_status_to_slave();
                // Push full config (bonds, ble_cfg, system) to the new slave so
                // it can take over again if there's a further role swap.
                s_config_sync_pending = true;
            }
        }
        break;

    case SPLIT_MSG_ROLE_SWAP_ACK:
        if (s_state == SPLIT_STATE_CONNECTED) {
            split_role_t new_role;
            split_role_on_swap_ack(s_role, &new_role);
            s_role = new_role;
            esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_ROLE_CHANGED, &s_role, sizeof(s_role), 0);
            ESP_LOGI(TAG, "role swap ACK: now %s", s_role == SPLIT_ROLE_MASTER ? "MASTER" : "SLAVE");
            apply_kb_routing_for_role(s_role);
            apply_ble_routing_for_role(s_role);
            if (s_role == SPLIT_ROLE_MASTER) send_ble_status_to_slave();
        }
        break;

    /* ---- Key state (MASTER receives from SLAVE) ---- */

    case SPLIT_MSG_KEY_STATE_FULL:
        if (s_state == SPLIT_STATE_CONNECTED && s_role == SPLIT_ROLE_MASTER) {
            split_sync_on_key_state_full(payload, len);
            apply_remote_matrix_if_changed();
        }
        break;

    case SPLIT_MSG_KEY_STATE_DELTA:
        if (s_state == SPLIT_STATE_CONNECTED && s_role == SPLIT_ROLE_MASTER) {
            split_sync_on_key_state_delta(payload, len);
            apply_remote_matrix_if_changed();
        }
        break;

    /* ---- Heartbeat ---- */

    case SPLIT_MSG_HEARTBEAT:
        handle_heartbeat_msg(src_mac, payload, len);
        break;

    case SPLIT_MSG_DISCONNECT:
        if (s_state == SPLIT_STATE_CONNECTED || s_state == SPLIT_STATE_CONNECTING) {
            on_disconnect("graceful peer DISCONNECT");
        }
        break;

    /* ---- Config sync ---- */

    case SPLIT_MSG_CONFIG_SYNC: {
        bool reverse = false;
        split_config_sync_on_fragment(src_mac, payload, len, s_peer_mac, next_seq, &reverse);
        if (reverse) s_reverse_ble_sync = true;
        break;
    }

    case SPLIT_MSG_CONFIG_SYNC_ACK:
        split_config_sync_on_ack(payload, len);
        break;

    /* ---- RTT benchmark (Master → Slave: PING;  Slave → Master: PONG) ---- */

    case SPLIT_MSG_PING:
        if (s_state == SPLIT_STATE_CONNECTED && s_role == SPLIT_ROLE_SLAVE) {
            split_transport_send(s_peer_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_PONG,
                                 next_seq(), payload, len);
        }
        break;

    case SPLIT_MSG_PONG:
        if (s_state == SPLIT_STATE_CONNECTED && s_role == SPLIT_ROLE_MASTER
            && s_bench.active && len >= sizeof(split_ping_payload_t)) {
            const split_ping_payload_t *p = (const split_ping_payload_t *)payload;
            if (p->probe_idx < BENCH_PROBES) {
                uint32_t rtt = (uint32_t)esp_timer_get_time() - p->sent_us;
                s_bench.rtt_us[p->probe_idx] = rtt;
                s_bench.pongs_received++;
                ESP_LOGD(TAG, "PONG #%u: RTT = %lu us", p->probe_idx, (unsigned long)rtt);
                if (s_bench.pongs_received >= BENCH_PROBES) {
                    bench_finish();
                }
            }
        }
        break;

    /* ---- BLE proxy ---- */

    case SPLIT_MSG_BLE_CMD:
        // Slave forwarded a BLE command — master executes it.
        if (s_state == SPLIT_STATE_CONNECTED && s_role == SPLIT_ROLE_MASTER
            && len >= sizeof(split_ble_cmd_payload_t)) {
            const split_ble_cmd_payload_t *p = (const split_ble_cmd_payload_t *)payload;
            ESP_LOGI(TAG, "BLE cmd from slave: 0x%02X arg=%u", p->cmd, p->arg);
            execute_ble_cmd(p->cmd, p->arg);
        }
        break;

    case SPLIT_MSG_BLE_STATUS:
        // Master pushed its BLE state — slave caches it via event bus.
        if (s_state == SPLIT_STATE_CONNECTED && s_role == SPLIT_ROLE_SLAVE
            && len >= sizeof(split_ble_status_payload_t)) {
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
        break;

    default:
        ESP_LOGD(TAG, "unhandled msg type 0x%02X", type);
        break;
    }
}

/* =========================================================================
 * Main task — per-state tick handlers (called at ~100 Hz)
 * ========================================================================= */

static void tick_pairing(TickType_t now)
{
    if (s_pairing_deadline != 0 && now >= s_pairing_deadline) {
        ESP_LOGI(TAG, "pairing timed out");
        split_pair_cancel();
        s_state            = SPLIT_STATE_IDLE;
        s_pairing_deadline = 0;
        esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_PAIR_FAILED, NULL, 0, 0);
        return;
    }

    if ((now - s_last_discovery_tx) < pdMS_TO_TICKS(SPLIT_DISCOVERY_MS)) return;

    split_pair_data_t pd;
    uint8_t pref = split_pair_get_data(&pd) ? pd.preferred_role : 0;

    uint8_t disc_buf[sizeof(split_discovery_payload_t)];
    size_t  disc_len = split_pair_build_discovery(disc_buf, sizeof(disc_buf), s_own_mac, pref);
    if (disc_len > 0) {
        static const uint8_t bcast[] = SPLIT_BROADCAST_MAC;
        split_transport_send(bcast, SPLIT_PROTO_SPLIT, SPLIT_MSG_DISCOVERY,
                             next_seq(), disc_buf, disc_len);
    }
    s_last_discovery_tx = now;
}

static void tick_connecting(TickType_t now)
{
    if ((now - s_last_role_neg_tx) < pdMS_TO_TICKS(SPLIT_ROLE_NEG_MS)) return;
    if (split_pair_is_paired()) {
        send_role_negotiate();
    }
    s_last_role_neg_tx = now;
}

static void tick_heartbeat(TickType_t now)
{
    if ((now - s_last_heartbeat_tx) < pdMS_TO_TICKS(SPLIT_HEARTBEAT_MS)) return;

    uint8_t bat_pct = battery_get_level_pct();
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
    split_transport_send(s_peer_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_HEARTBEAT,
                         next_seq(), (const uint8_t *)&hb, sizeof(hb));
    s_last_heartbeat_tx = now;
}

static void tick_bench(TickType_t now)
{
    TickType_t bench_elapsed = now - s_bench.started_at;

    if (bench_elapsed >= pdMS_TO_TICKS(BENCH_SETTLE_MS + BENCH_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "RTT benchmark timed out (%u/%u pongs)",
                 s_bench.pongs_received, s_bench.probes_sent);
        bench_finish();
        return;
    }

    bool settle_done  = bench_elapsed >= pdMS_TO_TICKS(BENCH_SETTLE_MS);
    bool more_probes  = s_bench.probes_sent < BENCH_PROBES;
    bool probe_due    = (now - s_bench.last_probe_tx) >= pdMS_TO_TICKS(BENCH_PROBE_MS);

    if (!settle_done || !more_probes || !probe_due) return;

    split_ping_payload_t ping = {
        .probe_idx = s_bench.probes_sent,
        .sent_us   = (uint32_t)esp_timer_get_time(),
    };
    if (split_transport_send(s_peer_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_PING,
                             next_seq(), (const uint8_t *)&ping, sizeof(ping)) == ESP_OK) {
        ESP_LOGD(TAG, "PING #%u sent", ping.probe_idx);
        s_bench.probes_sent++;
    }
    s_bench.last_probe_tx = now;
}

static void tick_connected(TickType_t now)
{
    TickType_t since_hb = now - s_peer_last_seen;

    if (since_hb >= pdMS_TO_TICKS(SPLIT_DISCONNECT_MS)) {
        on_disconnect("heartbeat timeout");
        return;
    }

    if (since_hb >= pdMS_TO_TICKS(SPLIT_STALE_MS) && !s_link_stale) {
        s_link_stale = true;
        ESP_LOGW(TAG, "link stale — no heartbeat for >%u ms", SPLIT_STALE_MS);
        esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_STALE, NULL, 0, 0);
    }

    if (s_role == SPLIT_ROLE_MASTER && s_config_sync_pending
        && (now - s_connected_at) >= pdMS_TO_TICKS(SPLIT_CONFIG_SYNC_SETTLE_MS)) {
        s_config_sync_pending = false;
        ESP_LOGI(TAG, "running initial config sync from split_task");
        split_config_sync_push_all(s_peer_mac, next_seq);
    }

    if (s_config_sync_incremental) {
        s_config_sync_incremental = false;
        ESP_LOGI(TAG, "running incremental config sync from split_task (%s)",
                 s_role == SPLIT_ROLE_MASTER ? "master→slave" : "slave→master");
        split_config_sync_push_all(s_peer_mac, next_seq);
    }

    if (s_reverse_ble_sync) {
        s_reverse_ble_sync = false;
        ESP_LOGI(TAG, "reverse BLE sync: pushing own ble_cfg + bond to peer");
        split_config_sync_push(s_peer_mac, next_seq, CFGMOD_KIND_CONNECTION, "ble_cfg");
        split_config_sync_push(s_peer_mac, next_seq, CFGMOD_KIND_BLE_BOND, "all");
    }

    if (s_role == SPLIT_ROLE_SLAVE) {
        tick_heartbeat(now);
    } else if (s_role == SPLIT_ROLE_MASTER && s_bench.active) {
        tick_bench(now);
    }
}

static void tick_disconnected(TickType_t now)
{
    if ((now - s_last_reconnect_at) < pdMS_TO_TICKS(s_reconnect_interval)) return;

    if (split_pair_is_paired()) {
        ESP_LOGI(TAG, "reconnect attempt (backoff=%lu ms)", (unsigned long)s_reconnect_interval);
        // Reset transport to stored key before sending ROLE_NEGOTIATE.  If the
        // previous connection had advanced key state, this ensures both sides are
        // always on a key they share before role negotiation begins.
        split_transport_set_session_key(s_stored_key);
        s_state            = SPLIT_STATE_CONNECTING;
        s_last_role_neg_tx = 0; // Trigger immediate ROLE_NEGOTIATE on next tick.
    }
    s_last_reconnect_at = now;

    if (s_reconnect_interval < SPLIT_RECONNECT_MS_MAX) {
        s_reconnect_interval *= 2;
        if (s_reconnect_interval > SPLIT_RECONNECT_MS_MAX) {
            s_reconnect_interval = SPLIT_RECONNECT_MS_MAX;
        }
    }
}

static void split_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "split task started");

    const TickType_t tick_period    = pdMS_TO_TICKS(10);
    for (;;) {
        vTaskDelay(tick_period);
        TickType_t now = xTaskGetTickCount();

        switch (s_state) {
        case SPLIT_STATE_PAIRING:      tick_pairing(now);      break;
        case SPLIT_STATE_CONNECTING:   tick_connecting(now);   break;
        case SPLIT_STATE_CONNECTED:    tick_connected(now);    break;
        case SPLIT_STATE_DISCONNECTED: tick_disconnected(now); break;
        default: break;
        }
    }
}

/* =========================================================================
 * BLE proxy — commands and status forwarding over the split link
 *
 * BLE actions arrive from two sources:
 *   1. Physical key presses (ble_controller.c → ble_hid_*)
 *   2. USB configurator (MODULE_BLE → ble_usb_callback below)
 *
 * When the configurator is connected to the SLAVE:
 *   - BLE commands are forwarded to the MASTER via SPLIT_MSG_BLE_CMD.
 *   - The MASTER executes them and its BLE events fire normally.
 *   - After each BLE event the MASTER sends SPLIT_MSG_BLE_STATUS to the SLAVE.
 *   - The SLAVE posts SPLIT_EVENT_BLE_STATUS_UPDATED so statusmod can push
 *     the correct state (mode, profile, bitmap) back to the configurator.
 * ========================================================================= */

#define BLE_USB_CMD_TOGGLE_ROUTING 0x01
#define BLE_USB_CMD_PAIR           0x02
#define BLE_USB_CMD_CONNECT        0x03
#define BLE_USB_CMD_TOGGLE_CONN    0x04

static void execute_ble_cmd(uint8_t cmd, uint8_t arg)
{
    switch (cmd) {
    case BLE_USB_CMD_TOGGLE_ROUTING:
        ble_hid_set_routing_active(!ble_hid_is_routing_active());
        break;
    case BLE_USB_CMD_PAIR:
        ble_hid_profile_pair(arg);
        break;
    case BLE_USB_CMD_CONNECT:
        ble_hid_profile_connect_and_select(arg);
        break;
    case BLE_USB_CMD_TOGGLE_CONN:
        ble_hid_profile_toggle_connection(arg);
        break;
    default:
        ESP_LOGW(TAG, "unknown BLE cmd 0x%02X", cmd);
        break;
    }
}

// Called on MASTER whenever local BLE state changes.
static void send_ble_status_to_slave(void)
{
    if (s_state != SPLIT_STATE_CONNECTED || s_role != SPLIT_ROLE_MASTER) return;
    const cfg_ble_state_t *st = cfg_ble_get_state();
    split_ble_status_payload_t p = {
        .routing_active   = st->ble_routing_enabled ? 1 : 0,
        .selected_profile = (uint8_t)st->selected_profile,
        .connected_bitmap = ble_hid_get_connected_profiles_bitmap(),
        .pairing_profile  = (int8_t)ble_hid_get_pairing_profile(),
    };
    split_transport_send(s_peer_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_BLE_STATUS,
                         next_seq(), (const uint8_t *)&p, sizeof(p));
}

// Registered as a BLE_EVENTS handler so the master pushes status on every change.
static void on_ble_event_for_slave(void *arg, esp_event_base_t base,
                                    int32_t event_id, void *data)
{
    (void)arg; (void)base; (void)event_id; (void)data;
    send_ble_status_to_slave();
}

// MODULE_BLE USB callback — executes locally if MASTER/standalone, forwards if SLAVE.
static bool ble_usb_callback(uint8_t *data, uint16_t data_len)
{
    if (!data || data_len < 1) return false;
    uint8_t cmd = data[0];
    uint8_t arg = (data_len >= 2) ? data[1] : 0;

    if (s_role == SPLIT_ROLE_SLAVE && s_state == SPLIT_STATE_CONNECTED) {
        split_ble_cmd_payload_t payload = {.cmd = cmd, .arg = arg};
        esp_err_t ret = split_transport_send(s_peer_mac, SPLIT_PROTO_SPLIT,
                                             SPLIT_MSG_BLE_CMD, next_seq(),
                                             (const uint8_t *)&payload, sizeof(payload));
        return ret == ESP_OK;
    }

    execute_ble_cmd(cmd, arg);
    return true;
}

/* =========================================================================
 * USB configurator callback — receives split commands from the host tool
 *
 * Command byte layout (first byte of payload):
 *   0x01  START_PAIRING  [optional 4-byte LE timeout_ms]
 *   0x02  CANCEL_PAIRING
 *   0x03  UNPAIR
 *   0x04  GET_STATUS     (triggers a status module push)
 *   0x05  GET_REMOTE_MATRIX
 *   0x06  ROLE_SWAP
 *   0x07  RUN_BENCH
 *   0x08  GET_BENCH
 * ========================================================================= */

#define SPLIT_USB_CMD_START_PAIRING     0x01
#define SPLIT_USB_CMD_CANCEL_PAIRING    0x02
#define SPLIT_USB_CMD_UNPAIR            0x03
#define SPLIT_USB_CMD_GET_STATUS        0x04
#define SPLIT_USB_CMD_GET_REMOTE_MATRIX 0x05
#define SPLIT_USB_CMD_ROLE_SWAP         0x06
#define SPLIT_USB_CMD_RUN_BENCH         0x07
#define SPLIT_USB_CMD_GET_BENCH         0x08

static void send_usb_json_response(uint8_t cmd, const char *json, size_t json_len)
{
    uint8_t *resp = malloc(7 + json_len);
    if (!resp) return;
    resp[0] = MODULE_SPLIT;
    resp[1] = cmd;
    resp[2] = 0x00; // key ID (unused)
    resp[3] = 0x00; // status OK
    resp[4] = 0x00;
    resp[5] = 0x00;
    resp[6] = 0x00;
    memcpy(resp + 7, json, json_len);
    send_payload(resp, (uint16_t)(7 + json_len));
    free(resp);
}

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
        uint8_t rm[SPLIT_MATRIX_BYTES];
        split_sync_get_remote_matrix(rm);

        char json[SPLIT_MATRIX_BYTES * 4 + 4];
        int pos = 0;
        json[pos++] = '[';
        for (int i = 0; i < (int)SPLIT_MATRIX_BYTES; i++) {
            pos += snprintf(json + pos, sizeof(json) - (size_t)pos,
                            "%u%s", rm[i],
                            i < (int)SPLIT_MATRIX_BYTES - 1 ? "," : "");
        }
        json[pos++] = ']';
        json[pos]   = '\0';
        send_usb_json_response(cmd, json, (size_t)pos);
        break;
    }
    case SPLIT_USB_CMD_ROLE_SWAP:
        splitmod_request_role_swap();
        break;
    case SPLIT_USB_CMD_RUN_BENCH:
        bench_start();
        break;
    case SPLIT_USB_CMD_GET_BENCH: {
        char json[128];
        int pos = snprintf(json, sizeof(json),
                           "{\"active\":%s,\"min\":%lu,\"avg\":%lu,\"max\":%lu,\"lost\":%u}",
                           s_bench.active ? "true" : "false",
                           (unsigned long)s_bench.result_min_us,
                           (unsigned long)s_bench.result_avg_us,
                           (unsigned long)s_bench.result_max_us,
                           s_bench.result_lost);
        send_usb_json_response(cmd, json, (size_t)pos);
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
        memcpy(s_peer_mac,   pd.peer_mac,   6);
        memcpy(s_stored_key, pd.shared_key, SPLIT_CRYPTO_KEY_SIZE);
        split_transport_add_peer(pd.peer_mac, pd.channel);
        split_transport_set_session_key(s_stored_key);
        s_state = SPLIT_STATE_CONNECTING;
        ESP_LOGI(TAG, "paired — reconnecting to " MACSTR, MAC2STR(pd.peer_mac));
    } else {
        s_state = SPLIT_STATE_IDLE;
        ESP_LOGI(TAG, "no pairing data — idle");
    }

    BaseType_t xret = xTaskCreateWithCaps(
        split_task, "split", 4096, NULL, 5, &s_task_handle,
        // Must be internal DRAM: split_task handles config sync which calls NVS.
        // NVS reads disable the SPI cache. A SPIRAM stack would crash the system.
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    );
    if (xret != pdPASS) {
        ESP_LOGE(TAG, "failed to create split task");
        return ESP_FAIL;
    }

    esp_event_handler_register(KB_EVENTS,     KB_EVENT_SYSTEM_ACTION,    on_kb_system_action,    NULL);
    esp_event_handler_register(CONFIG_EVENTS, CONFIG_EVENT_KIND_UPDATED, on_config_updated,      NULL);
    esp_event_handler_register(BLE_EVENTS,    ESP_EVENT_ANY_ID,          on_ble_event_for_slave, NULL);
    usbmod_register_callback(MODULE_SPLIT, split_usb_callback);
    usbmod_register_callback(MODULE_BLE,   ble_usb_callback);

    ESP_LOGI(TAG, "splitmod initialised");
    return ESP_OK;
}

esp_err_t splitmod_deinit(void)
{
    if (s_state == SPLIT_STATE_CONNECTED) {
        split_transport_send(s_peer_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_DISCONNECT,
                             next_seq(), NULL, 0);
    }

    if (s_task_handle) {
        vTaskDelete(s_task_handle);
        s_task_handle = NULL;
    }

    split_transport_deinit();
    kb_manager_set_remote_matrix(NULL);

    s_state = SPLIT_STATE_DISABLED;
    s_role  = SPLIT_ROLE_NONE;
    memset(s_stored_key, 0, sizeof(s_stored_key));

    ESP_LOGI(TAG, "splitmod deinitialized");
    return ESP_OK;
}

esp_err_t splitmod_start_pairing(uint32_t timeout_ms)
{
    if (s_state == SPLIT_STATE_PAIRING) return ESP_OK;

    split_pair_start();
    s_last_discovery_tx = 0; // Force immediate first beacon on next tick.
    s_state             = SPLIT_STATE_PAIRING;
    s_pairing_deadline  = (timeout_ms > 0)
                          ? (xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms))
                          : 0;
    // Pairing messages (DISCOVERY, PAIR_REQUEST, PAIR_RESPONSE) are plaintext.
    // Clear any active session key so the transport doesn't try to decrypt them.
    split_transport_set_session_key(NULL);

    esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_PAIR_STARTED, NULL, 0, 0);
    ESP_LOGI(TAG, "entering pairing mode");
    return ESP_OK;
}

esp_err_t splitmod_cancel_pairing(void)
{
    if (s_state != SPLIT_STATE_PAIRING) return ESP_ERR_INVALID_STATE;
    split_pair_cancel();
    s_state            = SPLIT_STATE_IDLE;
    s_pairing_deadline = 0;
    esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_PAIR_FAILED, NULL, 0, 0);
    ESP_LOGI(TAG, "pairing cancelled");
    return ESP_OK;
}

esp_err_t splitmod_unpair(void)
{
    if (s_state == SPLIT_STATE_CONNECTED) {
        split_transport_send(s_peer_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_DISCONNECT,
                             next_seq(), NULL, 0);
        split_transport_remove_peer(s_peer_mac);
    }

    split_pair_clear();
    memset(s_peer_mac,   0, 6);
    memset(s_stored_key, 0, sizeof(s_stored_key));
    split_transport_set_session_key(NULL);
    s_state          = SPLIT_STATE_IDLE;
    s_role           = SPLIT_ROLE_NONE;
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
        .proposed_role = (s_role == SPLIT_ROLE_MASTER) ? SPLIT_ROLE_SLAVE : SPLIT_ROLE_MASTER,
    };
    memcpy(req.device_id, s_own_mac, 6);

    return split_transport_send(s_peer_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_ROLE_SWAP_REQ,
                                next_seq(), (const uint8_t *)&req, sizeof(req));
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

bool splitmod_is_enabled(void)   { return s_state != SPLIT_STATE_DISABLED; }
bool splitmod_is_connected(void) { return s_state == SPLIT_STATE_CONNECTED; }
split_role_t splitmod_get_role(void) { return s_role; }
