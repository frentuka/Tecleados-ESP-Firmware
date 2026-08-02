#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "comm_send.h"
#include "comm_crc.h"
#include "comm_defs.h"
#include "esp_log.h"

static const char *TAG = "comm_send";

bool comm_build_send_single_packet(comm_transport_t target, uint8_t flags, uint16_t rem, uint8_t payload_len, const uint8_t *payload) {
    if (payload_len > 255) return false;

    // Minimum packet size is 4 bytes header + payload_len + 1 byte CRC
    uint16_t logical_len = sizeof(comm_packet_header_t) + payload_len + 1;
    
    uint8_t buffer[COMM_MAX_PACKET_SIZE] = {0};
    
    comm_packet_header_t *header = (comm_packet_header_t *)buffer;
    header->flags = flags;
    header->remaining_packets = rem;
    header->payload_len = payload_len;
    
    if (payload_len > 0 && payload != NULL) {
        memcpy(buffer + sizeof(comm_packet_header_t), payload, payload_len);
    }
    
    return comm_send_single_packet(target, buffer, logical_len);
}

bool comm_send_single_packet(comm_transport_t target, uint8_t *packet, uint16_t logical_len) {
    comm_crc_prepare_packet(packet, logical_len);
    
    const comm_transport_ops_t *ops = comm_transport_get(target);
    if (!ops || !ops->is_ready || !ops->is_ready()) {
        ESP_LOGW(TAG, "Transport %d not ready", target);
        return false;
    }
    
    return ops->send_packet(packet, logical_len);
}
