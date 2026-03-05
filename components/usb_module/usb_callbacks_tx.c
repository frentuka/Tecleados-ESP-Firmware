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
#include "esp_timer.h"

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

// ============ Blast mode state ============

static bool tx_blast_mode_flag = false;
static uint16_t tx_blast_total_packets = 0;
static uint64_t tx_blast_start_time_us = 0;
#define TX_BLAST_MAX_RECONCILE_ROUNDS 5
static uint8_t tx_blast_reconcile_attempts = 0;

// ============ Function pre-declarations ============

static bool tx_send_next_packet();
static bool tx_blast_send_all_mid_packets();
static bool tx_send_packet_by_index(uint16_t index);

// ============ Blast mode API ============

bool tx_blast_active()
{
    return tx_blast_mode_flag;
}

void tx_blast_handle_bitmap(const usb_packet_msg_t *msg)
{
    if (!tx_blast_mode_flag) {
        ESP_LOGE(TAG, "Received BITMAP but not in blast mode");
        return;
    }

    tx_blast_reconcile_attempts++;
    if (tx_blast_reconcile_attempts > TX_BLAST_MAX_RECONCILE_ROUNDS) {
        ESP_LOGE(TAG, "Blast: max reconcile attempts reached. Aborting.");
        build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, 0, 0, NULL);
        erase_tx_buffer();
        return;
    }

    // Parse bitmap from website — check which MID packets are missing
    // (indices 1..total-2 are MID, index 0 is FIRST already ACK'd, index total-1 is LAST)
    bool all_mid_received = true;
    bool any_retransmit_failed = false;

    for (uint16_t i = 1; i < tx_blast_total_packets - 1; i++) {
        uint16_t byte_idx = i / 8;
        uint8_t bit_idx = i % 8;

        if (byte_idx >= msg->payload_len) {
            // Bitmap too short — this packet wasn't reported, assume missing
            all_mid_received = false;
            ESP_LOGW(TAG, "Blast: bitmap too short, retransmitting packet %u", i);
            if (!tx_send_packet_by_index(i)) {
                any_retransmit_failed = true;
                break;
            }
            continue;
        }

        bool received = (msg->payload[byte_idx] >> bit_idx) & 1;
        if (!received) {
            all_mid_received = false;
            ESP_LOGW(TAG, "Blast: retransmitting missing packet %u", i);
            if (!tx_send_packet_by_index(i)) {
                any_retransmit_failed = true;
                break;
            }
        }
    }

    if (any_retransmit_failed) {
        ESP_LOGE(TAG, "Blast: retransmit failed. Aborting.");
        build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, 0, 0, NULL);
        erase_tx_buffer();
        return;
    }

    if (all_mid_received) {
        // All MID packets received — send LAST packet (commit)
        ESP_LOGI(TAG, "Blast: all MID packets confirmed. Sending LAST.");
        if (!tx_send_packet_by_index(tx_blast_total_packets - 1)) {
            ESP_LOGE(TAG, "Blast: failed to send LAST packet. Aborting.");
            build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, 0, 0, NULL);
            erase_tx_buffer();
            return;
        }
        // Now awaiting ACK|OK or ACK|ERR for the LAST packet
        tx_awaiting_response = true;
    } else {
        // Not all received — send STATUS_REQ again for next round
        ESP_LOGI(TAG, "Blast: retransmitted gaps, sending STATUS_REQ round %u", tx_blast_reconcile_attempts);
        build_send_single_msg_packet(PAYLOAD_FLAG_STATUS_REQ, 0, 0, NULL);
        tx_last_packet_timestamp_us = esp_timer_get_time();
        tx_awaiting_response = true;
    }
}

// ============ TX packet by index ============

static bool tx_send_packet_by_index(uint16_t index)
{
    if (index >= tx_blast_total_packets) {
        ESP_LOGE(TAG, "tx_send_packet_by_index: invalid index %u", index);
        return false;
    }

    // Calculate buffer offset and payload length for this index
    uint16_t offset = index * MAX_PAYLOAD_LENGTH;
    uint16_t bytes_from_offset = tx_buf_len - offset;
    uint16_t payload_len = bytes_from_offset > MAX_PAYLOAD_LENGTH ? MAX_PAYLOAD_LENGTH : bytes_from_offset;

    if (offset >= tx_buf_len) {
        ESP_LOGE(TAG, "tx_send_packet_by_index: offset %u >= buf_len %u", offset, tx_buf_len);
        return false;
    }

    // Build packet
    usb_packet_msg_t msg = {0};

    // Calculate remaining_packets: rem = total - 1 - index
    msg.remaining_packets = tx_blast_total_packets - 1 - index;
    msg.payload_len = payload_len;
    memcpy(msg.payload, tx_buf + offset, payload_len);

    // Set flags based on position
    if (index == 0) {
        msg.flags = PAYLOAD_FLAG_FIRST;
    } else if (index == tx_blast_total_packets - 1) {
        msg.flags = PAYLOAD_FLAG_LAST;
    } else {
        msg.flags = PAYLOAD_FLAG_MID;
    }

    usb_crc_prepare_packet((uint8_t*) &msg);

    if (!send_single_packet((uint8_t *) &msg, COMM_REPORT_SIZE)) {
        ESP_LOGE(TAG, "tx_send_packet_by_index: report send failed for index %u", index);
        return false;
    }

    tx_last_packet_timestamp_us = esp_timer_get_time();
    return true;
}

// ============ Blast send all MID packets ============

static bool tx_blast_send_all_mid_packets()
{
    // Send indices 1..total-2 (all MID packets)
    for (uint16_t i = 1; i < tx_blast_total_packets - 1; i++) {
        if (!tx_send_packet_by_index(i)) {
            ESP_LOGE(TAG, "Blast: failed to send MID packet %u", i);
            return false;
        }
    }

    ESP_LOGI(TAG, "Blast: sent %u MID packets", tx_blast_total_packets - 2);
    return true;
}

// ============ Response processing ============

void process_tx_response(const usb_packet_msg_t msg)
{
    // Check unexpected response
    if (!tx_awaiting_response) {
        ESP_LOGE(TAG, "Received unexpected TX response (flags: %02X). Ignoring.", msg.flags);
        return;
    }

    bool handled = false;

    // --- Blast mode: BITMAP response ---
    if (msg.flags == PAYLOAD_FLAG_BITMAP) {
        handled = true;
        tx_blast_handle_bitmap(&msg);
        return;
    }

    if (msg.flags & PAYLOAD_FLAG_ACK) {
        handled = true;

        // --- Blast mode: ACK to FIRST packet -> blast all MID + send STATUS_REQ ---
        if (tx_blast_mode_flag && tx_blast_reconcile_attempts == 0) {
            ESP_LOGI(TAG, "Blast: handshake ACK received, blasting MID packets");

            if (!tx_blast_send_all_mid_packets()) {
                ESP_LOGE(TAG, "Blast: failed to send MID packets. Aborting.");
                build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, 0, 0, NULL);
                erase_tx_buffer();
                return;
            }

            // Send STATUS_REQ to trigger reconciliation
            build_send_single_msg_packet(PAYLOAD_FLAG_STATUS_REQ, 0, 0, NULL);
            tx_last_packet_timestamp_us = esp_timer_get_time();
            tx_awaiting_response = true;
            return;
        }

        // --- Blast mode: ACK|OK to LAST packet -> done ---
        if (tx_blast_mode_flag && (msg.flags & PAYLOAD_FLAG_OK)) {
            uint64_t now_us = esp_timer_get_time();
            float size_kb = tx_buf_len / 1024.0f;
            float transfer_time_ms = (now_us - tx_blast_start_time_us) / 1000.0f;

            ESP_LOGI(TAG, "Payload TX Complete! Packets: %u | Size: %.2f KB | Transfer time: %.2f ms | Reconcile rounds: %u", 
                     tx_blast_total_packets, size_kb, transfer_time_ms, tx_blast_reconcile_attempts);

            erase_tx_buffer();
            return;
        }

        // --- Legacy mode: send next packet ---
        if (!tx_blast_mode_flag) {
            if (tx_buf_idx < tx_buf_len) {
                if (!tx_send_next_packet()) {
                    ESP_LOGE(TAG, "process_tx_response: Failed to send next packet. Aborting.");
                    build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, 0, 0, NULL);
                    return;
                }
                tx_awaiting_response = true;
            } else {
                erase_tx_buffer();
                if (!(msg.flags & PAYLOAD_FLAG_OK)) {
                    tx_awaiting_response = true; // awaiting for OK
                }
            }
        }
    }

    if (msg.flags & PAYLOAD_FLAG_NAK) {
        handled = true;
        ESP_LOGE(TAG, "Received NAK");
        
        if (tx_nak_resend_attempts >= TX_NAK_RESEND_MAX_ATTEMPTS) {
            ESP_LOGE(TAG, "Max NAK attempts reached. Aborting.");
            build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, 0, 0, NULL);
            erase_tx_buffer();
            return;
        }

        if (tx_blast_mode_flag) {
            // Blast mode NAK — resend FIRST packet
            if (!tx_send_packet_by_index(0)) {
                ESP_LOGE(TAG, "Blast: failed to resend FIRST. Aborting.");
                build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, 0, 0, NULL);
                erase_tx_buffer();
                return;
            }
        } else {
            // Legacy mode — resend last packet
            tx_buf_idx = tx_buf_last_packet_sent_idx;
            if (!tx_send_next_packet()) {
                ESP_LOGE(TAG, "Failed to resend packet.");
                build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, 0, 0, NULL);
                erase_tx_buffer();
                return;
            }
        }

        tx_nak_resend_attempts++;
    }

    if (msg.flags & PAYLOAD_FLAG_OK) {
        handled = true;
        erase_tx_buffer();
    }

    if (msg.flags & PAYLOAD_FLAG_ERR) {
        handled = true;
        ESP_LOGE(TAG, "Received ERR response");
        erase_tx_buffer();
    }

    if (msg.flags & PAYLOAD_FLAG_ABORT) {
        handled = true;
        ESP_LOGE(TAG, "Received ABORT response");
        erase_tx_buffer();
        return;
    }

    if (!handled) {
        ESP_LOGE(TAG, "Unhandled TX response flag: %02X. Force erasing buffer.", msg.flags);
        erase_tx_buffer();
    }
}

// ============ Buffer management ============

void erase_tx_buffer()
{
    memset(tx_buf, 0, sizeof(tx_buf));
    tx_buf_len = 0;
    tx_buf_idx = 0;
    tx_buf_last_packet_sent_idx = 0;
    tx_last_packet_timestamp_us = 0;
    tx_awaiting_response = false;
    tx_nak_resend_attempts = 0;
    tx_blast_mode_flag = false;
    tx_blast_total_packets = 0;
    tx_blast_start_time_us = 0;
    tx_blast_reconcile_attempts = 0;
}

uint64_t tx_get_last_packet_timestamp_us()
{
    return tx_last_packet_timestamp_us;
}

// ============ Legacy helpers (kept for single-packet compat) ============

static bool append_payload_to_tx_buffer(const uint8_t *data, uint8_t data_len)
{
    if (tx_buf_len + data_len > MAX_TX_BUF_SIZE) {
        ESP_LOGE(TAG, "TX BUF Error: trying to append packets bigger than available space (trying: %lu, max av: %lu)", data_len + tx_buf_len, MAX_TX_BUF_SIZE);
        return false;
    }

    memcpy(tx_buf + tx_buf_len, data, data_len);
    tx_buf_len+= data_len;

    return true;
}

static void tx_buf_extract_next_msg(usb_packet_msg_t *msg)
{
    memset(msg, 0, sizeof(*msg)); // msg.flags == 0x00 -> error
    if (!tx_buf_len) {
        ESP_LOGE(TAG, "Couldn't extract next msg from tx_buf. len == 0");
        return;
    }

    if (tx_buf_idx >= tx_buf_len) {
        ESP_LOGE(TAG, "Couldn't extract next msg from tx_buf. idx >= len");
        return;
    }

    uint16_t stripped_payload_len = tx_buf_len - tx_buf_idx;
    stripped_payload_len = stripped_payload_len > MAX_PAYLOAD_LENGTH ? MAX_PAYLOAD_LENGTH : stripped_payload_len;

    uint16_t bytes_left = tx_buf_len - tx_buf_idx;
    uint16_t bytes_left_after_this_msg = bytes_left - stripped_payload_len;
    
    msg->flags = 0xFF; // msg.flags == 0xFF -> success
    msg->remaining_packets = (bytes_left_after_this_msg + MAX_PAYLOAD_LENGTH - 1) / MAX_PAYLOAD_LENGTH;
    memcpy(msg->payload, tx_buf + tx_buf_idx, stripped_payload_len);
    msg->payload_len = stripped_payload_len;

    tx_buf_last_packet_sent_idx = tx_buf_idx;
    tx_buf_idx+= stripped_payload_len;

    usb_crc_prepare_packet((uint8_t*) msg);

     return;
}

static bool tx_send_next_packet()
{
    if (!tx_buf_len) {
        ESP_LOGE(TAG, "tx_send_next_packet: Buffer is empty.");
        return false;
    }

    bool is_first_msg = tx_buf_idx == 0;
    bool is_last_msg = tx_buf_len - tx_buf_idx <= MAX_PAYLOAD_LENGTH;

    usb_packet_msg_t msg = {0};
    tx_buf_extract_next_msg(&msg);

    if (msg.flags != 0xFF) {
        ESP_LOGE(TAG, "TX Buffer first message extraction failed. Aborting.");
        erase_tx_buffer();
        return false;
    }

    uint8_t flags = 0x00;
    if (is_first_msg) flags+= PAYLOAD_FLAG_FIRST;
    if (is_last_msg) flags+= PAYLOAD_FLAG_LAST;
    if (!is_first_msg && !is_last_msg) flags = PAYLOAD_FLAG_MID;

    msg.flags = flags;

    if (!send_single_packet((uint8_t *) &msg, COMM_REPORT_SIZE)) {
        ESP_LOGE(TAG, "TX Process queue full, dropping packet.");
        return false;
    }

    tx_last_packet_timestamp_us = esp_timer_get_time();

    return true;
}

// ============ Public API ============

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

    // Fill buffer
    memcpy(tx_buf, payload, payload_len);
    tx_buf_len = payload_len;

    // Determine if this is a multi-packet payload -> blast mode
    uint16_t total_packets = (payload_len + MAX_PAYLOAD_LENGTH - 1) / MAX_PAYLOAD_LENGTH;

    if (total_packets > 1) {
        // Enter blast mode
        tx_blast_mode_flag = true;
        tx_blast_total_packets = total_packets;
        tx_blast_reconcile_attempts = 0;
        tx_blast_start_time_us = esp_timer_get_time();

        // ESP_LOGI(TAG, "Blast TX: %u packets for %u bytes", total_packets, payload_len);

        // Send FIRST packet and wait for ACK (handshake)
        if (!tx_send_packet_by_index(0)) {
            ESP_LOGE(TAG, "Blast TX: failed to send FIRST packet");
            erase_tx_buffer();
            return false;
        }
    } else {
        // Single packet — legacy path
        if (!tx_send_next_packet()) {
            return false;
        }
    }

    tx_awaiting_response = true;
    return true;
}