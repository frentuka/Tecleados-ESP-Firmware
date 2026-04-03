#include "split_sync.h"
#include "split_transport.h"

#include <string.h>

#include "esp_log.h"
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
    // Minimum: active_layer(1) + changed_mask(2) = 3 bytes
    if (len < 3) return ESP_ERR_INVALID_SIZE;
    const split_key_state_delta_payload_t *p = (const split_key_state_delta_payload_t *)payload;

    uint16_t mask     = p->changed_mask;
    size_t   n_values = (size_t)__builtin_popcount(mask);

    if (len < 3 + n_values) return ESP_ERR_INVALID_SIZE;

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
    bool v  = s_changed;
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
                                      uint16_t *tx_seq)
{
    split_key_state_full_payload_t p = {.active_layer = active_layer};
    memcpy(p.matrix, matrix, SPLIT_MATRIX_BYTES);

    esp_err_t ret = split_transport_send(peer_mac, SPLIT_PROTO_SPLIT,
                                         SPLIT_MSG_KEY_STATE_FULL,
                                         (*tx_seq)++,
                                         (const uint8_t *)&p, sizeof(p));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "send FULL failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t split_sync_send_delta(const uint8_t *peer_mac,
                                 const uint8_t *old_matrix,
                                 const uint8_t *new_matrix,
                                 uint8_t active_layer,
                                 uint16_t *tx_seq)
{
    // Buffer: active_layer(1) + changed_mask(2) + up to SPLIT_MATRIX_BYTES values
    uint8_t buf[3 + SPLIT_MATRIX_BYTES];
    buf[0] = active_layer;

    uint16_t mask     = 0;
    uint8_t  n_values = 0;
    uint8_t *values   = buf + 3;

    for (int i = 0; i < SPLIT_MATRIX_BYTES; i++) {
        if (old_matrix[i] != new_matrix[i]) {
            mask |= (uint16_t)(1u << i);
            values[n_values++] = new_matrix[i];
        }
    }

    // Store mask (little-endian, matches packed uint16_t)
    buf[1] = (uint8_t)(mask & 0xFF);
    buf[2] = (uint8_t)(mask >> 8);

    size_t payload_len = 3 + n_values;
    esp_err_t ret = split_transport_send(peer_mac, SPLIT_PROTO_SPLIT,
                                         SPLIT_MSG_KEY_STATE_DELTA,
                                         (*tx_seq)++,
                                         buf, payload_len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "send DELTA failed: %s", esp_err_to_name(ret));
    }
    return ret;
}
