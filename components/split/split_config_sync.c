#include "split_config_sync.h"
#include "split_protocol.h"
#include "split_transport.h"

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "cfg_storage_keys.h"

#define SPLIT_CFG_SEND_RETRIES  3      // Max attempts per fragment
#define SPLIT_CFG_RETRY_DELAY_MS 10    // Delay between retries

#define TAG "SPLIT_CFG"

/* =========================================================================
 * Sync table — (kind, NVS key) pairs pushed to slave on connect / config update
 * ========================================================================= */

const split_sync_entry_t SPLIT_SYNC_ENTRIES[] = {
    { CFGMOD_KIND_LAYOUT,  CFG_ST_LAYER_0 },
    { CFGMOD_KIND_LAYOUT,  CFG_ST_LAYER_1 },
    { CFGMOD_KIND_LAYOUT,  CFG_ST_LAYER_2 },
    { CFGMOD_KIND_LAYOUT,  CFG_ST_LAYER_3 },
    { CFGMOD_KIND_SYSTEM,  "system"        },
    { CFGMOD_KIND_PHYSICAL, CFG_ST_PHYSICAL_LAYOUT },
};

const size_t SPLIT_SYNC_ENTRY_COUNT =
    sizeof(SPLIT_SYNC_ENTRIES) / sizeof(SPLIT_SYNC_ENTRIES[0]);

/* =========================================================================
 * MASTER — send fragments for one (kind, key) blob
 * ========================================================================= */

esp_err_t split_config_sync_push(const uint8_t *peer_mac, uint16_t *tx_seq,
                                  cfgmod_kind_t kind, const char *key)
{
    if (!peer_mac || !tx_seq || !key) return ESP_ERR_INVALID_ARG;

    // Read raw blob from NVS
    uint8_t *blob = malloc(SPLIT_CONFIG_SYNC_DATA_MAX * 255); // max ~57 KB, but only alloc needed
    if (!blob) return ESP_ERR_NO_MEM;

    // First pass: find out required size
    size_t blob_len = SPLIT_CONFIG_SYNC_DATA_MAX * 255;
    esp_err_t ret = cfgmod_read_storage(kind, key, blob, &blob_len);
    if (ret != ESP_OK) {
        if (ret != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "read_storage(%u, %s): %s — skipping", kind, key, esp_err_to_name(ret));
        }
        free(blob);
        return ESP_OK; // Not an error — entry may not yet be written on a fresh device
    }

    if (blob_len == 0) {
        free(blob);
        return ESP_OK;
    }

    uint8_t total_fragments = (uint8_t)((blob_len + SPLIT_CONFIG_SYNC_DATA_MAX - 1)
                                         / SPLIT_CONFIG_SYNC_DATA_MAX);
    if (total_fragments == 0) total_fragments = 1;

    ESP_LOGD(TAG, "pushing kind=%u key=%s blob_len=%u fragments=%u",
             kind, key, (unsigned)blob_len, total_fragments);

    // Frame buffer: header + data
    uint8_t frame[SPLIT_CONFIG_SYNC_HDR_SIZE + SPLIT_CONFIG_SYNC_DATA_MAX];

    for (uint8_t idx = 0; idx < total_fragments; idx++) {
        size_t offset    = (size_t)idx * SPLIT_CONFIG_SYNC_DATA_MAX;
        size_t chunk_len = blob_len - offset;
        if (chunk_len > SPLIT_CONFIG_SYNC_DATA_MAX) chunk_len = SPLIT_CONFIG_SYNC_DATA_MAX;

        split_config_sync_payload_t *hdr = (split_config_sync_payload_t *)frame;
        hdr->kind           = (uint8_t)kind;
        hdr->fragment_idx   = idx;
        hdr->fragment_total = total_fragments;
        memset(hdr->key, 0, SPLIT_CONFIG_SYNC_KEY_LEN);
        strncpy((char *)hdr->key, key, SPLIT_CONFIG_SYNC_KEY_LEN - 1);
        memcpy(frame + SPLIT_CONFIG_SYNC_HDR_SIZE, blob + offset, chunk_len);

        ret = ESP_FAIL;
        for (int attempt = 0; attempt < SPLIT_CFG_SEND_RETRIES; attempt++) {
            ret = split_transport_send(peer_mac, SPLIT_PROTO_SPLIT,
                                       SPLIT_MSG_CONFIG_SYNC, (*tx_seq)++,
                                       frame, SPLIT_CONFIG_SYNC_HDR_SIZE + chunk_len);
            if (ret == ESP_OK) break;
            ESP_LOGW(TAG, "fragment %u/%u send attempt %d/%d failed: %s",
                     idx + 1, total_fragments, attempt + 1, SPLIT_CFG_SEND_RETRIES,
                     esp_err_to_name(ret));
            if (attempt + 1 < SPLIT_CFG_SEND_RETRIES) {
                vTaskDelay(pdMS_TO_TICKS(SPLIT_CFG_RETRY_DELAY_MS));
            }
        }
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "fragment %u/%u permanently failed after %d attempts",
                     idx + 1, total_fragments, SPLIT_CFG_SEND_RETRIES);
            free(blob);
            return ret;
        }
    }

    free(blob);
    return ESP_OK;
}

esp_err_t split_config_sync_push_all(const uint8_t *peer_mac, uint16_t *tx_seq)
{
    ESP_LOGI(TAG, "pushing %u config entries to slave", (unsigned)SPLIT_SYNC_ENTRY_COUNT);
    for (size_t i = 0; i < SPLIT_SYNC_ENTRY_COUNT; i++) {
        esp_err_t ret = split_config_sync_push(peer_mac, tx_seq,
                                                SPLIT_SYNC_ENTRIES[i].kind,
                                                SPLIT_SYNC_ENTRIES[i].key);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "push entry %u failed: %s", (unsigned)i, esp_err_to_name(ret));
            // Continue — best-effort sync
        }
    }
    return ESP_OK;
}

void split_config_sync_on_ack(const uint8_t *payload, size_t len)
{
    if (len < sizeof(split_config_sync_ack_payload_t)) return;
    const split_config_sync_ack_payload_t *ack = (const split_config_sync_ack_payload_t *)payload;

    char key[SPLIT_CONFIG_SYNC_KEY_LEN + 1];
    memcpy(key, ack->key, SPLIT_CONFIG_SYNC_KEY_LEN);
    key[SPLIT_CONFIG_SYNC_KEY_LEN] = '\0';

    if (ack->status == 0) {
        ESP_LOGD(TAG, "slave ACK'd kind=%u key=%s", ack->kind, key);
    } else {
        ESP_LOGW(TAG, "slave NAK'd kind=%u key=%s status=%u", ack->kind, key, ack->status);
    }
}

/* =========================================================================
 * SLAVE — reassembly
 * ========================================================================= */

typedef struct {
    bool      active;
    uint8_t   kind;
    char      key[SPLIT_CONFIG_SYNC_KEY_LEN + 1];
    uint8_t   total;
    uint8_t   received;        // bitmask of received fragment indices (max 8 for now)
    uint8_t  *buf;
    size_t    buf_len;         // allocated size
    size_t    data_len;        // total expected data length (known after first fragment)
} reassembly_t;

static reassembly_t s_rx = {0};

void split_config_sync_reset(void)
{
    if (s_rx.buf) {
        free(s_rx.buf);
        s_rx.buf = NULL;
    }
    memset(&s_rx, 0, sizeof(s_rx));
}

esp_err_t split_config_sync_on_fragment(const uint8_t *src_mac,
                                         const uint8_t *payload, size_t len,
                                         const uint8_t *reply_mac,
                                         uint16_t *tx_seq)
{
    (void)src_mac;
    if (len < SPLIT_CONFIG_SYNC_HDR_SIZE) return ESP_ERR_INVALID_SIZE;

    const split_config_sync_payload_t *frag = (const split_config_sync_payload_t *)payload;
    size_t data_len = len - SPLIT_CONFIG_SYNC_HDR_SIZE;

    char key[SPLIT_CONFIG_SYNC_KEY_LEN + 1];
    memcpy(key, frag->key, SPLIT_CONFIG_SYNC_KEY_LEN);
    key[SPLIT_CONFIG_SYNC_KEY_LEN] = '\0';

    // New transfer or different (kind, key) → reset reassembly
    if (!s_rx.active || s_rx.kind != frag->kind ||
        strncmp(s_rx.key, key, SPLIT_CONFIG_SYNC_KEY_LEN) != 0 ||
        s_rx.total != frag->fragment_total) {
        split_config_sync_reset();
        s_rx.kind     = frag->kind;
        s_rx.total    = frag->fragment_total;
        s_rx.received = 0;
        s_rx.active   = true;
        memcpy(s_rx.key, key, sizeof(s_rx.key));

        // Allocate buffer for the full blob
        s_rx.buf_len = (size_t)frag->fragment_total * SPLIT_CONFIG_SYNC_DATA_MAX;
        s_rx.buf     = malloc(s_rx.buf_len);
        if (!s_rx.buf) {
            s_rx.active = false;
            return ESP_ERR_NO_MEM;
        }
        s_rx.data_len = 0;
    }

    if (frag->fragment_idx >= frag->fragment_total) {
        ESP_LOGW(TAG, "bad fragment idx %u / total %u", frag->fragment_idx, frag->fragment_total);
        return ESP_ERR_INVALID_ARG;
    }

    // Copy fragment data into reassembly buffer
    size_t offset = (size_t)frag->fragment_idx * SPLIT_CONFIG_SYNC_DATA_MAX;
    if (offset + data_len > s_rx.buf_len) {
        ESP_LOGW(TAG, "fragment overflows buffer");
        split_config_sync_reset();
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(s_rx.buf + offset, payload + SPLIT_CONFIG_SYNC_HDR_SIZE, data_len);
    s_rx.received |= (uint8_t)(1u << frag->fragment_idx);

    // Track total data length from the last fragment
    if (frag->fragment_idx == frag->fragment_total - 1) {
        s_rx.data_len = offset + data_len;
    }

    ESP_LOGD(TAG, "rx fragment %u/%u kind=%u key=%s",
             frag->fragment_idx + 1, frag->fragment_total, frag->kind, key);

    // Check if all fragments received
    uint8_t full_mask = (uint8_t)((1u << frag->fragment_total) - 1u);
    if ((s_rx.received & full_mask) != full_mask || s_rx.data_len == 0) {
        return ESP_OK; // still waiting for more fragments
    }

    // All fragments received — write to NVS
    esp_err_t ret = cfgmod_write_storage((cfgmod_kind_t)s_rx.kind, s_rx.key,
                                          s_rx.buf, s_rx.data_len);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "config sync applied kind=%u key=%s (%u bytes)",
                 s_rx.kind, s_rx.key, (unsigned)s_rx.data_len);
    } else {
        ESP_LOGW(TAG, "NVS write failed for kind=%u key=%s: %s",
                 s_rx.kind, s_rx.key, esp_err_to_name(ret));
    }

    // Send ACK
    if (reply_mac && tx_seq) {
        split_config_sync_ack_payload_t ack = {
            .kind   = s_rx.kind,
            .status = (ret == ESP_OK) ? 0 : 1,
        };
        memcpy(ack.key, s_rx.key, SPLIT_CONFIG_SYNC_KEY_LEN);
        split_transport_send(reply_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_CONFIG_SYNC_ACK,
                             (*tx_seq)++, (const uint8_t *)&ack, sizeof(ack));
    }

    split_config_sync_reset();
    return ret;
}
