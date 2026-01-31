#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "usb_callbacks_rx.h"
#include "usb_defs.h"
#include "usb_send.h"
#include "usb_crc.h"

#include "basic_utils.h"

#include "esp_log.h"
#include "esp_timer.h"

#define TAG "USB Callbacks RX"

static uint8_t rx_buf[MAX_RX_BUF_SIZE] = {0};
static uint16_t rx_buf_len = 0;
static uint64_t rx_last_packet_timestamp_us = 0;      // last received packet timestamp
static uint16_t rx_last_packet_remaining_packets = 0;

// ============ Function declaration ============

const uint64_t rx_get_last_packet_timestamp_us();
static bool append_payload_to_rx_buffer(const uint8_t *data, uint8_t data_len);
static bool process_rx_buffer();

void process_rx_request(const usb_packet_msg_t msg)
{
    bool result = false;

    if (msg.flags & PAYLOAD_FLAG_FIRST) {
        ESP_LOGI(TAG, "Identified FIRST flag");

        erase_rx_buffer();
        result = append_payload_to_rx_buffer(msg.payload, msg.payload_len);

        if (!result) {
            ESP_LOGE(TAG, "Error when appending to rx buffer. Aborting.");
            erase_rx_buffer();
            build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, msg.remaining_packets, 0, NULL);
            return;
        }
    }

    if (msg.flags & PAYLOAD_FLAG_MID) {
        ESP_LOGI(TAG, "Identified MID flag");

        result = append_payload_to_rx_buffer(msg.payload, msg.payload_len);

        if (!result) {
            ESP_LOGE(TAG, "Error when appending to rx buffer. Aborting.");
            erase_rx_buffer();
            build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, msg.remaining_packets, 0, NULL);
            return;
        }
    }

    if (msg.flags & PAYLOAD_FLAG_LAST) {
        ESP_LOGI(TAG, "Identified LAST flag");

        result = append_payload_to_rx_buffer(msg.payload, msg.payload_len);
        if (result) {
            result = process_rx_buffer();
            if (!result) {
                ESP_LOGE(TAG, "Error when processing rx buffer. Responding with ERR.");
                erase_rx_buffer();
                build_send_single_msg_packet(PAYLOAD_FLAG_ERR, msg.remaining_packets, 0, NULL);
                return;
            }
        } else {
            ESP_LOGE(TAG, "Error when appending to rx buffer. Aborting.");
            erase_rx_buffer();
            build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, msg.remaining_packets, 0, NULL);
            return;
        }
    }

    if (!result) {
        ESP_LOGE(TAG, "Error");
        return;
    }

    rx_last_packet_timestamp_us = esp_timer_get_time();
    
    return;
}

// Erase rx buffer in order to receive new data
void erase_rx_buffer()
{
    memset(rx_buf, 0, sizeof(rx_buf));
    rx_buf_len = 0;
    rx_last_packet_timestamp_us = 0;
    ESP_LOGI(TAG, "RX buffer erased");
}

uint64_t rx_get_last_packet_timestamp_us()
{
    return rx_last_packet_timestamp_us;
}

// Will store data in rx buffer appending it next to data already stored
static bool append_payload_to_rx_buffer(const uint8_t *data, uint8_t data_len)
{
    if (rx_buf_len + data_len > MAX_RX_BUF_SIZE) {
        ESP_LOGE(TAG, "RX BUF Error: trying to append packets bigger than available space (trying: %lu, max av: %lu)", data_len + rx_buf_len, MAX_RX_BUF_SIZE);
        return false;
    }

    memcpy(rx_buf + rx_buf_len, data, data_len);
    rx_buf_len+= data_len;

    ESP_LOGI(TAG, "Appended data to RX buf. Data length: %d. Buffer length: %d sizeof(rx_buf): %ull", data_len, rx_buf_len, sizeof(rx_buf));

    return true;
}

// Execute payload inside rx_buffer. Will erase rx_buffer afterwards, regardless of the result.
static bool process_rx_buffer()
{
    ESP_LOGI(TAG, "Processing RX buffer. rx_buf_len: %u", rx_buf_len);
    print_bytes_as_chars(TAG, rx_buf, rx_buf_len);

    return true;
}