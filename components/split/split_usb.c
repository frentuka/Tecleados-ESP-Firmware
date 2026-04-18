#include "split_usb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "usbmod.h"
#include "usb_send.h"

#include "splitmod.h"
#include "split_protocol.h"
#include "split_sync.h"
#include "split_bench.h"

#define TAG "SPLIT_US"

/* =========================================================================
 * Command byte layout (first byte of the configurator payload)
 * ========================================================================= */

#define SPLIT_USB_CMD_START_PAIRING     0x01  // + optional 4-byte LE timeout_ms
#define SPLIT_USB_CMD_CANCEL_PAIRING    0x02
#define SPLIT_USB_CMD_UNPAIR            0x03
#define SPLIT_USB_CMD_GET_STATUS        0x04  // No-op: configurator polls MODULE_STATUS
#define SPLIT_USB_CMD_GET_REMOTE_MATRIX 0x05
#define SPLIT_USB_CMD_ROLE_SWAP         0x06
#define SPLIT_USB_CMD_RUN_BENCH         0x07
#define SPLIT_USB_CMD_GET_BENCH         0x08

/* =========================================================================
 * JSON response helper — builds the 7-byte MODULE_SPLIT reply header then
 * appends the JSON body and hands it to the USB send pipe.
 * ========================================================================= */

#define SPLIT_USB_RESP_HDR_SIZE 7

static void send_usb_json_response(uint8_t cmd, const char *json, size_t json_len)
{
    uint8_t *resp = malloc(SPLIT_USB_RESP_HDR_SIZE + json_len);
    if (!resp) {
        ESP_LOGW(TAG, "resp alloc failed (%u bytes)",
                 (unsigned)(SPLIT_USB_RESP_HDR_SIZE + json_len));
        return;
    }
    resp[0] = MODULE_SPLIT;
    resp[1] = cmd;
    resp[2] = 0x00; // key ID (unused)
    resp[3] = 0x00; // status OK
    resp[4] = 0x00;
    resp[5] = 0x00;
    resp[6] = 0x00;
    memcpy(resp + SPLIT_USB_RESP_HDR_SIZE, json, json_len);
    send_payload(resp, (uint16_t)(SPLIT_USB_RESP_HDR_SIZE + json_len));
    free(resp);
}

/* =========================================================================
 * Command handlers
 * ========================================================================= */

static void handle_start_pairing(const uint8_t *data, uint16_t data_len)
{
    uint32_t timeout_ms = 0;
    if (data_len >= 5) {
        timeout_ms = (uint32_t)data[1]
                   | ((uint32_t)data[2] << 8)
                   | ((uint32_t)data[3] << 16)
                   | ((uint32_t)data[4] << 24);
    }
    splitmod_start_pairing(timeout_ms);
}

static void handle_get_remote_matrix(uint8_t cmd)
{
    uint8_t rm[SPLIT_MATRIX_BYTES];
    split_sync_get_remote_matrix(rm);

    // Generous margin: "255," per byte max = 4; 14 bytes + brackets + NUL.
    char json[SPLIT_MATRIX_BYTES * 4 + 8];
    int  pos = 0;
    json[pos++] = '[';
    for (int i = 0; i < (int)SPLIT_MATRIX_BYTES; i++) {
        pos += snprintf(json + pos, sizeof(json) - (size_t)pos,
                        "%u%s", rm[i],
                        i < (int)SPLIT_MATRIX_BYTES - 1 ? "," : "");
    }
    json[pos++] = ']';
    json[pos]   = '\0';
    send_usb_json_response(cmd, json, (size_t)pos);
}

static void handle_get_bench(uint8_t cmd)
{
    char   json[128];
    size_t n = split_bench_format_json(json, sizeof(json));
    if (n == 0) return;
    send_usb_json_response(cmd, json, n);
}

/* =========================================================================
 * Public callback
 * ========================================================================= */

bool split_usb_callback(uint8_t *data, uint16_t data_len)
{
    if (!data || data_len == 0) return false;

    uint8_t cmd = data[0];
    switch (cmd) {
    case SPLIT_USB_CMD_START_PAIRING:
        handle_start_pairing(data, data_len);
        return true;
    case SPLIT_USB_CMD_CANCEL_PAIRING:
        splitmod_cancel_pairing();
        return true;
    case SPLIT_USB_CMD_UNPAIR:
        splitmod_unpair();
        return true;
    case SPLIT_USB_CMD_GET_STATUS:
        return true;   // Configurator polls MODULE_STATUS for this.
    case SPLIT_USB_CMD_GET_REMOTE_MATRIX:
        handle_get_remote_matrix(cmd);
        return true;
    case SPLIT_USB_CMD_ROLE_SWAP:
        splitmod_request_role_swap();
        return true;
    case SPLIT_USB_CMD_RUN_BENCH:
        split_bench_start();
        return true;
    case SPLIT_USB_CMD_GET_BENCH:
        handle_get_bench(cmd);
        return true;
    default:
        ESP_LOGW(TAG, "unknown USB split command 0x%02X", cmd);
        return false;
    }
}
