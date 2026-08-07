#include "ble_comm_transport.h"
#include "ble_comm_service.h"
#include "comm_transport.h"
#include "comm_session.h"
#include "blemod.h" 
#include "host/ble_hs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ble_comm_transport";

static uint16_t s_comm_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_comm_subscribed = false;
static bool s_mtu_subscribed = false;

static bool ble_comm_send_packet(const uint8_t *packet, uint16_t len) {
    if (s_comm_conn_handle == BLE_HS_CONN_HANDLE_NONE) return false;
    if (!s_comm_subscribed) return false;

    // To prevent COMM from starving HID during blast TX, attempt allocation
    // and wait/retry if the mbuf pool is exhausted OR if ble_gatts_notify_custom
    // returns BLE_HS_ENOMEM (6), which happens when the NimBLE L2CAP/ACL buffer
    // pool is full even though the mbuf allocation itself succeeded.
    uint32_t wait_timeout_ticks = pdMS_TO_TICKS(250);
    if (wait_timeout_ticks == 0) wait_timeout_ticks = 1;
    TickType_t start_tick = xTaskGetTickCount();

    while (true) {
        if (xTaskGetTickCount() - start_tick > wait_timeout_ticks) {
            ESP_LOGW(TAG, "mbuf pool starved, timeout");
            return false;
        }

        struct os_mbuf *om = ble_hs_mbuf_from_flat(packet, len);
        if (om == NULL) {
            // Mbuf pool itself is exhausted — wait and retry
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        uint16_t tx_handle = ble_comm_get_tx_handle();
        int rc = ble_gatts_notify_custom(s_comm_conn_handle, tx_handle, om);
        if (rc == 0) return true;

        if (rc == BLE_HS_ENOMEM) {
            // NimBLE's internal ACL/L2CAP queue is full. ble_gatts_notify_custom
            // does NOT free the mbuf on failure — we must free it ourselves before
            // retrying, otherwise we leak the allocation.
            os_mbuf_free_chain(om);
            vTaskDelay(pdMS_TO_TICKS(5)); // Wait for the TX queue to drain
            continue;
        }

        // Any other error (disconnected, stack not synced, etc.) is unrecoverable
        ESP_LOGE(TAG, "ble_gatts_notify_custom failed: rc=%d", rc);
        return false;
    }
}

static bool ble_comm_is_ready(void) {
    return s_comm_conn_handle != BLE_HS_CONN_HANDLE_NONE
        && s_comm_subscribed
        && !ble_hid_is_suspended();
}

uint16_t ble_comm_get_max_packet_size(void) {
    if (s_comm_conn_handle == BLE_HS_CONN_HANDLE_NONE) return 63; // Default fallback
    uint16_t max_size = ble_att_mtu(s_comm_conn_handle) - 3;
    if (max_size < 5) return 5;
    return max_size > 260 ? 260 : max_size;
}

static const comm_transport_ops_t s_ble_transport_ops = {
    .send_packet = ble_comm_send_packet,
    .is_ready = ble_comm_is_ready,
    .get_max_packet_size = ble_comm_get_max_packet_size,
};

void ble_comm_transport_init(void) {
    comm_transport_register(COMM_TRANSPORT_BLE, &s_ble_transport_ops);
}

void ble_comm_set_conn_handle(uint16_t conn_handle) {
    if (s_comm_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        s_comm_conn_handle = conn_handle;
    }
}

void ble_comm_set_subscribed(uint16_t conn_handle, bool subscribed) {
    if (subscribed) {
        if (s_comm_conn_handle != BLE_HS_CONN_HANDLE_NONE
            && s_comm_conn_handle != conn_handle) {
            ESP_LOGW(TAG, "Rejecting duplicate COMM subscriber (handle=%d, existing=%d)",
                     conn_handle, s_comm_conn_handle);
            return;
        }
        s_comm_conn_handle = conn_handle;
        s_comm_subscribed = true;
        comm_transport_set_connected(COMM_TRANSPORT_BLE, true);
    } else {
        if (s_comm_conn_handle == conn_handle) {
            s_comm_subscribed = false;
            comm_transport_set_connected(COMM_TRANSPORT_BLE, false);
        }
    }
}

void ble_comm_set_mtu_subscribed(uint16_t conn_handle, bool subscribed) {
    if (s_comm_conn_handle == conn_handle) {
        s_mtu_subscribed = subscribed;
    }
}

void ble_comm_on_disconnect(uint16_t conn_handle) {
    if (s_comm_conn_handle != conn_handle) return;

    s_comm_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_comm_subscribed = false;
    s_mtu_subscribed = false;
    comm_transport_set_connected(COMM_TRANSPORT_BLE, false);

    if (comm_session_get_active() == COMM_TRANSPORT_BLE) {
        comm_session_unlock();
    }
    ESP_LOGI(TAG, "COMM transport disconnected (handle=%d)", conn_handle);
}

void ble_comm_on_mtu_change(uint16_t conn_handle, uint16_t mtu) {
    if (s_comm_conn_handle == conn_handle && s_mtu_subscribed) {
        // Use the `mtu` parameter directly instead of re-querying ble_att_mtu().
        // NimBLE may not have updated its internal ATT MTU state by the time this
        // callback fires, so ble_comm_get_max_packet_size() could return the stale
        // pre-negotiation value (23 - 3 = 20), causing the configurator to use
        // severely undersized packets for all subsequent BLE transfers.
        uint16_t max_packet = (mtu >= 8) ? (mtu - 3) : 5;
        if (max_packet > 260) max_packet = 260;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(&max_packet, sizeof(max_packet));
        if (om) {
            ble_gatts_notify_custom(conn_handle, ble_comm_get_mtu_handle(), om);
        }
    }
}

void ble_comm_reset_state(void) {
    ESP_LOGI(TAG, "Phase 3 Verif: COMM transport state cleared.");
    s_comm_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_comm_subscribed = false;
    s_mtu_subscribed = false;
    comm_transport_set_connected(COMM_TRANSPORT_BLE, false);
    if (comm_session_get_active() == COMM_TRANSPORT_BLE) {
        comm_session_unlock();
    }
}
