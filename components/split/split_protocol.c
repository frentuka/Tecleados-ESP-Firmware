#include "split_protocol.h"

#include <string.h>

#include "esp_log.h"

#define TAG "SPLIT_PR"

size_t split_protocol_build_frame(uint8_t *out_buf, size_t out_max,
                                  uint8_t proto, uint8_t type, uint16_t seq,
                                  const uint8_t *payload, size_t payload_len)
{
    size_t total = SPLIT_FRAME_HEADER_SIZE + payload_len + SPLIT_FRAME_MIC_SIZE;
    if (total > out_max || payload_len > SPLIT_MAX_PAYLOAD) {
        return 0;
    }

    // Header (little-endian)
    split_frame_header_t *hdr = (split_frame_header_t *)out_buf;
    hdr->magic = SPLIT_FRAME_MAGIC;
    hdr->proto = proto;
    hdr->type  = type;
    hdr->seq   = seq;

    // Payload
    if (payload && payload_len > 0) {
        memcpy(out_buf + SPLIT_FRAME_HEADER_SIZE, payload, payload_len);
    }

    // MIC placeholder (zeroed until encryption layer fills it)
    memset(out_buf + SPLIT_FRAME_HEADER_SIZE + payload_len, 0, SPLIT_FRAME_MIC_SIZE);

    return total;
}

bool split_protocol_parse_frame(const uint8_t *data, size_t len,
                                split_frame_header_t *out_header,
                                const uint8_t **out_payload,
                                size_t *out_payload_len)
{
    if (!data || len < SPLIT_FRAME_OVERHEAD || !out_header) {
        return false;
    }

    // Parse header
    const split_frame_header_t *hdr = (const split_frame_header_t *)data;

    if (hdr->magic != SPLIT_FRAME_MAGIC) {
        ESP_LOGD(TAG, "bad magic: 0x%04X", hdr->magic);
        return false;
    }

    *out_header = *hdr;

    size_t payload_len = len - SPLIT_FRAME_OVERHEAD;

    if (out_payload) {
        *out_payload = (payload_len > 0) ? (data + SPLIT_FRAME_HEADER_SIZE) : NULL;
    }
    if (out_payload_len) {
        *out_payload_len = payload_len;
    }

    // TODO: MIC verification will be added with split_crypto

    return true;
}
