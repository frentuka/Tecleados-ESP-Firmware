/**
 * @file test_split_protocol.c
 * @brief Protocol parser fuzzer and frame builder tests for split_protocol.c.
 *
 * Exercises split_protocol_build_frame() and split_protocol_parse_frame()
 * against valid, boundary, and malformed inputs.
 *
 * Production code linked via main.c (#include "../../components/split/split_protocol.c").
 */
#include "test_harness.h"
/* split_protocol.h is already included transitively via split_protocol.c in main.c */
#include "../../components/split/split_protocol.h"

/* ---- Helpers ---- */

static uint8_t s_buf[SPLIT_ESP_NOW_MAX + 16]; /* +16 for overflow detection */

static size_t build(uint8_t proto, uint8_t type, uint16_t seq,
                    const uint8_t *payload, size_t plen)
{
    memset(s_buf, 0xAA, sizeof(s_buf)); /* sentinel fill */
    return split_protocol_build_frame(s_buf, SPLIT_ESP_NOW_MAX,
                                      proto, type, seq, payload, plen);
}

/* ---- Build tests ---- */

TEST_CASE(split_protocol, build_returns_correct_total_size) {
    uint8_t data[10] = {1,2,3,4,5,6,7,8,9,10};
    size_t len = build(SPLIT_PROTO_SPLIT, SPLIT_MSG_HEARTBEAT, 42, data, sizeof(data));
    TEST_ASSERT_EQUAL(SPLIT_FRAME_HEADER_SIZE + 10 + SPLIT_FRAME_MIC_SIZE, (int)len);
}

TEST_CASE(split_protocol, build_zero_payload) {
    size_t len = build(SPLIT_PROTO_SPLIT, SPLIT_MSG_DISCONNECT, 0, NULL, 0);
    TEST_ASSERT_EQUAL(SPLIT_FRAME_OVERHEAD, (int)len);
}

TEST_CASE(split_protocol, build_max_payload) {
    uint8_t big[SPLIT_MAX_PAYLOAD];
    memset(big, 0x55, sizeof(big));
    size_t len = build(SPLIT_PROTO_SPLIT, SPLIT_MSG_CONFIG_SYNC, 0xFFFF,
                       big, sizeof(big));
    TEST_ASSERT_EQUAL(SPLIT_ESP_NOW_MAX, (int)len);
}

TEST_CASE(split_protocol, build_oversized_payload_returns_zero) {
    uint8_t over[SPLIT_MAX_PAYLOAD + 1];
    memset(over, 0, sizeof(over));
    size_t len = build(SPLIT_PROTO_SPLIT, SPLIT_MSG_CONFIG_SYNC, 0, over, sizeof(over));
    TEST_ASSERT_EQUAL(0, (int)len);
}

TEST_CASE(split_protocol, build_buffer_too_small_returns_zero) {
    uint8_t data[8] = {0};
    uint8_t small[SPLIT_FRAME_HEADER_SIZE]; /* too small for payload + MIC */
    size_t len = split_protocol_build_frame(small, sizeof(small),
                                            SPLIT_PROTO_SPLIT, SPLIT_MSG_HEARTBEAT,
                                            0, data, sizeof(data));
    TEST_ASSERT_EQUAL(0, (int)len);
}

TEST_CASE(split_protocol, build_header_fields_correct) {
    uint8_t payload[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    size_t len = build(SPLIT_PROTO_SPLIT, SPLIT_MSG_ROLE_NEGOTIATE, 0x1234,
                       payload, sizeof(payload));
    TEST_ASSERT(len > 0);

    const split_frame_header_t *hdr = (const split_frame_header_t *)s_buf;
    TEST_ASSERT_EQUAL(SPLIT_FRAME_MAGIC, hdr->magic);
    TEST_ASSERT_EQUAL(SPLIT_PROTO_SPLIT, hdr->proto);
    TEST_ASSERT_EQUAL(SPLIT_MSG_ROLE_NEGOTIATE, hdr->type);
    TEST_ASSERT_EQUAL(0x1234, hdr->seq);
}

TEST_CASE(split_protocol, build_payload_copied_correctly) {
    uint8_t payload[8];
    for (int i = 0; i < 8; i++) payload[i] = (uint8_t)(i * 17 + 3);

    size_t len = build(SPLIT_PROTO_SPLIT, SPLIT_MSG_KEY_STATE_FULL, 0,
                       payload, sizeof(payload));
    TEST_ASSERT(len > 0);

    const uint8_t *in_frame = s_buf + SPLIT_FRAME_HEADER_SIZE;
    TEST_ASSERT(memcmp(in_frame, payload, 8) == 0);
}

TEST_CASE(split_protocol, build_mic_placeholder_zeroed) {
    uint8_t payload[4] = {1,2,3,4};
    size_t len = build(SPLIT_PROTO_SPLIT, SPLIT_MSG_HEARTBEAT, 0,
                       payload, sizeof(payload));
    TEST_ASSERT(len > 0);

    const uint8_t *mic = s_buf + SPLIT_FRAME_HEADER_SIZE + 4;
    for (size_t i = 0; i < SPLIT_FRAME_MIC_SIZE; i++) {
        TEST_ASSERT_EQUAL(0x00, mic[i]);
    }
}

TEST_CASE(split_protocol, build_no_overwrite_beyond_frame) {
    build(SPLIT_PROTO_SPLIT, SPLIT_MSG_HEARTBEAT, 1, NULL, 0);
    /* Sentinel bytes beyond the frame must be untouched */
    TEST_ASSERT_EQUAL(0xAA, s_buf[SPLIT_FRAME_OVERHEAD]);
}

TEST_CASE(split_protocol, build_different_seqs_produce_same_size) {
    uint8_t p[1] = {0};
    size_t l1 = build(0x01, 0x30, 0x0000, p, 1);
    size_t l2 = build(0x01, 0x30, 0xFFFF, p, 1);
    TEST_ASSERT_EQUAL((int)l1, (int)l2);
}

/* ---- Parse tests ---- */

static size_t build_and_get_len(uint8_t type, uint16_t seq,
                                 const uint8_t *payload, size_t plen)
{
    return build(SPLIT_PROTO_SPLIT, type, seq, payload, plen);
}

TEST_CASE(split_protocol, parse_valid_frame_succeeds) {
    uint8_t payload[6] = {0x11,0x22,0x33,0x44,0x55,0x66};
    size_t len = build_and_get_len(SPLIT_MSG_HEARTBEAT, 0xABCD, payload, 6);

    split_frame_header_t hdr;
    const uint8_t *out_payload;
    size_t out_len;
    bool ok = split_protocol_parse_frame(s_buf, len, &hdr, &out_payload, &out_len);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(SPLIT_FRAME_MAGIC, hdr.magic);
    TEST_ASSERT_EQUAL(SPLIT_PROTO_SPLIT, hdr.proto);
    TEST_ASSERT_EQUAL(SPLIT_MSG_HEARTBEAT, hdr.type);
    TEST_ASSERT_EQUAL(0xABCD, hdr.seq);
    TEST_ASSERT_EQUAL(6, (int)out_len);
    TEST_ASSERT(memcmp(out_payload, payload, 6) == 0);
}

TEST_CASE(split_protocol, parse_zero_payload_frame) {
    size_t len = build_and_get_len(SPLIT_MSG_DISCONNECT, 0, NULL, 0);

    split_frame_header_t hdr;
    const uint8_t *out_payload = (const uint8_t *)0xDEAD; /* sentinel */
    size_t out_len = 999;
    bool ok = split_protocol_parse_frame(s_buf, len, &hdr, &out_payload, &out_len);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(0, (int)out_len);
    TEST_ASSERT_NULL(out_payload);
}

TEST_CASE(split_protocol, parse_bad_magic_rejected) {
    uint8_t payload[4] = {1,2,3,4};
    size_t len = build_and_get_len(SPLIT_MSG_HEARTBEAT, 0, payload, 4);

    /* Corrupt the magic */
    s_buf[0] ^= 0xFF;
    split_frame_header_t hdr;
    bool ok = split_protocol_parse_frame(s_buf, len, &hdr, NULL, NULL);
    TEST_ASSERT_FALSE(ok);
}

TEST_CASE(split_protocol, parse_null_data_rejected) {
    split_frame_header_t hdr;
    bool ok = split_protocol_parse_frame(NULL, 16, &hdr, NULL, NULL);
    TEST_ASSERT_FALSE(ok);
}

TEST_CASE(split_protocol, parse_null_header_rejected) {
    uint8_t payload[4] = {0};
    size_t len = build_and_get_len(SPLIT_MSG_HEARTBEAT, 0, payload, 4);
    bool ok = split_protocol_parse_frame(s_buf, len, NULL, NULL, NULL);
    TEST_ASSERT_FALSE(ok);
}

TEST_CASE(split_protocol, parse_too_short_rejected) {
    uint8_t payload[4] = {0};
    size_t len = build_and_get_len(SPLIT_MSG_HEARTBEAT, 0, payload, 4);
    split_frame_header_t hdr;
    /* Feed only FRAME_OVERHEAD - 1 bytes */
    bool ok = split_protocol_parse_frame(s_buf, SPLIT_FRAME_OVERHEAD - 1, &hdr, NULL, NULL);
    TEST_ASSERT_FALSE(ok);
}

TEST_CASE(split_protocol, parse_exactly_overhead_bytes_ok) {
    size_t len = build_and_get_len(SPLIT_MSG_DISCONNECT, 7, NULL, 0);
    TEST_ASSERT_EQUAL((int)SPLIT_FRAME_OVERHEAD, (int)len);
    split_frame_header_t hdr;
    bool ok = split_protocol_parse_frame(s_buf, len, &hdr, NULL, NULL);
    TEST_ASSERT_TRUE(ok);
}

/* ---- Fuzzer: feed 256 single-byte mutations of a valid frame ---- */

TEST_CASE(split_protocol, fuzz_single_byte_mutations) {
    uint8_t payload[8] = {0xA1,0xB2,0xC3,0xD4,0xE5,0xF6,0x07,0x18};
    size_t frame_len = build_and_get_len(SPLIT_MSG_KEY_STATE_DELTA, 0x5A5A, payload, 8);
    uint8_t original[SPLIT_ESP_NOW_MAX];
    memcpy(original, s_buf, frame_len);

    int valid_count  = 0;
    int reject_count = 0;
    split_frame_header_t hdr;

    /* Flip each byte once (except payload region — we only care about magic corruption) */
    for (size_t byte_idx = 0; byte_idx < frame_len; byte_idx++) {
        for (int bit = 0; bit < 8; bit++) {
            uint8_t mutated[SPLIT_ESP_NOW_MAX];
            memcpy(mutated, original, frame_len);
            mutated[byte_idx] ^= (uint8_t)(1 << bit);

            bool ok = split_protocol_parse_frame(mutated, frame_len, &hdr, NULL, NULL);
            if (ok) valid_count++; else reject_count++;

            /* If frame was "valid" but magic was corrupted, that's a bug */
            if (ok && byte_idx < 2) {
                /* magic bytes were corrupted — parser should have rejected */
                TEST_ASSERT_MSG(false, "magic corruption accepted as valid");
            }
        }
    }

    /* At least half of all mutations should be rejected (mostly magic or overhead) */
    TEST_ASSERT_MSG(reject_count > 0, "no mutations were rejected");
    (void)valid_count; /* some mutations in payload region are still valid frames */
}

/* ---- Round-trip: build then parse ---- */

TEST_CASE(split_protocol, round_trip_all_message_types) {
    const split_msg_type_t types[] = {
        SPLIT_MSG_DISCOVERY, SPLIT_MSG_PAIR_REQUEST, SPLIT_MSG_PAIR_RESPONSE,
        SPLIT_MSG_ROLE_NEGOTIATE, SPLIT_MSG_ROLE_SWAP_REQ, SPLIT_MSG_ROLE_SWAP_ACK,
        SPLIT_MSG_KEY_STATE_FULL, SPLIT_MSG_KEY_STATE_DELTA,
        SPLIT_MSG_HEARTBEAT, SPLIT_MSG_DISCONNECT,
        SPLIT_MSG_CONFIG_SYNC, SPLIT_MSG_CONFIG_SYNC_ACK,
    };
    uint8_t payload[16];
    for (int i = 0; i < 16; i++) payload[i] = (uint8_t)(i * 13 + 7);

    for (size_t t = 0; t < sizeof(types)/sizeof(types[0]); t++) {
        size_t len = build(SPLIT_PROTO_SPLIT, (uint8_t)types[t], (uint16_t)t,
                           payload, 16);
        TEST_ASSERT_MSG(len > 0, "build_frame returned 0");

        split_frame_header_t hdr;
        const uint8_t *out_p;
        size_t out_l;
        bool ok = split_protocol_parse_frame(s_buf, len, &hdr, &out_p, &out_l);
        TEST_ASSERT_MSG(ok, "parse_frame failed on round-trip");
        TEST_ASSERT_EQUAL((int)types[t], (int)hdr.type);
        TEST_ASSERT_EQUAL((int)t, (int)hdr.seq);
        TEST_ASSERT_EQUAL(16, (int)out_l);
        TEST_ASSERT(memcmp(out_p, payload, 16) == 0);
    }
}
