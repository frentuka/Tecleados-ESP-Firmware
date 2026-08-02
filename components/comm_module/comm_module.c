#include "comm_module.h"
#include "comm_dispatch.h"
#include "comm_session.h"
#include "comm_tx.h"
#include <string.h>
#include <malloc.h>
#include "esp_log.h"

static const char *TAG = "comm_module";

void comm_init(void) {
    comm_session_init();
    comm_dispatch_init();
    ESP_LOGI(TAG, "Communication module initialized.");
}

void comm_register_module(comm_module_id_t module_id, comm_data_callback_t callback) {
    comm_register_callback(module_id, callback);
}

bool comm_send_message(comm_transport_t target, comm_module_id_t module_id, const uint8_t *payload, uint16_t len) {
    if (len + 1 > MAX_TX_BUF_SIZE) {
        ESP_LOGE(TAG, "Payload too large");
        return false;
    }
    uint8_t *buffer = malloc(len + 1);
    if (!buffer) return false;
    
    buffer[0] = (uint8_t)module_id;
    if (len > 0 && payload != NULL) {
        memcpy(buffer + 1, payload, len);
    }
    
    bool res = comm_send_payload(target, buffer, len + 1);
    free(buffer);
    return res;
}
