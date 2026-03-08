#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <malloc.h>

#include "usb_callbacks_tx.h"
#include "usb_descriptors.h"
#include "usb_defs.h"
#include "usb_send.h"
#include "usb_crc.h"
#include "basic_utils.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define TAG "USB Callbacks TX"

// ============ TX State & Buffer ============

static uint8_t tx_buf[MAX_TX_BUF_SIZE] = {0};
static uint16_t tx_buf_len = 0;
static uint16_t tx_buf_idx = 0;
static uint16_t tx_buf_last_packet_sent_idx = 0;
static uint64_t tx_last_packet_timestamp_us = 0; 
static bool tx_awaiting_response = false;

#define TX_NAK_RESEND_MAX_ATTEMPTS 3
static uint8_t tx_nak_resend_attempts = 0;

// ============ Blast mode state ============

static bool tx_blast_mode_flag = false;
static uint16_t tx_blast_total_packets = 0;
static uint64_t tx_blast_start_time_us = 0;
#define TX_BLAST_MAX_RECONCILE_ROUNDS 5
static uint8_t tx_blast_reconcile_attempts = 0;

// ============ Queuing System ============

typedef struct {
    uint8_t *data;
    uint16_t len;
} tx_queue_item_t;

#define TX_QUEUE_LENGTH 16
static QueueHandle_t tx_queue = NULL;
static SemaphoreHandle_t tx_done_sem = NULL;

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

    bool all_mid_received = true;
    bool any_retransmit_failed = false;

    for (uint16_t i = 1; i < tx_blast_total_packets - 1; i++) {
        uint16_t byte_idx = i / 8;
        uint8_t bit_idx = i % 8;

        if (byte_idx >= msg->payload_len) {
            all_mid_received = false;
            if (!tx_send_packet_by_index(i)) {
                any_retransmit_failed = true;
                break;
            }
            continue;
        }

        bool received = (msg->payload[byte_idx] >> bit_idx) & 1;
        if (!received) {
            all_mid_received = false;
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
        ESP_LOGI(TAG, "Blast: all MID packets confirmed. Sending LAST.");
        if (!tx_send_packet_by_index(tx_blast_total_packets - 1)) {
            ESP_LOGE(TAG, "Blast: failed to send LAST packet. Aborting.");
            build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, 0, 0, NULL);
            erase_tx_buffer();
            return;
        }
        tx_awaiting_response = true;
    } else {
        ESP_LOGI(TAG, "Blast: retransmitted gaps, sending STATUS_REQ round %u", tx_blast_reconcile_attempts);
        build_send_single_msg_packet(PAYLOAD_FLAG_STATUS_REQ, 0, 0, NULL);
        tx_last_packet_timestamp_us = esp_timer_get_time();
        tx_awaiting_response = true;
    }
}

// ============ TX packet by index ============

static bool tx_send_packet_by_index(uint16_t index)
{
    if (index >= tx_blast_total_packets) return false;

    uint16_t offset = index * MAX_PAYLOAD_LENGTH;
    uint16_t bytes_from_offset = tx_buf_len - offset;
    uint16_t payload_len = bytes_from_offset > MAX_PAYLOAD_LENGTH ? MAX_PAYLOAD_LENGTH : bytes_from_offset;

    if (offset >= tx_buf_len) return false;

    usb_packet_msg_t msg = {0};
    msg.remaining_packets = tx_blast_total_packets - 1 - index;
    msg.payload_len = payload_len;
    memcpy(msg.payload, tx_buf + offset, payload_len);

    if (index == 0) msg.flags = PAYLOAD_FLAG_FIRST;
    else if (index == tx_blast_total_packets - 1) msg.flags = PAYLOAD_FLAG_LAST;
    else msg.flags = PAYLOAD_FLAG_MID;

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
    if (tx_blast_total_packets < 2) return true;

    for (uint16_t i = 1; i < tx_blast_total_packets - 1; i++) {
        if (!tx_send_packet_by_index(i)) return false;
    }

    ESP_LOGI(TAG, "Blast: sent %u MID packets", tx_blast_total_packets - 2);
    return true;
}

// ============ Response processing ============

void process_tx_response(const usb_packet_msg_t msg)
{
    if (!tx_awaiting_response) {
        ESP_LOGE(TAG, "Received unexpected TX response (flags: %02X).", msg.flags);
        return;
    }

    bool handled = false;

    if (msg.flags == PAYLOAD_FLAG_BITMAP) {
        handled = true;
        tx_blast_handle_bitmap(&msg);
        return;
    }

    if (msg.flags & PAYLOAD_FLAG_ACK) {
        handled = true;
        if (tx_blast_mode_flag && tx_blast_reconcile_attempts == 0) {
            if (!tx_blast_send_all_mid_packets()) {
                build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, 0, 0, NULL);
                erase_tx_buffer();
                return;
            }
            build_send_single_msg_packet(PAYLOAD_FLAG_STATUS_REQ, 0, 0, NULL);
            tx_last_packet_timestamp_us = esp_timer_get_time();
            tx_awaiting_response = true;
            return;
        }

        if (tx_blast_mode_flag && (msg.flags & PAYLOAD_FLAG_OK)) {
            erase_tx_buffer();
            return;
        }

        if (!tx_blast_mode_flag) {
            if (tx_buf_idx < tx_buf_len) {
                if (!tx_send_next_packet()) {
                    build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, 0, 0, NULL);
                    erase_tx_buffer();
                    return;
                }
                tx_awaiting_response = true;
            } else {
                if (msg.flags & PAYLOAD_FLAG_OK) {
                    erase_tx_buffer();
                } else {
                    tx_awaiting_response = true;
                }
            }
        }
    }

    if (msg.flags & PAYLOAD_FLAG_NAK) {
        handled = true;
        if (++tx_nak_resend_attempts >= TX_NAK_RESEND_MAX_ATTEMPTS) {
            build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, 0, 0, NULL);
            erase_tx_buffer();
            return;
        }

        if (tx_blast_mode_flag) {
            if (!tx_send_packet_by_index(0)) erase_tx_buffer();
        } else {
            tx_buf_idx = tx_buf_last_packet_sent_idx;
            if (!tx_send_next_packet()) erase_tx_buffer();
        }
    }

    if (msg.flags & (PAYLOAD_FLAG_OK | PAYLOAD_FLAG_ERR | PAYLOAD_FLAG_ABORT)) {
        handled = true;
        erase_tx_buffer();
    }

    if (!handled) {
        ESP_LOGE(TAG, "Unhandled TX response flag: %02X.", msg.flags);
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

    // Signal the TX task that the current transfer is done
    if (tx_done_sem != NULL) {
        xSemaphoreGive(tx_done_sem);
    }
}

uint64_t tx_get_last_packet_timestamp_us()
{
    return tx_last_packet_timestamp_us;
}

// ============ Internal helpers ============

static void tx_buf_extract_next_msg(usb_packet_msg_t *msg)
{
    memset(msg, 0, sizeof(*msg));
    if (tx_buf_idx >= tx_buf_len) return;

    uint16_t stripped_payload_len = tx_buf_len - tx_buf_idx;
    stripped_payload_len = stripped_payload_len > MAX_PAYLOAD_LENGTH ? MAX_PAYLOAD_LENGTH : stripped_payload_len;

    msg->flags = 0xFF; 
    msg->remaining_packets = (tx_buf_len - tx_buf_idx - stripped_payload_len + MAX_PAYLOAD_LENGTH - 1) / MAX_PAYLOAD_LENGTH;
    memcpy(msg->payload, tx_buf + tx_buf_idx, stripped_payload_len);
    msg->payload_len = stripped_payload_len;

    tx_buf_last_packet_sent_idx = tx_buf_idx;
    tx_buf_idx += stripped_payload_len;
    usb_crc_prepare_packet((uint8_t*) msg);
}

static bool tx_send_next_packet()
{
    if (!tx_buf_len) return false;

    bool is_first_msg = tx_buf_idx == 0;
    bool is_last_msg = tx_buf_len - tx_buf_idx <= MAX_PAYLOAD_LENGTH;

    usb_packet_msg_t msg = {0};
    tx_buf_extract_next_msg(&msg);

    if (msg.flags != 0xFF) return false;

    msg.flags = is_first_msg ? PAYLOAD_FLAG_FIRST : (is_last_msg ? PAYLOAD_FLAG_LAST : PAYLOAD_FLAG_MID);
    if (is_first_msg && is_last_msg) msg.flags = PAYLOAD_FLAG_FIRST | PAYLOAD_FLAG_LAST;

    if (!send_single_packet((uint8_t *) &msg, COMM_REPORT_SIZE)) return false;

    tx_last_packet_timestamp_us = esp_timer_get_time();
    return true;
}

// ============ TX Task ============

static void usb_tx_task(void *pvParameters)
{
    tx_queue_item_t item;
    while (1) {
        if (xQueueReceive(tx_queue, &item, portMAX_DELAY) == pdTRUE) {
            // New payload to send
            memcpy(tx_buf, item.data, item.len);
            tx_buf_len = item.len;
            free(item.data); // Free the allocated buffer

            uint16_t total_packets = (tx_buf_len + MAX_PAYLOAD_LENGTH - 1) / MAX_PAYLOAD_LENGTH;
            if (total_packets > 1) {
                tx_blast_mode_flag = true;
                tx_blast_total_packets = total_packets;
                tx_blast_reconcile_attempts = 0;
                tx_blast_start_time_us = esp_timer_get_time();
                if (!tx_send_packet_by_index(0)) {
                    erase_tx_buffer();
                    continue;
                }
            } else {
                if (!tx_send_next_packet()) {
                    erase_tx_buffer();
                    continue;
                }
            }

            tx_awaiting_response = true;

            // Wait for transfer completion (signaled by erase_tx_buffer)
            // Timeout after TX_TIMEOUT_MS to prevent lockups
            if (xSemaphoreTake(tx_done_sem, pdMS_TO_TICKS(TX_TIMEOUT_MS)) != pdTRUE) {
                ESP_LOGE(TAG, "TX timeout waiting for response. Clearing buffer.");
                erase_tx_buffer();
            }
        }
    }
}

// ============ Public API ============

bool send_payload(const uint8_t *payload, uint16_t payload_len)
{
    if (tx_queue == NULL) return false;

    // Allocate copy for the queue
    uint8_t *copy = (uint8_t *)malloc(payload_len);
    if (!copy) return false;
    memcpy(copy, payload, payload_len);

    tx_queue_item_t item = { .data = copy, .len = payload_len };
    if (xQueueSend(tx_queue, &item, 0) != pdTRUE) {
        ESP_LOGE(TAG, "TX Queue full, dropping payload.");
        free(copy);
        return false;
    }

    return true;
}

void usb_tx_init(void)
{
    tx_queue = xQueueCreate(TX_QUEUE_LENGTH, sizeof(tx_queue_item_t));
    tx_done_sem = xSemaphoreCreateBinary();
    xTaskCreate(usb_tx_task, "usb_tx_task", 4096, NULL, 10, NULL); // Higher priority than RX/Status
    ESP_LOGI(TAG, "USB TX Queuing System initialized");
}