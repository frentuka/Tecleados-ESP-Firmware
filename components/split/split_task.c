#include "split_task.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_event.h"
#if CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif

#include "event_bus.h"
#include "kb_manager.h"
#include "battery.h"
#include "cfgmod.h"

#include "split_session.h"
#include "split_transport.h"
#include "split_protocol.h"
#include "split_pair.h"
#include "split_role.h"
#include "split_sync.h"
#include "split_config_sync.h"
#include "split_bridge.h"
#include "split_bench.h"
#include "blemod.h"
#include "usbmod.h"

#define TAG "SPLIT_TK"

/* =========================================================================
 * Timing constants
 * ========================================================================= */

#define SPLIT_HEARTBEAT_MS          150   // Slave → Master keepalive interval
#define SPLIT_STALE_MS             1500   // Silence this long → link stale (warn)
#define SPLIT_DISCONNECT_MS        5000   // Silence this long → peer disconnected
#define SPLIT_DISCOVERY_MS          500   // Interval between DISCOVERY broadcasts
#define SPLIT_ROLE_NEG_MS           500   // Retransmit ROLE_NEGOTIATE at this rate
#define SPLIT_RECONNECT_MS_MIN      500   // Initial reconnect backoff
#define SPLIT_RECONNECT_MS_MAX     5000   // Maximum reconnect backoff
#define SPLIT_CONFIG_SYNC_SETTLE_MS 300   // Post-connect delay before config sync

/* =========================================================================
 * Task-local timing and deferred-work flags.
 *
 * s_config_sync_pending   — full push after initial connect (with settle delay).
 * s_config_sync_incremental — incremental push from on_config_updated.
 * s_reverse_ble_sync      — peer sent stale ble_cfg; push ours back.
 *
 * These flags let non-task contexts (WiFi task, event-bus task) request
 * NVS-heavy work without blocking their own loops on vTaskDelay.
 * ========================================================================= */

static TickType_t s_last_discovery_tx = 0;
static TickType_t s_last_role_neg_tx  = 0;
static TickType_t s_last_reconnect_at = 0;
static uint32_t   s_reconnect_interval = SPLIT_RECONNECT_MS_MIN;
static TickType_t s_pairing_deadline  = 0;

static volatile bool s_config_sync_pending     = false;
static volatile bool s_config_sync_incremental = false;
static volatile bool s_reverse_ble_sync        = false;

static TaskHandle_t s_task_handle = NULL;

/* =========================================================================
 * Power management — only cycles on MASTER connect / matrix activity.
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
#else
static inline void pm_apply_active(void) {}
#endif

/* =========================================================================
 * ROLE_NEGOTIATE send — shared between dispatch and the CONNECTING tick
 * ========================================================================= */

void split_task_send_role_negotiate(void)
{
    split_pair_data_t pd;
    uint8_t pref = split_pair_get_data(&pd) ? pd.preferred_role : 0;

    split_role_negotiate_payload_t rp = {
        .proposed_role = pref,
        .usb_connected = tud_mounted() ? 1u : 0u,
        .ble_connected = ble_hid_is_connected() ? 1u : 0u,
        .last_role     = (uint8_t)split_role_load_last(),
    };
    memcpy(rp.device_id, split_session_own_mac(), 6);

    split_transport_send(split_session_peer_mac(), SPLIT_PROTO_SPLIT,
                         SPLIT_MSG_ROLE_NEGOTIATE, split_session_next_seq(),
                         (const uint8_t *)&rp, sizeof(rp));

    s_last_role_neg_tx = xTaskGetTickCount();
}

/* =========================================================================
 * Disconnect — tear down routing and arm reconnect
 * ========================================================================= */

void split_task_handle_disconnect(const char *reason)
{
    ESP_LOGW(TAG, "disconnected: %s", reason);

    split_session_set_state(SPLIT_STATE_DISCONNECTED);
    split_session_set_role(SPLIT_ROLE_NONE);
    split_session_set_link_stale(false);
    split_session_reset_rx_seq();

    s_reconnect_interval = SPLIT_RECONNECT_MS_MIN;
    s_last_reconnect_at  = xTaskGetTickCount();

    split_bridge_reset_routing_standalone();
    split_config_sync_reset();

    pm_apply_active();

    uint8_t reason_byte = 0;
    esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_DISCONNECTED,
                   &reason_byte, sizeof(reason_byte), 0);
}

/* =========================================================================
 * Public state-machine helpers
 * ========================================================================= */

void split_task_trigger_immediate_role_neg(void)   { s_last_role_neg_tx  = 0; }
void split_task_reset_discovery_timer(void)        { s_last_discovery_tx = 0; }
void split_task_set_pairing_deadline(TickType_t d) { s_pairing_deadline  = d; }
void split_task_reset_reconnect_backoff(void)      { s_reconnect_interval = SPLIT_RECONNECT_MS_MIN; }

void split_task_request_config_sync_initial(void)      { s_config_sync_pending     = true; }
void split_task_request_config_sync_incremental(void)  { s_config_sync_incremental = true; }
void split_task_request_reverse_ble_sync(void)         { s_reverse_ble_sync        = true; }

/* =========================================================================
 * Per-state tick handlers
 * ========================================================================= */

static void tick_pairing(TickType_t now)
{
    if (s_pairing_deadline != 0 && now >= s_pairing_deadline) {
        ESP_LOGI(TAG, "pairing timed out");
        split_pair_cancel();
        split_session_set_state(SPLIT_STATE_IDLE);
        s_pairing_deadline = 0;
        esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_PAIR_FAILED, NULL, 0, 0);
        return;
    }

    if ((now - s_last_discovery_tx) < pdMS_TO_TICKS(SPLIT_DISCOVERY_MS)) return;

    split_pair_data_t pd;
    uint8_t pref = split_pair_get_data(&pd) ? pd.preferred_role : 0;

    uint8_t disc_buf[sizeof(split_discovery_payload_t)];
    size_t  disc_len = split_pair_build_discovery(disc_buf, sizeof(disc_buf),
                                                   split_session_own_mac(), pref);
    if (disc_len > 0) {
        static const uint8_t bcast[] = SPLIT_BROADCAST_MAC;
        split_transport_send(bcast, SPLIT_PROTO_SPLIT, SPLIT_MSG_DISCOVERY,
                             split_session_next_seq(), disc_buf, disc_len);
    }
    s_last_discovery_tx = now;
}

static void tick_connecting(TickType_t now)
{
    if ((now - s_last_role_neg_tx) < pdMS_TO_TICKS(SPLIT_ROLE_NEG_MS)) return;
    if (split_pair_is_paired()) {
        split_task_send_role_negotiate();   // also updates s_last_role_neg_tx
    } else {
        s_last_role_neg_tx = now;
    }
}

static void tick_heartbeat(TickType_t now)
{
    if ((now - split_transport_get_last_tx_time()) < pdMS_TO_TICKS(SPLIT_HEARTBEAT_MS)) return;

    uint8_t bat_pct = battery_get_level_pct();
    if (bat_pct != 0xFF) {
        if      (bat_pct < 10) kb_manager_set_scan_divisor(4);
        else if (bat_pct < 30) kb_manager_set_scan_divisor(2);
        else                   kb_manager_set_scan_divisor(1);
    }

    split_heartbeat_payload_t hb = {
        .state       = (uint8_t)split_session_get_state(),
        .role        = (uint8_t)split_session_get_role(),
        .battery_pct = bat_pct,
        .rssi        = split_session_get_rssi(),
        .sent_us     = (uint32_t)esp_timer_get_time(),
    };
    split_transport_send(split_session_peer_mac(), SPLIT_PROTO_SPLIT,
                         SPLIT_MSG_HEARTBEAT, split_session_next_seq(),
                         (const uint8_t *)&hb, sizeof(hb));
}

static void drain_deferred_config_sync(TickType_t now)
{
    const uint8_t *peer_mac = split_session_peer_mac();

    if (split_session_get_role() == SPLIT_ROLE_MASTER && s_config_sync_pending &&
        (now - split_session_connected_at()) >= pdMS_TO_TICKS(SPLIT_CONFIG_SYNC_SETTLE_MS)) {
        s_config_sync_pending = false;
        ESP_LOGI(TAG, "running initial config sync from split_task");
        split_config_sync_push_all(peer_mac, split_session_next_seq);
    }

    if (s_config_sync_incremental) {
        s_config_sync_incremental = false;
        ESP_LOGI(TAG, "running incremental config sync from split_task (%s)",
                 split_session_get_role() == SPLIT_ROLE_MASTER ? "master→slave" : "slave→master");
        split_config_sync_push_all(peer_mac, split_session_next_seq);
    }

    if (s_reverse_ble_sync) {
        s_reverse_ble_sync = false;
        ESP_LOGI(TAG, "reverse BLE sync: pushing own ble_cfg + bond to peer");
        split_config_sync_push(peer_mac, split_session_next_seq,
                                CFGMOD_KIND_CONNECTION, "ble_cfg");
        split_config_sync_push(peer_mac, split_session_next_seq,
                                CFGMOD_KIND_BLE_BOND, "all");
    }
}

static void tick_connected(TickType_t now)
{
    TickType_t since_hb = now - split_session_peer_last_seen();

    if (since_hb >= pdMS_TO_TICKS(SPLIT_DISCONNECT_MS)) {
        split_task_handle_disconnect("heartbeat timeout");
        return;
    }

    if (since_hb >= pdMS_TO_TICKS(SPLIT_STALE_MS) && !split_session_is_link_stale()) {
        split_session_set_link_stale(true);
        ESP_LOGW(TAG, "link stale — no heartbeat for >%u ms", SPLIT_STALE_MS);
        esp_event_post(SPLIT_EVENTS, SPLIT_EVENT_STALE, NULL, 0, 0);
    }

    drain_deferred_config_sync(now);

    split_role_t role = split_session_get_role();
    if (role == SPLIT_ROLE_SLAVE) {
        tick_heartbeat(now);
    } else if (role == SPLIT_ROLE_MASTER) {
        split_bench_tick(now);
    }
}

static void tick_disconnected(TickType_t now)
{
    if ((now - s_last_reconnect_at) < pdMS_TO_TICKS(s_reconnect_interval)) return;

    if (split_pair_is_paired()) {
        ESP_LOGI(TAG, "reconnect attempt (backoff=%lu ms)",
                 (unsigned long)s_reconnect_interval);
        // Reset transport to stored key before sending ROLE_NEGOTIATE so both
        // sides are on a shared key before role negotiation begins.
        split_transport_set_session_key(split_session_stored_key());
        split_session_set_state(SPLIT_STATE_CONNECTING);
        split_task_trigger_immediate_role_neg();
    }
    s_last_reconnect_at = now;

    if (s_reconnect_interval < SPLIT_RECONNECT_MS_MAX) {
        s_reconnect_interval *= 2;
        if (s_reconnect_interval > SPLIT_RECONNECT_MS_MAX) {
            s_reconnect_interval = SPLIT_RECONNECT_MS_MAX;
        }
    }
}

/* =========================================================================
 * Main loop
 * ========================================================================= */

static void split_task_main(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "split task started");

    const TickType_t tick_period = pdMS_TO_TICKS(10);
    for (;;) {
        vTaskDelay(tick_period);
        TickType_t now = xTaskGetTickCount();

        switch (split_session_get_state()) {
        case SPLIT_STATE_PAIRING:      tick_pairing(now);      break;
        case SPLIT_STATE_CONNECTING:   tick_connecting(now);   break;
        case SPLIT_STATE_CONNECTED:    tick_connected(now);    break;
        case SPLIT_STATE_DISCONNECTED: tick_disconnected(now); break;
        default: break;
        }
    }
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

esp_err_t split_task_start(void)
{
    if (s_task_handle) return ESP_OK;

    BaseType_t ret = xTaskCreateWithCaps(
        split_task_main, "split", 4096, NULL, 5, &s_task_handle,
        // Must be internal DRAM: the task runs NVS writes (config sync).
        // NVS reads disable the SPI cache; a SPIRAM stack would crash.
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "failed to create split task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void split_task_stop(void)
{
    if (s_task_handle) {
        vTaskDelete(s_task_handle);
        s_task_handle = NULL;
    }
}
