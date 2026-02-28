#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "usb_callbacks_tx.h"

#include "usb_descriptors.h"
#include "usb_defs.h"
#include "usb_send.h"
#include "usb_crc.h"

#include "basic_utils.h"

#include "esp_log.h"

#define TAG "USB Callbacks TX"

// ============ TX Buffer ============

static uint8_t tx_buf[MAX_TX_BUF_SIZE] = {0};
static uint16_t tx_buf_len = 0;
static uint16_t tx_buf_idx = 0;
static uint16_t tx_buf_last_packet_sent_idx = 0;
static uint64_t tx_last_packet_timestamp_us = 0; // last sent packet timestamp
static bool tx_awaiting_response = false;

#define TX_NAK_RESEND_MAX_ATTEMPTS 3
static uint8_t tx_nak_resend_attempts = 0;

// function pre-declarations
static bool tx_send_next_packet();

void process_tx_response(const usb_packet_msg_t msg)
{
    // Check unexpected response
    if (!tx_awaiting_response) {
        ESP_LOGE(TAG, "Received unexpected TX response (flags: %02X). Ignoring.", msg.flags);
        return;
    }

    if (msg.flags & PAYLOAD_FLAG_ACK) {
        ESP_LOGI(TAG, "Received ACK:");
        print_bytes_as_chars(TAG, msg.payload, msg.payload_len);

        // send next packet, if any
        if (tx_buf_idx < tx_buf_len) {
            if (!tx_send_next_packet()) {
                ESP_LOGE(TAG, "process_tx_response: Failed to send next packet. Aborting.");
                build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, 0, 0, NULL);
                return;
            }

            tx_awaiting_response = true; // probably unnecessary
        }

        // this was the last packet's ack
        else {
            erase_tx_buffer();
            
            tx_awaiting_response = true; // awaiting for OK
        }
    }

    if (msg.flags & PAYLOAD_FLAG_NAK) {
        ESP_LOGE(TAG, "Received NAK");
        
        if (tx_nak_resend_attempts >= TX_NAK_RESEND_MAX_ATTEMPTS) {
            ESP_LOGE(TAG, "Max NAK attempts reached. Aborting.");
            build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, 0, 0, NULL);
            return;
        }

        // set IDX back to last sent packet's idx
        tx_buf_idx = tx_buf_last_packet_sent_idx;
        if (!tx_send_next_packet()) {
            ESP_LOGE(TAG, "Failed to resend packet.");
            tx_nak_resend_attempts = 0;
            build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, 0, 0, NULL);
            return;
        }

        tx_nak_resend_attempts++;
    }

    if (msg.flags & PAYLOAD_FLAG_OK) {
        ESP_LOGI(TAG, "Received OK response");
        tx_awaiting_response = false;
        erase_tx_buffer();
    }

    if (msg.flags & PAYLOAD_FLAG_ERR) {
        ESP_LOGE(TAG, "Received ERR response");
        tx_awaiting_response = false;
        erase_tx_buffer();
    }

    if (msg.flags & PAYLOAD_FLAG_ABORT) {
        ESP_LOGE(TAG, "Received ABORT response");
        tx_awaiting_response = false;
        erase_tx_buffer();
    }
}

// Erase tx buffer in order to receive new data
void erase_tx_buffer()
{
    memset(tx_buf, 0, sizeof(tx_buf));
    tx_buf_len = 0;
    tx_buf_idx = 0;
    tx_buf_last_packet_sent_idx = 0;
    tx_last_packet_timestamp_us = 0;
    tx_awaiting_response = false;
    ESP_LOGI(TAG, "TX buffer erased");
}

uint64_t tx_get_last_packet_timestamp_us()
{
    return tx_last_packet_timestamp_us;
}

// Will store data in rx buffer appending it next to data already stored
static bool append_payload_to_tx_buffer(const uint8_t *data, uint8_t data_len)
{
    if (tx_buf_len + data_len > MAX_TX_BUF_SIZE) {
        ESP_LOGE(TAG, "TX BUF Error: trying to append packets bigger than available space (trying: %lu, max av: %lu)", data_len + tx_buf_len, MAX_TX_BUF_SIZE);
        return false;
    }

    memcpy(tx_buf + tx_buf_len, data, data_len);
    tx_buf_len+= data_len;

    ESP_LOGI(TAG, "Appended data to TX buf. Data length: %d. Buffer length: %d sizeof(tx_buf): %ull", data_len, tx_buf_len, sizeof(tx_buf));

    return true;
}

// Will extract the next payload available and increment buffer's idx.
// If payload extraction is valid, msg.flags will be 0xFF. Otherwise, 0x00
static void tx_buf_extract_next_msg(usb_packet_msg_t *msg)
{
    memset(msg, 0, sizeof(msg)); // msg.flags == 0x00 -> error
    if (!tx_buf_len) {
        ESP_LOGE(TAG, "Couldn't extract next msg from tx_buf. len == 0");
        return;
    }

    if (tx_buf_idx >= tx_buf_len) {
        ESP_LOGE(TAG, "Couldn't extract next msg from tx_buf. idx >= len");
        return;
    }

    // extraction should succeed

    uint16_t stripped_payload_len = tx_buf_len - tx_buf_idx;
    stripped_payload_len = stripped_payload_len > MAX_PAYLOAD_LENGTH ? MAX_PAYLOAD_LENGTH : stripped_payload_len;

    // calculate bytes left to calculate amount of remaining packets
    uint16_t bytes_left = tx_buf_len - tx_buf_idx;
    
    // set all values
    msg->flags = 0xFF; // msg.flags == 0xFF -> success
    msg->remaining_packets = (bytes_left + MAX_PAYLOAD_LENGTH - 1) / MAX_PAYLOAD_LENGTH;
    memcpy(msg->payload, tx_buf + tx_buf_idx, stripped_payload_len);
    msg->payload_len = stripped_payload_len;

    // increment idx
    tx_buf_last_packet_sent_idx = tx_buf_idx;
    tx_buf_idx+= stripped_payload_len;

    // append crc data
    usb_crc_prepare_packet((uint8_t*) msg);

    // buffer emptying should be managed in tx_processing_queue

     return;
}

// Will extract and send the next available packet inside the buffer.
static bool tx_send_next_packet()
{
    // check buffer is not empty
    if (!tx_buf_len) {
        ESP_LOGE(TAG, "tx_send_next_packet: Buffer is empty.");
        return false;
    }

    bool is_first_msg = tx_buf_idx == 0;
    bool is_last_msg = tx_buf_len - tx_buf_idx <= MAX_PAYLOAD_LENGTH;

    // Extract first message
    usb_packet_msg_t msg = {0};
    tx_buf_extract_next_msg(&msg);

    // check msg validity
    if (msg.flags != 0xFF) {
        ESP_LOGE(TAG, "TX Buffer first message extraction failed. Aborting.");
        erase_tx_buffer();
        return false;
    }

    // calculate flags
    uint8_t flags = 0x00;
    if (is_first_msg) flags+= PAYLOAD_FLAG_FIRST;
    if (is_last_msg) flags+= PAYLOAD_FLAG_LAST;
    if (!is_first_msg && !is_last_msg) flags = PAYLOAD_FLAG_MID;

    msg.flags = flags;

    // Send first payload message. The others (if any) should be sent over
    if (!send_single_packet((uint8_t *) &msg, COMM_REPORT_SIZE)) {
        ESP_LOGE(TAG, "TX Process queue full, dropping packet.");
        return false;
    }

    return true;
}

// Send full payload using tx buffer
bool send_payload(const uint8_t *payload, uint16_t payload_len)
{
    // Check for buffer content
    if (tx_buf_len) {
        ESP_LOGE(TAG, "Can't send payload: dirty buffer");
        return false;
    }

    // Check if any response is being awaited
    if (tx_awaiting_response) {
        ESP_LOGE(TAG, "Can't send payload: awaiting for response");
        return false;
    }

    // fill buffer
    memcpy(tx_buf, payload, payload_len);
    tx_buf_len = payload_len;

    if (!tx_send_next_packet()) {
        return false;
    }

    tx_awaiting_response = true;

    return true;
}