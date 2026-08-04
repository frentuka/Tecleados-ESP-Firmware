#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <malloc.h>

#include "comm_tx.h"
#include "comm_defs.h"
#include "comm_send.h"
#include "comm_crc.h"
#include "comm_dispatch.h"
#include "comm_session.h"
#include "basic_utils.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "comm_tx";

// ============ TX State & Buffer ============
static comm_transport_t s_tx_current_target = COMM_TRANSPORT_NONE;

static inline uint16_t tx_get_max_payload_len(void) {
    const comm_transport_ops_t *ops = comm_transport_get(s_tx_current_target);
    if (ops && ops->get_max_packet_size) {
        uint16_t m = ops->get_max_packet_size();
        if (m > sizeof(comm_packet_header_t) + 1) {
            return m - sizeof(comm_packet_header_t) - 1;
        }
    }
    return COMM_REPORT_SIZE - sizeof(comm_packet_header_t) - 1;
}

// Replaced macro with direct function calls

// ============ TX State & Buffer ============
static uint8_t s_tx_buf[MAX_TX_BUF_SIZE] = {0};
static uint16_t s_tx_buf_len = 0;
static uint16_t s_tx_buf_idx = 0;
static uint16_t s_tx_buf_last_packet_sent_idx = 0;
static uint64_t s_tx_last_packet_timestamp_us = 0; 
static bool s_tx_awaiting_response = false;

#define TX_NAK_RESEND_MAX_ATTEMPTS 3
static uint8_t s_tx_nak_resend_attempts = 0;

// ============ Blast mode state ============
static bool s_tx_blast_mode_flag = false;
static uint16_t s_tx_blast_total_packets = 0;
static uint64_t s_tx_blast_start_time_us = 0;
static uint16_t s_tx_blast_max_payload_len = 0;
#define TX_BLAST_MAX_RECONCILE_ROUNDS 5
static uint8_t s_tx_blast_reconcile_attempts = 0;

// ============ Queuing System ============
typedef struct {
    comm_transport_t target;
    uint8_t *data;
    uint16_t len;
} tx_queue_item_t;

#define TX_QUEUE_LENGTH 16
static QueueHandle_t s_tx_queue = NULL;
static SemaphoreHandle_t s_tx_done_sem = NULL;

static bool tx_send_next_packet(void);
static bool tx_blast_send_all_mid_packets(void);
static bool tx_send_packet_by_index(uint16_t index);

bool comm_tx_blast_active(void) {
    return s_tx_blast_mode_flag;
}

void comm_tx_blast_handle_bitmap(const uint8_t *packet, uint16_t len) {
    if (!s_tx_blast_mode_flag) {
        ESP_LOGE(TAG, "Received BITMAP but not in blast mode");
        return;
    }

    s_tx_blast_reconcile_attempts++;
    if (s_tx_blast_reconcile_attempts > TX_BLAST_MAX_RECONCILE_ROUNDS) {
        ESP_LOGE(TAG, "Blast: max reconcile attempts reached. Aborting.");
        comm_build_send_single_packet(s_tx_current_target, PAYLOAD_FLAG_ABORT, 0, 0, NULL);
        comm_erase_tx_buffer();
        return;
    }

    bool all_mid_received = true;
    bool any_retransmit_failed = false;
    comm_packet_header_t *header = (comm_packet_header_t *)packet;
    const uint8_t *bitmap_payload = packet + sizeof(comm_packet_header_t);

    for (uint16_t i = 1; i < s_tx_blast_total_packets - 1; i++) {
        uint16_t byte_idx = i / 8;
        uint8_t bit_idx = i % 8;

        if (byte_idx >= header->payload_len) {
            all_mid_received = false;
            if (!tx_send_packet_by_index(i)) {
                any_retransmit_failed = true;
                break;
            }
            continue;
        }

        bool received = (bitmap_payload[byte_idx] >> bit_idx) & 1;
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
        comm_build_send_single_packet(s_tx_current_target, PAYLOAD_FLAG_ABORT, 0, 0, NULL);
        comm_erase_tx_buffer();
        return;
    }

    if (all_mid_received) {
        if (!tx_send_packet_by_index(s_tx_blast_total_packets - 1)) {
            ESP_LOGE(TAG, "Blast: failed to send LAST packet. Aborting.");
            comm_build_send_single_packet(s_tx_current_target, PAYLOAD_FLAG_ABORT, 0, 0, NULL);
            comm_erase_tx_buffer();
            return;
        }
        s_tx_awaiting_response = true;
    } else {
        ESP_LOGI(TAG, "Blast: retransmitted gaps, sending STATUS_REQ round %u", s_tx_blast_reconcile_attempts);
        comm_build_send_single_packet(s_tx_current_target, PAYLOAD_FLAG_STATUS_REQ, 0, 0, NULL);
        s_tx_last_packet_timestamp_us = esp_timer_get_time();
        s_tx_awaiting_response = true;
    }
}

static bool tx_send_packet_by_index(uint16_t index) {
    if (index >= s_tx_blast_total_packets) return false;

    uint16_t offset = index * s_tx_blast_max_payload_len;
    uint16_t bytes_from_offset = s_tx_buf_len - offset;
    uint16_t payload_len = bytes_from_offset > s_tx_blast_max_payload_len ? s_tx_blast_max_payload_len : bytes_from_offset;

    if (offset >= s_tx_buf_len) return false;

    uint8_t flags = 0;
    if (index == 0) flags = PAYLOAD_FLAG_FIRST;
    else if (index == s_tx_blast_total_packets - 1) flags = PAYLOAD_FLAG_LAST;
    else flags = PAYLOAD_FLAG_MID;

    uint16_t rem = s_tx_blast_total_packets - 1 - index;

    if (!comm_build_send_single_packet(s_tx_current_target, flags, rem, payload_len, s_tx_buf + offset)) {
        return false;
    }

    s_tx_last_packet_timestamp_us = esp_timer_get_time();
    return true;
}

static bool tx_blast_send_all_mid_packets(void) {
    if (s_tx_blast_total_packets < 2) return true;
    for (uint16_t i = 1; i < s_tx_blast_total_packets - 1; i++) {
        if (!tx_send_packet_by_index(i)) return false;
    }
    return true;
}

void comm_process_tx_response(comm_transport_t source, const uint8_t *packet, uint16_t len) {
    if (source != s_tx_current_target) {
        ESP_LOGE(TAG, "Received TX response from unexpected transport %d (flags=0x%02X)", source, packet[0]);
        return;
    }

    if (!s_tx_awaiting_response) {
        ESP_LOGE(TAG, "Received unexpected TX response. (flags=0x%02X)", packet[0]);
        return;
    }

    comm_packet_header_t *header = (comm_packet_header_t *)packet;
    bool handled = false;

    if (header->flags == PAYLOAD_FLAG_BITMAP) {
        handled = true;
        comm_tx_blast_handle_bitmap(packet, len);
        return;
    }

    if (header->flags & PAYLOAD_FLAG_ACK) {
        handled = true;
        if (s_tx_blast_mode_flag && s_tx_blast_reconcile_attempts == 0) {
            if (!tx_blast_send_all_mid_packets()) {
                comm_build_send_single_packet(s_tx_current_target, PAYLOAD_FLAG_ABORT, 0, 0, NULL);
                comm_erase_tx_buffer();
                return;
            }
            comm_build_send_single_packet(s_tx_current_target, PAYLOAD_FLAG_STATUS_REQ, 0, 0, NULL);
            s_tx_last_packet_timestamp_us = esp_timer_get_time();
            s_tx_awaiting_response = true;
            return;
        }

        if (s_tx_blast_mode_flag && (header->flags & PAYLOAD_FLAG_OK)) {
            float time_ms = (esp_timer_get_time() - s_tx_blast_start_time_us) / 1000.0f;
            ESP_LOGI(TAG, "[Blast TX] Transaction complete: %u packets, %u bytes in %.1f ms", s_tx_blast_total_packets, s_tx_buf_len, time_ms);
            comm_erase_tx_buffer();
            return;
        }

        if (!s_tx_blast_mode_flag) {
            if (s_tx_buf_idx < s_tx_buf_len) {
                if (!tx_send_next_packet()) {
                    comm_build_send_single_packet(s_tx_current_target, PAYLOAD_FLAG_ABORT, 0, 0, NULL);
                    comm_erase_tx_buffer();
                    return;
                }
                s_tx_awaiting_response = true;
            } else {
                if (header->flags & PAYLOAD_FLAG_OK) {
                    ESP_LOGI(TAG, "[Single TX] Transaction complete: 1 packet, %u bytes", s_tx_buf_len);
                    comm_erase_tx_buffer();
                } else {
                    s_tx_awaiting_response = true;
                }
            }
        }
    }

    if (header->flags & PAYLOAD_FLAG_NAK) {
        handled = true;
        if (++s_tx_nak_resend_attempts >= TX_NAK_RESEND_MAX_ATTEMPTS) {
            comm_build_send_single_packet(s_tx_current_target, PAYLOAD_FLAG_ABORT, 0, 0, NULL);
            comm_erase_tx_buffer();
            return;
        }

        if (s_tx_blast_mode_flag) {
            if (!tx_send_packet_by_index(0)) comm_erase_tx_buffer();
        } else {
            s_tx_buf_idx = s_tx_buf_last_packet_sent_idx;
            if (!tx_send_next_packet()) comm_erase_tx_buffer();
        }
    }

    if (header->flags & (PAYLOAD_FLAG_OK | PAYLOAD_FLAG_ERR | PAYLOAD_FLAG_ABORT)) {
        handled = true;
        comm_erase_tx_buffer();
    }

    if (!handled) {
        ESP_LOGE(TAG, "Unhandled TX response flag.");
        comm_erase_tx_buffer();
    }
}

void comm_erase_tx_buffer(void) {
    s_tx_buf_len = 0;
    s_tx_buf_idx = 0;
    s_tx_buf_last_packet_sent_idx = 0;
    s_tx_last_packet_timestamp_us = 0;
    s_tx_awaiting_response = false;
    s_tx_nak_resend_attempts = 0;
    s_tx_blast_mode_flag = false;
    s_tx_blast_total_packets = 0;
    s_tx_blast_start_time_us = 0;
    s_tx_blast_max_payload_len = 0;
    s_tx_blast_reconcile_attempts = 0;
    comm_session_unlock();
    s_tx_current_target = COMM_TRANSPORT_NONE;

    if (s_tx_done_sem != NULL) {
        xSemaphoreGive(s_tx_done_sem);
    }
}

uint64_t comm_tx_get_last_packet_timestamp_us(void) {
    return s_tx_last_packet_timestamp_us;
}

static bool tx_send_next_packet(void) {
    if (!s_tx_buf_len) return false;

    if (s_tx_buf_idx >= s_tx_buf_len) return false;

    uint16_t current_mtu = tx_get_max_payload_len();
    uint16_t stripped_payload_len = s_tx_buf_len - s_tx_buf_idx;
    stripped_payload_len = stripped_payload_len > current_mtu ? current_mtu : stripped_payload_len;

    bool is_first_msg = s_tx_buf_idx == 0;
    bool is_last_msg = (s_tx_buf_len - s_tx_buf_idx) <= current_mtu;
    uint8_t flags = is_first_msg ? PAYLOAD_FLAG_FIRST : (is_last_msg ? PAYLOAD_FLAG_LAST : PAYLOAD_FLAG_MID);
    if (is_first_msg && is_last_msg) flags = PAYLOAD_FLAG_FIRST | PAYLOAD_FLAG_LAST;

    uint16_t rem = (s_tx_buf_len - s_tx_buf_idx - stripped_payload_len + current_mtu - 1) / current_mtu;
    
    s_tx_buf_last_packet_sent_idx = s_tx_buf_idx;
    
    if (!comm_build_send_single_packet(s_tx_current_target, flags, rem, stripped_payload_len, s_tx_buf + s_tx_buf_idx)) {
        return false;
    }

    s_tx_buf_idx += stripped_payload_len;
    s_tx_last_packet_timestamp_us = esp_timer_get_time();
    return true;
}

static void comm_tx_task(void *pvParameters) {
    tx_queue_item_t item;
    while (1) {
        if (xQueueReceive(s_tx_queue, &item, portMAX_DELAY) == pdTRUE) {
            
            // Clear any stale semaphore BEFORE starting a new transmission
            xSemaphoreTake(s_tx_done_sem, 0);

            s_tx_current_target = item.target;
            uint16_t current_mtu = tx_get_max_payload_len();
            uint16_t total_packets = (item.len + current_mtu - 1) / current_mtu;

            if (total_packets > 1) {
                if (item.target != COMM_TRANSPORT_BROADCAST) {
                    if (!comm_session_try_lock(item.target)) {
                        ESP_LOGE(TAG, "Failed to acquire session lock for blast TX to transport %d", item.target);
                        free(item.data);
                        s_tx_current_target = COMM_TRANSPORT_NONE;
                        continue;
                    }
                }
            }

            memcpy(s_tx_buf, item.data, item.len);
            s_tx_buf_len = item.len;
            free(item.data);

            if (total_packets > 1) {
                s_tx_blast_mode_flag = true;
                s_tx_blast_total_packets = total_packets;
                s_tx_blast_max_payload_len = current_mtu;
                s_tx_blast_reconcile_attempts = 0;
                s_tx_blast_start_time_us = esp_timer_get_time();
                if (!tx_send_packet_by_index(0)) {
                    comm_erase_tx_buffer();
                    continue;
                }
            } else {
                if (!tx_send_next_packet()) {
                    comm_erase_tx_buffer();
                    continue;
                }
            }

            s_tx_awaiting_response = true;

            if (xSemaphoreTake(s_tx_done_sem, pdMS_TO_TICKS(TX_TIMEOUT_MS)) != pdTRUE) {
                ESP_LOGW(TAG, "TX timeout waiting for response. (No configurator connected?)");
                comm_erase_tx_buffer();
            }
        }
    }
}

bool comm_send_payload(comm_transport_t target, const uint8_t *payload, uint16_t payload_len) {
    if (s_tx_queue == NULL) return false;

    if (target == COMM_TRANSPORT_BROADCAST) {
        bool any = false;
        for (int i = 0; i < COMM_TRANSPORT_COUNT; i++) {
            if (comm_transport_is_connected(i)) {
                if (comm_send_payload(i, payload, payload_len)) {
                    any = true;
                }
            }
        }
        return any;
    }

    uint8_t *copy = (uint8_t *)malloc(payload_len);
    if (!copy) return false;
    memcpy(copy, payload, payload_len);

    tx_queue_item_t item = { .target = target, .data = copy, .len = payload_len };
    if (xQueueSend(s_tx_queue, &item, 0) != pdTRUE) {
        ESP_LOGE(TAG, "TX Queue full, dropping payload.");
        free(copy);
        return false;
    }

    return true;
}

void comm_tx_init(void) {
    s_tx_queue = xQueueCreate(TX_QUEUE_LENGTH, sizeof(tx_queue_item_t));
    s_tx_done_sem = xSemaphoreCreateBinary();
    xTaskCreateWithCaps(comm_tx_task, "comm_tx_task", 4096, NULL, 10, NULL, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "COMM TX Queuing System initialized");
}
