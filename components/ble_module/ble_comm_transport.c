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
    // and wait/retry if the mbuf pool is exhausted.
    uint32_t wait_timeout_ticks = pdMS_TO_TICKS(250);
    if (wait_timeout_ticks == 0) wait_timeout_ticks = 1;
    TickType_t start_tick = xTaskGetTickCount();

    struct os_mbuf *om = NULL;
    while (om == NULL) {
        om = ble_hs_mbuf_from_flat(packet, len);
        if (om) break;
        
        if (xTaskGetTickCount() - start_tick > wait_timeout_ticks) {
            ESP_LOGW(TAG, "mbuf pool starved, timeout");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(5)); // Safe to block here (called from comm_tx task)
    }

    uint16_t tx_handle = ble_comm_get_tx_handle();
    int rc = ble_gatts_notify_custom(s_comm_conn_handle, tx_handle, om);
    return rc == 0;
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
        uint16_t max_packet = ble_comm_get_max_packet_size();
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
