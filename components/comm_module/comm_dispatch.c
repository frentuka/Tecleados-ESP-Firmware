#include "comm_dispatch.h"
#include "comm_rx.h"
#include "comm_tx.h"
#include "comm_crc.h"
#include "comm_defs.h"
#include "comm_send.h"
#include "comm_session.h"

#include "basic_utils.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/message_buffer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "comm_dispatch";

MessageBufferHandle_t s_comm_message_buffer = NULL;
static comm_transport_t s_current_source = COMM_TRANSPORT_NONE;
static comm_data_callback_t s_module_callbacks[COMM_MODULE_COUNT] = {0};

void comm_register_callback(comm_module_id_t callback_module, comm_data_callback_t callback) {
    if (callback_module >= COMM_MODULE_COUNT) {
        ESP_LOGE(TAG, "Failed to register callback: module > COMM_MODULE_COUNT");
        return;
    }
    s_module_callbacks[callback_module] = callback;
}

bool comm_execute_callback(comm_transport_t source, comm_module_id_t callback_module, uint8_t const *data, uint16_t data_len) {
    if (callback_module >= COMM_MODULE_COUNT) {
        ESP_LOGE(TAG, "Failed to execute callback: module > COMM_MODULE_COUNT");
        return false;
    }
    if (!s_module_callbacks[callback_module]) {
        ESP_LOGE(TAG, "Failed to execute callback: callback not registered");
        return false;
    }
    return s_module_callbacks[callback_module](source, (uint8_t *)data, data_len);
}

comm_transport_t comm_get_current_source(void) {
    return s_current_source;
}

static void process_incoming_packet(comm_transport_t source, const uint8_t *raw_packet, uint16_t raw_len) {
    if (raw_len < sizeof(comm_packet_header_t) + 1) {
        ESP_LOGE(TAG, "Packet too short");
        return;
    }
    
    comm_packet_header_t *header = (comm_packet_header_t *)raw_packet;
    uint16_t logical_len = sizeof(comm_packet_header_t) + header->payload_len + 1;
    
    if (raw_len < logical_len) {
        ESP_LOGE(TAG, "Physical length %d is less than logical length %d", raw_len, logical_len);
        return;
    }
    
    if (!comm_crc_verify_packet(raw_packet, logical_len)) {
        if (!comm_rx_blast_active()) {
            ESP_LOGE(TAG, "CRC verification failed. Responding with NAK.");
            comm_build_send_single_packet(source, PAYLOAD_FLAG_NAK, header->remaining_packets, 0, NULL);
        }
        return;
    }
    
    uint8_t flags = header->flags;
    
    if (!comm_rx_blast_active() || (flags & PAYLOAD_FLAG_FIRST)) {
        if (flags != PAYLOAD_FLAG_STATUS_REQ && flags != PAYLOAD_FLAG_BITMAP) {
            comm_build_send_single_packet(source, PAYLOAD_FLAG_ACK, header->remaining_packets, 0, NULL);
        }
    }
    
    if (flags == PAYLOAD_FLAG_STATUS_REQ) {
        comm_rx_blast_update_activity();
        if (comm_rx_blast_active()) {
            ESP_LOGI(TAG, "STATUS_REQ: sending RX bitmap");
            uint8_t bitmap_payload[COMM_MAX_PACKET_SIZE] = {0};
            uint16_t bitmap_logical_len = 0;
            comm_rx_blast_build_bitmap_response(bitmap_payload, &bitmap_logical_len);
            comm_send_single_packet(source, bitmap_payload, bitmap_logical_len);
        } else {
            ESP_LOGW(TAG, "STATUS_REQ received but not in blast mode");
        }
        return;
    }
    
    if (flags == PAYLOAD_FLAG_BITMAP) {
        comm_rx_blast_update_activity();
        comm_process_tx_response(source, raw_packet, logical_len);
        return;
    }
    
    bool is_rx = (flags & PAYLOAD_FLAG_FIRST) || (flags & PAYLOAD_FLAG_MID) || (flags & PAYLOAD_FLAG_LAST);
    bool is_tx = !is_rx && ((flags & PAYLOAD_FLAG_ACK) || (flags & PAYLOAD_FLAG_NAK) ||
                            (flags & PAYLOAD_FLAG_OK) || (flags & PAYLOAD_FLAG_ERR) ||
                            (flags & PAYLOAD_FLAG_ABORT));
                            
    if (is_rx && header->payload_len == 0) {
        ESP_LOGE(TAG, "Received RX payload_len == 0");
        return;
    }
    
    if (is_rx) {
        if (comm_rx_blast_active() && (flags & PAYLOAD_FLAG_LAST)) {
            ESP_LOGI(TAG, "Blast RX: LAST packet received, committing");
            bool result = comm_rx_blast_commit(source, raw_packet, logical_len);
            if (result) {
                comm_build_send_single_packet(source, PAYLOAD_FLAG_ACK | PAYLOAD_FLAG_OK, 0, 0, NULL);
            } else {
                comm_build_send_single_packet(source, PAYLOAD_FLAG_ACK | PAYLOAD_FLAG_ERR, 0, 0, NULL);
            }
            return;
        }
        comm_process_rx_request(source, raw_packet, logical_len);
        return;
    }
    
    if (is_tx) {
        comm_process_tx_response(source, raw_packet, logical_len);
        return;
    }
}

static void comm_processing_task(void *pvParameters) {
    uint8_t buffer[COMM_MAX_PACKET_SIZE + 1];
    while (1) {
        size_t received = xMessageBufferReceive(s_comm_message_buffer, buffer, sizeof(buffer), portMAX_DELAY);
        if (received > 1) {
            comm_transport_t source = (comm_transport_t)buffer[0];
            s_current_source = source;
            process_incoming_packet(source, &buffer[1], received - 1);
            s_current_source = COMM_TRANSPORT_NONE;
        }
    }
}

static void comm_timeouts_task(void *pvParameters) {
    while (1) {
        uint64_t now_us = esp_timer_get_time();
        
        uint64_t rx_last = comm_rx_get_last_packet_timestamp_us();
        if (rx_last > 0 && (now_us - rx_last) > RX_TIMEOUT_MS * 1000) {
            comm_erase_rx_buffer();
            comm_session_unlock();
        }
        
        uint64_t tx_last = comm_tx_get_last_packet_timestamp_us();
        if (tx_last > 0 && (now_us - tx_last) > TX_TIMEOUT_MS * 1000) {
            comm_erase_tx_buffer();
        }
        
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void comm_dispatch_init(void) {
    comm_tx_init();
    
    s_comm_message_buffer = xMessageBufferCreate(8192);
    if (s_comm_message_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to create message buffer");
        return;
    }
    
    xTaskCreateWithCaps(comm_processing_task, "comm_proc_task", 8192, NULL, 5, NULL, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    xTaskCreateWithCaps(comm_timeouts_task, "comm_timeout_task", 4096, NULL, 5, NULL, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void comm_dispatch_enqueue(comm_transport_t source, const uint8_t *packet, uint16_t len) {
    if (s_comm_message_buffer == NULL || packet == NULL || len == 0) return;

    uint16_t total_len = len + 1;
    uint8_t buffer[total_len];
    
    buffer[0] = (uint8_t)source;
    memcpy(&buffer[1], packet, len);

    size_t sent = xMessageBufferSend(s_comm_message_buffer, buffer, total_len, 0);
    if (sent == 0) {
        ESP_LOGE(TAG, "Message buffer full, dropped packet from transport %d", source);
    }
}
