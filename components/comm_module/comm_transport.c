#include "comm_transport.h"
#include <stddef.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "esp_log.h"
#include "comm_dispatch.h"

static const char *TAG = "comm_transport";

static comm_transport_ops_t s_transport_ops[COMM_TRANSPORT_COUNT] = {0};
static bool s_connected[COMM_TRANSPORT_COUNT] = {false};

void comm_transport_register(comm_transport_t id, const comm_transport_ops_t *ops) {
    if ((int)id < 0 || id >= COMM_TRANSPORT_COUNT || ops == NULL) return;
    s_transport_ops[id] = *ops;
    ESP_LOGI(TAG, "Registered transport %d", id);
}

const comm_transport_ops_t *comm_transport_get(comm_transport_t id) {
    if ((int)id < 0 || id >= COMM_TRANSPORT_COUNT) return NULL;
    if (s_transport_ops[id].send_packet == NULL) return NULL;
    return &s_transport_ops[id];
}

void comm_transport_receive_packet(comm_transport_t source, const uint8_t *packet, uint16_t len) {
    if ((int)source < 0 || source >= COMM_TRANSPORT_COUNT || packet == NULL || len == 0) return;
    comm_dispatch_enqueue(source, packet, len);
}

void comm_transport_set_connected(comm_transport_t id, bool connected) {
    if ((int)id < 0 || id >= COMM_TRANSPORT_COUNT) return;
    if (s_connected[id] != connected) {
        s_connected[id] = connected;
        ESP_LOGI(TAG, "Transport %d connected: %d", id, connected);
    }
}

bool comm_transport_is_connected(comm_transport_t id) {
    if ((int)id < 0 || id >= COMM_TRANSPORT_COUNT) return false;
    return s_connected[id];
}

bool comm_transport_any_connected(void) {
    for (int i = 0; i < COMM_TRANSPORT_COUNT; i++) {
        if (s_connected[i]) return true;
    }
    return false;
}
