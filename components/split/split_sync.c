#include "split_sync.h"
#include "split_transport.h"

#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#define TAG "SPLIT_SYNC"

/* =========================================================================
 * Remote matrix state (written from WiFi-task, read from kb_manager-task)
 * ========================================================================= */

static uint8_t         s_remote[SPLIT_MATRIX_BYTES];
static portMUX_TYPE    s_lock    = portMUX_INITIALIZER_UNLOCKED;
static volatile bool   s_changed = false;

/* =========================================================================
 * MASTER — receive from slave
 * ========================================================================= */

esp_err_t split_sync_on_key_state_full(const uint8_t *payload, size_t len)
{
    if (len < sizeof(split_key_state_full_payload_t)) return ESP_ERR_INVALID_SIZE;
    const split_key_state_full_payload_t *p = (const split_key_state_full_payload_t *)payload;

    portENTER_CRITICAL(&s_lock);
    memcpy(s_remote, p->matrix, SPLIT_MATRIX_BYTES);
    s_changed = true;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGD(TAG, "rx FULL layer=%u", p->active_layer);
    return ESP_OK;
}

esp_err_t split_sync_on_key_state_delta(const uint8_t *payload, size_t len)
{
    if (len < sizeof(split_key_state_delta_payload_t)) return ESP_ERR_INVALID_SIZE;
    const split_key_state_delta_payload_t *p = (const split_key_state_delta_payload_t *)payload;

    uint16_t mask     = p->changed_mask;
    size_t   n_values = (size_t)__builtin_popcount(mask);

    if (len < sizeof(split_key_state_delta_payload_t) + n_values) return ESP_ERR_INVALID_SIZE;

    portENTER_CRITICAL(&s_lock);
    const uint8_t *v = p->values;
    for (int i = 0; i < SPLIT_MATRIX_BYTES; i++) {
        if (mask & (uint16_t)(1u << i)) {
            s_remote[i] = *v++;
        }
    }
    s_changed = true;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGD(TAG, "rx DELTA mask=0x%04X (%u bytes)", mask, (unsigned)n_values);
    return ESP_OK;
}

void split_sync_get_remote_matrix(uint8_t *out_bitmap)
{
    portENTER_CRITICAL(&s_lock);
    memcpy(out_bitmap, s_remote, SPLIT_MATRIX_BYTES);
    portEXIT_CRITICAL(&s_lock);
}

void split_sync_clear_remote_matrix(void)
{
    portENTER_CRITICAL(&s_lock);
    memset(s_remote, 0, SPLIT_MATRIX_BYTES);
    s_changed = true;
    portEXIT_CRITICAL(&s_lock);
}

bool split_sync_remote_matrix_changed(void)
{
    portENTER_CRITICAL(&s_lock);
    bool v    = s_changed;
    s_changed = false;
    portEXIT_CRITICAL(&s_lock);
    return v;
}

/* =========================================================================
 * SLAVE — send to master
 * ========================================================================= */

esp_err_t split_sync_send_full_state(const uint8_t *peer_mac,
                                      const uint8_t *matrix,
                                      uint8_t active_layer,
                                      uint64_t seq)
{
    split_key_state_full_payload_t p = {.active_layer = active_layer};
    memcpy(p.matrix, matrix, SPLIT_MATRIX_BYTES);

    esp_err_t ret = split_transport_send(peer_mac, SPLIT_PROTO_SPLIT,
                                         SPLIT_MSG_KEY_STATE_FULL, seq,
                                         (const uint8_t *)&p, sizeof(p));
    if (ret != ESP_OK) {
        // ESP_ERR_ESPNOW_NO_MEM usually means the ESP-NOW TX queue is full,
        // not heap exhaustion. Log both so we can distinguish.
        ESP_LOGW(TAG, "send FULL failed: %s | heap free=%lu int=%lu min=%lu | stack HWM=%lu B",
                 esp_err_to_name(ret),
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
                 (unsigned long)esp_get_minimum_free_heap_size(),
                 (unsigned long)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
    }
    return ret;
}

