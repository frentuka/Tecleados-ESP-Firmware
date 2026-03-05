#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "usb_callbacks_rx.h"
#include "usb_callbacks.h"
#include "usb_defs.h"
#include "usb_send.h"
#include "usb_crc.h"

#include "basic_utils.h"

#include "esp_log.h"
#include "esp_timer.h"

#define TAG "USB Callbacks RX"

// ============ RX Buffer ============

static uint8_t rx_buf[MAX_RX_BUF_SIZE] = {0};
static uint16_t rx_buf_len = 0;
static uint64_t rx_last_packet_timestamp_us = 0;      // last received packet timestamp

// ============ Blast mode state ============

static bool rx_blast_mode_flag = false;
static uint16_t rx_blast_total_packets = 0;
static uint64_t rx_blast_start_time_us = 0;
static uint8_t rx_blast_bitmap[RX_BLAST_BITMAP_BYTES] = {0};
// Store each packet's actual payload_len so we can reconstruct total length
static uint8_t rx_blast_payload_lens[RX_BLAST_MAX_PACKETS] = {0};

// ============ Function declaration ============

static bool append_payload_to_rx_buffer(const uint8_t *data, uint8_t data_len);
static bool process_rx_buffer();

// ============ Blast mode helpers ============

static inline void rx_blast_set_bit(uint16_t index)
{
    if (index < RX_BLAST_MAX_PACKETS) {
        rx_blast_bitmap[index / 8] |= (1 << (index % 8));
    }
}

static inline bool rx_blast_get_bit(uint16_t index)
{
    if (index >= RX_BLAST_MAX_PACKETS) return false;
    return (rx_blast_bitmap[index / 8] >> (index % 8)) & 1;
}

static void rx_blast_reset()
{
    rx_blast_mode_flag = false;
    rx_blast_total_packets = 0;
    rx_blast_start_time_us = 0;
    memset(rx_blast_bitmap, 0, sizeof(rx_blast_bitmap));
    memset(rx_blast_payload_lens, 0, sizeof(rx_blast_payload_lens));
}

bool rx_blast_active()
{
    return rx_blast_mode_flag;
}

void rx_blast_receive_packet(const usb_packet_msg_t *msg)
{
    // Calculate index from remaining_packets: index = total - 1 - rem
    // For FIRST packet (rem = total-1): index = 0
    // For last MID (rem = 1): index = total - 2
    uint16_t index = rx_blast_total_packets - 1 - msg->remaining_packets;

    if (index >= rx_blast_total_packets || index >= RX_BLAST_MAX_PACKETS) {
        ESP_LOGE(TAG, "Blast: invalid packet index %u (total: %u)", index, rx_blast_total_packets);
        return;
    }

    // Already received this packet? Skip (could be a retransmit of a packet we already got)
    if (rx_blast_get_bit(index)) {
        ESP_LOGI(TAG, "Blast: duplicate packet index %u, skipping", index);
        return;
    }

    // Write payload to correct offset in rx_buf
    uint16_t offset = index * MAX_PAYLOAD_LENGTH;
    if (offset + msg->payload_len > MAX_RX_BUF_SIZE) {
        ESP_LOGE(TAG, "Blast: packet %u would overflow rx_buf", index);
        return;
    }

    memcpy(rx_buf + offset, msg->payload, msg->payload_len);
    rx_blast_payload_lens[index] = msg->payload_len;
    rx_blast_set_bit(index);

    rx_last_packet_timestamp_us = esp_timer_get_time();

    // ESP_LOGI(TAG, "Blast: received packet %u/%u (%u bytes)", index, rx_blast_total_packets, msg->payload_len);
}

void rx_blast_build_bitmap_response(usb_packet_msg_t *out_msg)
{
    memset(out_msg, 0, sizeof(*out_msg));
    out_msg->flags = PAYLOAD_FLAG_BITMAP;
    out_msg->remaining_packets = 0;

    // Copy bitmap into payload (up to MAX_PAYLOAD_LENGTH bytes)
    uint8_t bitmap_bytes_needed = (rx_blast_total_packets + 7) / 8;
    if (bitmap_bytes_needed > MAX_PAYLOAD_LENGTH) {
        bitmap_bytes_needed = MAX_PAYLOAD_LENGTH;
    }

    memcpy(out_msg->payload, rx_blast_bitmap, bitmap_bytes_needed);
    out_msg->payload_len = bitmap_bytes_needed;

    // CRC will be added by send_single_packet
}

bool rx_blast_commit(const usb_packet_msg_t *last_msg)
{
    // Write the LAST packet payload to its index
    uint16_t last_index = rx_blast_total_packets - 1;
    uint16_t offset = last_index * MAX_PAYLOAD_LENGTH;

    if (offset + last_msg->payload_len > MAX_RX_BUF_SIZE) {
        ESP_LOGE(TAG, "Blast commit: LAST packet would overflow rx_buf");
        rx_blast_reset();
        erase_rx_buffer();
        return false;
    }

    memcpy(rx_buf + offset, last_msg->payload, last_msg->payload_len);
    rx_blast_payload_lens[last_index] = last_msg->payload_len;
    rx_blast_set_bit(last_index);

    // Calculate total buffer length from all packets' payload_lens
    rx_buf_len = 0;
    for (uint16_t i = 0; i < rx_blast_total_packets; i++) {
        rx_buf_len += rx_blast_payload_lens[i];
    }

    // Process the assembled buffer
    uint64_t rx_process_start = esp_timer_get_time();
    bool result = process_rx_buffer();
    uint64_t rx_process_end = esp_timer_get_time();

    float size_kb = rx_buf_len / 1024.0f;
    float receive_time_ms = (rx_last_packet_timestamp_us - rx_blast_start_time_us) / 1000.0f;
    float process_time_ms = (rx_process_end - rx_process_start) / 1000.0f;

    ESP_LOGI(TAG, "Payload RX Complete! Packets: %u | Size: %.2f KB | Receive time: %.2f ms | Process time: %.2f ms", 
             rx_blast_total_packets, size_kb, receive_time_ms, process_time_ms);

    // Clean up
    rx_blast_reset();
    erase_rx_buffer();

    return result;
}

// ============ Legacy sequential API ============

void process_rx_request(const usb_packet_msg_t msg)
{
    bool result = false;

    // Detect blast mode entry: FIRST flag with remaining > 0 (multi-packet)
    if ((msg.flags & PAYLOAD_FLAG_FIRST) && msg.remaining_packets > 0) {
        // Enter blast mode
        erase_rx_buffer();
        rx_blast_reset();
        rx_blast_mode_flag = true;
        rx_blast_total_packets = msg.remaining_packets + 1; // total = rem + 1
        rx_blast_start_time_us = esp_timer_get_time();

        ESP_LOGI(TAG, "Blast mode: expecting %u packets", rx_blast_total_packets);

        // Store the FIRST packet using blast receive
        rx_blast_receive_packet(&msg);
        return;
    }

    // Blast mode: MID packets
    if (rx_blast_mode_flag && (msg.flags & PAYLOAD_FLAG_MID)) {
        rx_blast_receive_packet(&msg);
        return;
    }

    // Blast mode: LAST packet -> commit
    if (rx_blast_mode_flag && (msg.flags & PAYLOAD_FLAG_LAST)) {
        result = rx_blast_commit(&msg);
        // Response (ACK|OK or ACK|ERR) is sent by the caller (usb_callbacks.c)
        return;
    }

    // --- Legacy single-packet path (FIRST|LAST) ---

    if (msg.flags & PAYLOAD_FLAG_FIRST) {
        erase_rx_buffer();
    }

    if (msg.flags & (PAYLOAD_FLAG_FIRST | PAYLOAD_FLAG_MID | PAYLOAD_FLAG_LAST)) {
        result = append_payload_to_rx_buffer(msg.payload, msg.payload_len);
        if (!result) {
            ESP_LOGE(TAG, "Error when appending to rx buffer. Aborting.");
            erase_rx_buffer();
            build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, msg.remaining_packets, 0, NULL);
            return;
        }
    }

    if ((msg.flags & PAYLOAD_FLAG_LAST) && result) {
        result = process_rx_buffer();
        if (!result) {
            ESP_LOGE(TAG, "Error when processing rx buffer. Responding with ERR.");
            erase_rx_buffer();
            build_send_single_msg_packet(PAYLOAD_FLAG_ERR, msg.remaining_packets, 0, NULL);
            return;
        }
    }

    if (!result) {
        ESP_LOGE(TAG, "Error");
        return;
    }

    if (rx_buf_len > 0) {
        rx_last_packet_timestamp_us = esp_timer_get_time();
    }
    
    return;
}

// Erase rx buffer in order to receive new data
void erase_rx_buffer()
{
    memset(rx_buf, 0, sizeof(rx_buf));
    rx_buf_len = 0;
    rx_last_packet_timestamp_us = 0;
    rx_blast_reset();
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

    return true;
}

// Execute payload inside rx_buffer. Will erase rx_buffer afterwards, regardless of the result.
static bool process_rx_buffer()
{
    if (!rx_buf_len) {
        ESP_LOGE(TAG, "Can't process a buffer that's 0 bytes long");
        return false;
    }

    usb_msg_module_t module = (usb_msg_module_t)rx_buf[0];
    if (module >= USB_MODULE_COUNT) {
        ESP_LOGE(TAG, "Invalid module ID in payload: %d", module);
        return false;
    }

    // Pass the rest of the payload to the module callback
    bool success = execute_callback(module, &rx_buf[1], rx_buf_len - 1);
    
    if (!success) {
        ESP_LOGE(TAG, "Module %d callback failed to execute", module);
        return false;
    }

    return true;
}