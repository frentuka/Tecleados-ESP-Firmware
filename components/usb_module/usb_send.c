#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "usb_send.h"

#include "usb_descriptors.h"
#include "usb_defs.h"
#include "usb_crc.h"

#include "basic_utils.h"

#include "esp_log.h"

#define TAG "USB Send"

bool build_send_single_msg_packet(uint8_t flags, uint16_t rem, uint8_t payload_len, uint8_t *payload)
{
    usb_packet_msg_t msg = {0};
    msg.flags = flags;
    msg.remaining_packets = rem;
    msg.payload_len = payload_len;
    memcpy(msg.payload, payload, payload_len);
    usb_crc_prepare_packet((uint8_t *) &msg);

    // ESP_LOGI(TAG, "Building+Sending message with flags "BYTE_TO_BINARY_PATTERN"", BYTE_TO_BINARY(flags));

    return send_single_packet((uint8_t *) &msg, sizeof(msg));
}

#define PACKET_SEND_MAX_ATTEMPTS 3
#define PACKET_SEND_ATTEMPT_DELAY_MS 10

// will send a packet of length COMM_REPORT_SIZE where last byte will be CRC
bool send_single_packet(uint8_t *packet, uint16_t packet_len)
{
    usb_crc_prepare_packet(packet);
    bool result = tud_hid_n_report(ITF_NUM_HID_COMM, REPORT_ID_COMM, packet, packet_len);

    for (uint8_t i = 1; !result && i < PACKET_SEND_MAX_ATTEMPTS; i++) {
        ESP_LOGE(TAG, "Failed report send attempt...");
        vTaskDelay(pdMS_TO_TICKS(PACKET_SEND_ATTEMPT_DELAY_MS));
        result = tud_hid_n_report(ITF_NUM_HID_COMM, REPORT_ID_COMM, packet, packet_len);
    }

    // if (result) ESP_LOGI(TAG, "Report sent successfully.");
    // else        ESP_LOGE(TAG, "Unable to send usb report.");

    return result;
}