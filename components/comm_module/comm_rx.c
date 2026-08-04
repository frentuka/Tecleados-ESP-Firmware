#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "comm_rx.h"
#include "comm_dispatch.h"
#include "comm_send.h"
#include "comm_session.h"
#include "basic_utils.h"

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "comm_rx";

static inline uint16_t rx_get_max_payload_len(void) {
    comm_transport_t source = comm_session_get_active();
    if (source == COMM_TRANSPORT_NONE) return COMM_REPORT_SIZE - sizeof(comm_packet_header_t) - 1;
    const comm_transport_ops_t *ops = comm_transport_get(source);
    if (ops && ops->get_max_packet_size) {
        uint16_t m = ops->get_max_packet_size();
        if (m > sizeof(comm_packet_header_t) + 1) {
            return m - sizeof(comm_packet_header_t) - 1;
        }
    }
    return COMM_REPORT_SIZE - sizeof(comm_packet_header_t) - 1;
}

// Replaced macro with direct function calls

// ============ Shared RX Buffer ============
static uint8_t s_rx_buf[MAX_RX_BUF_SIZE] = {0};
static uint16_t s_rx_buf_len = 0;
static uint64_t s_rx_last_packet_timestamp_us = 0;

// ============ Blast mode state ============
static bool s_rx_blast_mode_flag = false;
static uint16_t s_rx_blast_total_packets = 0;
static uint64_t s_rx_blast_start_time_us = 0;
static uint16_t s_rx_blast_max_payload_len = 0;
static uint8_t s_rx_blast_bitmap[RX_BLAST_BITMAP_BYTES] = {0};
static uint8_t s_rx_blast_payload_lens[RX_BLAST_MAX_PACKETS] = {0};

static bool append_payload_to_rx_buffer(const uint8_t *data, uint8_t data_len);
static bool process_rx_buffer(comm_transport_t source);

static inline void rx_blast_set_bit(uint16_t index) {
    if (index < RX_BLAST_MAX_PACKETS) {
        s_rx_blast_bitmap[index / 8] |= (1 << (index % 8));
    }
}

static inline bool rx_blast_get_bit(uint16_t index) {
    if (index >= RX_BLAST_MAX_PACKETS) return false;
    return (s_rx_blast_bitmap[index / 8] >> (index % 8)) & 1;
}

static void rx_blast_reset(void) {
    s_rx_blast_mode_flag = false;
    s_rx_blast_total_packets = 0;
    s_rx_blast_start_time_us = 0;
    s_rx_blast_max_payload_len = 0;
    memset(s_rx_blast_bitmap, 0, sizeof(s_rx_blast_bitmap));
    memset(s_rx_blast_payload_lens, 0, sizeof(s_rx_blast_payload_lens));
}

bool comm_rx_blast_active(void) {
    return s_rx_blast_mode_flag;
}

void comm_rx_blast_update_activity(void) {
    s_rx_last_packet_timestamp_us = esp_timer_get_time();
}

static void rx_blast_receive_packet(comm_transport_t source, const uint8_t *packet, uint16_t len) {
    comm_packet_header_t *header = (comm_packet_header_t *)packet;
    uint16_t index = s_rx_blast_total_packets - 1 - header->remaining_packets;

    if (index >= s_rx_blast_total_packets || index >= RX_BLAST_MAX_PACKETS) {
        ESP_LOGE(TAG, "Blast: invalid packet index %u (total: %u)", index, s_rx_blast_total_packets);
        return;
    }

    if (rx_blast_get_bit(index)) {
        ESP_LOGI(TAG, "Blast: duplicate packet index %u, skipping", index);
        return;
    }

    uint16_t offset = index * s_rx_blast_max_payload_len;
    if (offset + header->payload_len > MAX_RX_BUF_SIZE) {
        ESP_LOGE(TAG, "Blast: packet %u would overflow rx_buf", index);
        return;
    }

    const uint8_t *payload = packet + sizeof(comm_packet_header_t);
    memcpy(s_rx_buf + offset, payload, header->payload_len);
    s_rx_blast_payload_lens[index] = header->payload_len;
    rx_blast_set_bit(index);

    s_rx_last_packet_timestamp_us = esp_timer_get_time();
}

void comm_rx_blast_build_bitmap_response(uint8_t *out_payload, uint16_t *out_len) {
    comm_packet_header_t *header = (comm_packet_header_t *)out_payload;
    header->flags = PAYLOAD_FLAG_BITMAP;
    header->remaining_packets = 0;

    uint8_t bitmap_bytes_needed = (s_rx_blast_total_packets + 7) / 8;
    uint16_t current_mtu = rx_get_max_payload_len();
    if (bitmap_bytes_needed > current_mtu) {
        bitmap_bytes_needed = current_mtu;
    }

    header->payload_len = bitmap_bytes_needed;
    memcpy(out_payload + sizeof(comm_packet_header_t), s_rx_blast_bitmap, bitmap_bytes_needed);

    *out_len = sizeof(comm_packet_header_t) + bitmap_bytes_needed + 1; // + 1 for CRC
}

bool comm_rx_blast_commit(comm_transport_t source, const uint8_t *last_packet, uint16_t len) {
    comm_packet_header_t *header = (comm_packet_header_t *)last_packet;
    uint16_t last_index = s_rx_blast_total_packets - 1;
    uint16_t offset = last_index * s_rx_blast_max_payload_len;

    if (offset + header->payload_len > MAX_RX_BUF_SIZE) {
        ESP_LOGE(TAG, "Blast commit: LAST packet would overflow rx_buf");
        rx_blast_reset();
        comm_erase_rx_buffer();
        comm_session_unlock();
        return false;
    }

    const uint8_t *payload = last_packet + sizeof(comm_packet_header_t);
    memcpy(s_rx_buf + offset, payload, header->payload_len);
    s_rx_blast_payload_lens[last_index] = header->payload_len;
    rx_blast_set_bit(last_index);

    s_rx_buf_len = 0;
    for (uint16_t i = 0; i < s_rx_blast_total_packets; i++) {
        s_rx_buf_len += s_rx_blast_payload_lens[i];
    }

    uint64_t rx_process_start = esp_timer_get_time();
    bool result = process_rx_buffer(source);
    uint64_t rx_process_end = esp_timer_get_time();

    float size_kb = s_rx_buf_len / 1024.0f;
    float receive_time_ms = (s_rx_last_packet_timestamp_us - s_rx_blast_start_time_us) / 1000.0f;
    float process_time_ms = (rx_process_end - rx_process_start) / 1000.0f;
    float transfer_speed_kbps = receive_time_ms > 0 ? size_kb / (receive_time_ms / 1000.0f) : 0;

    ESP_LOGI(TAG, "[Blast RX] Complete. %u packets, %u bytes total in %.1f ms. Speed: %.2f KB/s", 
             s_rx_blast_total_packets, s_rx_buf_len, receive_time_ms, transfer_speed_kbps);

    rx_blast_reset();
    comm_erase_rx_buffer();
    comm_session_unlock();

    return result;
}

void comm_process_rx_request(comm_transport_t source, const uint8_t *packet, uint16_t len) {
    bool result = false;
    comm_packet_header_t *header = (comm_packet_header_t *)packet;
    const uint8_t *payload = packet + sizeof(comm_packet_header_t);

    if ((header->flags & PAYLOAD_FLAG_FIRST) && header->remaining_packets > 0) {
        if (!comm_session_try_lock(source)) {
            ESP_LOGW(TAG, "Failed to acquire session lock for blast mode from transport %d", source);
            comm_build_send_single_packet(source, PAYLOAD_FLAG_ABORT, header->remaining_packets, 0, NULL);
            return;
        }

        comm_erase_rx_buffer();
        rx_blast_reset();
        s_rx_blast_mode_flag = true;
        s_rx_blast_total_packets = header->remaining_packets + 1;
        s_rx_blast_start_time_us = esp_timer_get_time();
        s_rx_blast_max_payload_len = rx_get_max_payload_len();

        ESP_LOGI(TAG, "Blast mode: expecting %u packets from transport %d", s_rx_blast_total_packets, source);

        rx_blast_receive_packet(source, packet, len);
        return;
    }

    if (s_rx_blast_mode_flag && (header->flags & PAYLOAD_FLAG_MID)) {
        if (comm_session_get_active() != source) {
            ESP_LOGW(TAG, "Received MID from non-active transport %d", source);
            return;
        }
        rx_blast_receive_packet(source, packet, len);
        return;
    }

    if (s_rx_blast_mode_flag && (header->flags & PAYLOAD_FLAG_LAST)) {
        if (comm_session_get_active() != source) {
            ESP_LOGW(TAG, "Received LAST from non-active transport %d", source);
            return;
        }
        // Last is handled in comm_dispatch by calling comm_rx_blast_commit
        return;
    }

    // --- Legacy single-packet path (FIRST|LAST) ---

    if (header->flags & PAYLOAD_FLAG_FIRST) {
        comm_erase_rx_buffer();
    }

    if (header->flags & (PAYLOAD_FLAG_FIRST | PAYLOAD_FLAG_MID | PAYLOAD_FLAG_LAST)) {
        result = append_payload_to_rx_buffer(payload, header->payload_len);
        if (!result) {
            ESP_LOGE(TAG, "Error when appending to rx buffer. Aborting.");
            comm_erase_rx_buffer();
            comm_build_send_single_packet(source, PAYLOAD_FLAG_ABORT, header->remaining_packets, 0, NULL);
            return;
        }
    }

    if ((header->flags & PAYLOAD_FLAG_LAST) && result) {
        ESP_LOGI(TAG, "[Single RX] Transaction complete: 1 packet, %d bytes", header->payload_len);
        result = process_rx_buffer(source);
        if (!result) {
            ESP_LOGE(TAG, "Error when processing rx buffer. Responding with ERR.");
            comm_erase_rx_buffer();
            comm_build_send_single_packet(source, PAYLOAD_FLAG_ERR, header->remaining_packets, 0, NULL);
            return;
        }
    }

    if (s_rx_buf_len > 0) {
        s_rx_last_packet_timestamp_us = esp_timer_get_time();
    }
}

void comm_erase_rx_buffer(void) {
    memset(s_rx_buf, 0, sizeof(s_rx_buf));
    s_rx_buf_len = 0;
    s_rx_last_packet_timestamp_us = 0;
    rx_blast_reset();
}

uint64_t comm_rx_get_last_packet_timestamp_us(void) {
    return s_rx_last_packet_timestamp_us;
}

static bool append_payload_to_rx_buffer(const uint8_t *data, uint8_t data_len) {
    if (s_rx_buf_len + data_len > MAX_RX_BUF_SIZE) {
        ESP_LOGE(TAG, "RX BUF Error: trying to append packets bigger than available space");
        return false;
    }
    memcpy(s_rx_buf + s_rx_buf_len, data, data_len);
    s_rx_buf_len += data_len;
    return true;
}

static bool process_rx_buffer(comm_transport_t source) {
    if (!s_rx_buf_len) {
        ESP_LOGE(TAG, "Can't process a buffer that's 0 bytes long");
        return false;
    }
    comm_module_id_t module = (comm_module_id_t)s_rx_buf[0];
    uint8_t cmd = (s_rx_buf_len >= 2) ? s_rx_buf[1] : 0;
    uint8_t key_id = (s_rx_buf_len >= 3) ? s_rx_buf[2] : 0;
    ESP_LOGI(TAG, "COMMAND RECEIVED: module=%d, cmd=%d, keyId=%d, payload_len=%d bytes", module, cmd, key_id, s_rx_buf_len);
    
    bool success = comm_execute_callback(source, module, &s_rx_buf[1], s_rx_buf_len - 1);
    if (!success) {
        ESP_LOGE(TAG, "Module %d callback failed to execute", module);
        return false;
    }
    return true;
}
