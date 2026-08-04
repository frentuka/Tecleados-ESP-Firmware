#include "ble_comm_service.h"
#include "ble_comm_transport.h"
#include "comm_defs.h"
#include "comm_transport.h"
#include "host/ble_hs.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ble_comm_svc";

/* TEF COMM Service — Custom vendor service for keyboard configuration.
 *
 * Service UUID:  4D544546-0001-4B42-4254-455F434F4D4D
 * RX Char UUID:  4D544546-0002-4B42-4254-455F434F4D4D  (WRITE | WRITE_NO_RSP)
 * TX Char UUID:  4D544546-0003-4B42-4254-455F434F4D4D  (READ | NOTIFY)
 */

const ble_uuid128_t comm_svc_uuid = BLE_UUID128_INIT(
    0x4D, 0x4D, 0x4F, 0x43, 0x5F, 0x45, 0x54, 0x42,
    0x42, 0x4B, 0x01, 0x00, 0x46, 0x45, 0x54, 0x4D);

static const ble_uuid128_t comm_rx_uuid = BLE_UUID128_INIT(
    0x4D, 0x4D, 0x4F, 0x43, 0x5F, 0x45, 0x54, 0x42,
    0x42, 0x4B, 0x02, 0x00, 0x46, 0x45, 0x54, 0x4D);

static const ble_uuid128_t comm_tx_uuid = BLE_UUID128_INIT(
    0x4D, 0x4D, 0x4F, 0x43, 0x5F, 0x45, 0x54, 0x42,
    0x42, 0x4B, 0x03, 0x00, 0x46, 0x45, 0x54, 0x4D);

static const ble_uuid128_t comm_mtu_uuid = BLE_UUID128_INIT(
    0x4D, 0x4D, 0x4F, 0x43, 0x5F, 0x45, 0x54, 0x42,
    0x42, 0x4B, 0x04, 0x00, 0x46, 0x45, 0x54, 0x4D);

static uint16_t s_tx_handle = 0;
static uint16_t s_mtu_handle = 0;

uint16_t ble_comm_get_tx_handle(void) {
    return s_tx_handle;
}

uint16_t ble_comm_get_mtu_handle(void) {
    return s_mtu_handle;
}

static int comm_rx_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > 0) {
            uint8_t data[len];
            os_mbuf_copydata(ctxt->om, 0, len, data);
            comm_transport_receive_packet(COMM_TRANSPORT_BLE, data, len);
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int comm_tx_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
    // TX is notify-only, read is permitted but we just return 0.
    return 0;
}

static int comm_mtu_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint16_t max_packet = ble_comm_get_max_packet_size();
        int rc = os_mbuf_append(ctxt->om, &max_packet, sizeof(max_packet));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def comm_svc_defs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &comm_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &comm_rx_uuid.u,
                .access_cb = comm_rx_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid = &comm_tx_uuid.u,
                .access_cb = comm_tx_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
                .val_handle = &s_tx_handle,
            },
            {
                .uuid = &comm_mtu_uuid.u,
                .access_cb = comm_mtu_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
                .val_handle = &s_mtu_handle,
            },
            {
                0, // No more characteristics
            }
        },
    },
    {
        0, // No more services
    },
};

void ble_comm_svc_register(void) {
    int rc = ble_gatts_count_cfg(comm_svc_defs);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(comm_svc_defs);
    assert(rc == 0);
    ESP_LOGI(TAG, "BLE COMM Service registered");
}
