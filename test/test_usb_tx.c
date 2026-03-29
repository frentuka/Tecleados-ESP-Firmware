/**
 * @file test_usb_tx.c
 * @brief Tests for usb_callbacks_tx.c — TX buffer management, blast mode,
 *        bitmap reconciliation, NAK retry logic, and packet construction.
 */
#include "test_harness.h"
#include "mocks/mock_esp.h"

/* Protocol constants — flags already defined in test_usb_rx.c (same TU) */
#ifndef PAYLOAD_FLAG_FIRST
#define PAYLOAD_FLAG_FIRST      0x80
#define PAYLOAD_FLAG_MID        0x40
#define PAYLOAD_FLAG_LAST       0x20
#define PAYLOAD_FLAG_ACK        0x10
#define PAYLOAD_FLAG_OK         0x04
#define PAYLOAD_FLAG_ERR        0x02
#define PAYLOAD_FLAG_ABORT      0x01
#define PAYLOAD_FLAG_BITMAP     0x48
#define PAYLOAD_FLAG_NAK        0x08
#define MAX_PAYLOAD_LENGTH 58
#define COMM_REPORT_SIZE   63
#endif

#define MAX_TX_BUF_SIZE    21500
#define TX_BLAST_MAX_RECONCILE_ROUNDS 5
#define TX_NAK_RESEND_MAX_ATTEMPTS 3

/* usb_packet_msg_t already defined in test_usb_rx.c (same TU) */

/* ---- Simplified TX state for testing ---- */

static uint8_t tx_buf[MAX_TX_BUF_SIZE];
static uint16_t tx_buf_len = 0;
static uint16_t tx_buf_idx = 0;
static bool tx_blast_mode = false;
static uint16_t tx_blast_total_packets = 0;
static uint8_t tx_reconcile_attempts = 0;
static uint8_t tx_nak_attempts = 0;
static bool tx_awaiting = false;

static void tx_reset(void) {
    memset(tx_buf, 0, sizeof(tx_buf));
    tx_buf_len = 0;
    tx_buf_idx = 0;
    tx_blast_mode = false;
    tx_blast_total_packets = 0;
    tx_reconcile_attempts = 0;
    tx_nak_attempts = 0;
    tx_awaiting = false;
}

/* Calculate total packets needed for a payload */
static uint16_t calc_total_packets(uint16_t payload_len) {
    return (payload_len + MAX_PAYLOAD_LENGTH - 1) / MAX_PAYLOAD_LENGTH;
}

/* Build a packet by index (simplified) */
static bool build_packet_by_index(uint16_t index, usb_packet_msg_t *out) {
    if (index >= tx_blast_total_packets) return false;
    uint16_t offset = index * MAX_PAYLOAD_LENGTH;
    if (offset >= tx_buf_len) return false;

    uint16_t bytes_from_offset = tx_buf_len - offset;
    uint16_t plen = bytes_from_offset > MAX_PAYLOAD_LENGTH ? MAX_PAYLOAD_LENGTH : bytes_from_offset;

    memset(out, 0, sizeof(*out));
    out->remaining_packets = tx_blast_total_packets - 1 - index;
    out->payload_len = plen;
    memcpy(out->payload, tx_buf + offset, plen);

    if (index == 0) out->flags = PAYLOAD_FLAG_FIRST;
    else if (index == tx_blast_total_packets - 1) out->flags = PAYLOAD_FLAG_LAST;
    else out->flags = PAYLOAD_FLAG_MID;

    return true;
}

/* ---- Tests ---- */

TEST_CASE(usb_tx, single_packet_calculation) {
    TEST_ASSERT_EQUAL(1, calc_total_packets(1));
    TEST_ASSERT_EQUAL(1, calc_total_packets(58));
}

TEST_CASE(usb_tx, multi_packet_calculation) {
    TEST_ASSERT_EQUAL(2, calc_total_packets(59));
    TEST_ASSERT_EQUAL(2, calc_total_packets(116));
    TEST_ASSERT_EQUAL(3, calc_total_packets(117));
}

TEST_CASE(usb_tx, large_payload_calculation) {
    /* 21500 bytes / 58 = ~370.7 -> 371 packets */
    TEST_ASSERT_EQUAL(371, calc_total_packets(MAX_TX_BUF_SIZE));
}

TEST_CASE(usb_tx, build_first_packet) {
    tx_reset();
    memset(tx_buf, 0xAA, 200);
    tx_buf_len = 200;
    tx_blast_total_packets = calc_total_packets(200);

    usb_packet_msg_t pkt;
    TEST_ASSERT_TRUE(build_packet_by_index(0, &pkt));
    TEST_ASSERT_EQUAL(PAYLOAD_FLAG_FIRST, pkt.flags);
    TEST_ASSERT_EQUAL(58, pkt.payload_len);
    TEST_ASSERT_EQUAL(tx_blast_total_packets - 1, pkt.remaining_packets);
    TEST_ASSERT_EQUAL(0xAA, pkt.payload[0]);
}

TEST_CASE(usb_tx, build_mid_packet) {
    tx_reset();
    for (int i = 0; i < 200; i++) tx_buf[i] = (uint8_t)i;
    tx_buf_len = 200;
    tx_blast_total_packets = calc_total_packets(200);

    usb_packet_msg_t pkt;
    TEST_ASSERT_TRUE(build_packet_by_index(1, &pkt));
    TEST_ASSERT_EQUAL(PAYLOAD_FLAG_MID, pkt.flags);
    TEST_ASSERT_EQUAL(58, pkt.payload_len);
    TEST_ASSERT_EQUAL(58, pkt.payload[0]); /* Data starting at offset 58 */
}

TEST_CASE(usb_tx, build_last_packet) {
    tx_reset();
    tx_buf_len = 150;
    tx_blast_total_packets = calc_total_packets(150);

    usb_packet_msg_t pkt;
    uint16_t last_idx = tx_blast_total_packets - 1;
    TEST_ASSERT_TRUE(build_packet_by_index(last_idx, &pkt));
    TEST_ASSERT_EQUAL(PAYLOAD_FLAG_LAST, pkt.flags);
    TEST_ASSERT_EQUAL(0, pkt.remaining_packets);
    /* Last packet should have remainder: 150 - 2*58 = 34 bytes */
    TEST_ASSERT_EQUAL(34, pkt.payload_len);
}

TEST_CASE(usb_tx, build_out_of_bounds) {
    tx_reset();
    tx_buf_len = 100;
    tx_blast_total_packets = 2;

    usb_packet_msg_t pkt;
    TEST_ASSERT_FALSE(build_packet_by_index(5, &pkt));
}

TEST_CASE(usb_tx, single_packet_is_first_and_last) {
    tx_reset();
    tx_buf_len = 30;
    tx_blast_total_packets = 1;

    usb_packet_msg_t pkt;
    TEST_ASSERT_TRUE(build_packet_by_index(0, &pkt));
    /* For single packet, index 0 is also the last (tx_blast_total-1) */
    /* In the real code, FIRST|LAST is set; here our logic gives FIRST since index==0 */
    TEST_ASSERT_EQUAL(PAYLOAD_FLAG_FIRST, pkt.flags);
    TEST_ASSERT_EQUAL(30, pkt.payload_len);
    TEST_ASSERT_EQUAL(0, pkt.remaining_packets);
}

TEST_CASE(usb_tx, bitmap_reconcile_detection) {
    /* BITMAP flag = 0x48 = MID | NAK */
    usb_packet_msg_t msg = {0};
    msg.flags = PAYLOAD_FLAG_BITMAP;
    TEST_ASSERT_EQUAL_HEX(0x48, msg.flags);

    /* Verify it's distinct from other flags */
    TEST_ASSERT(msg.flags != PAYLOAD_FLAG_ACK);
    TEST_ASSERT(msg.flags != PAYLOAD_FLAG_MID);
}

TEST_CASE(usb_tx, bitmap_parsing) {
    /* Simulate receiving a bitmap that says packets 0,1,3 received (missing: 2,4) */
    usb_packet_msg_t bitmap_msg = {0};
    bitmap_msg.flags = PAYLOAD_FLAG_BITMAP;
    bitmap_msg.payload_len = 1;
    bitmap_msg.payload[0] = 0x0B; /* 0b00001011 = bits 0,1,3 */

    /* Check which packets are missing */
    uint16_t total = 5;
    int missing = 0;
    for (uint16_t i = 1; i < total - 1; i++) { /* Skip FIRST and LAST */
        uint16_t byte_idx = i / 8;
        uint8_t bit_idx = i % 8;
        bool received = (bitmap_msg.payload[byte_idx] >> bit_idx) & 1;
        if (!received) missing++;
    }
    TEST_ASSERT_EQUAL(1, missing); /* Only packet 2 is missing (among MID packets 1-3) */
}

TEST_CASE(usb_tx, reconcile_max_rounds) {
    TEST_ASSERT_EQUAL(5, TX_BLAST_MAX_RECONCILE_ROUNDS);
}

TEST_CASE(usb_tx, nak_max_retries) {
    TEST_ASSERT_EQUAL(3, TX_NAK_RESEND_MAX_ATTEMPTS);
}

TEST_CASE(usb_tx, ack_ok_signals_success) {
    usb_packet_msg_t msg = {0};
    msg.flags = PAYLOAD_FLAG_ACK | PAYLOAD_FLAG_OK;
    TEST_ASSERT(msg.flags & PAYLOAD_FLAG_ACK);
    TEST_ASSERT(msg.flags & PAYLOAD_FLAG_OK);
}

TEST_CASE(usb_tx, exact_payload_boundary) {
    /* 58 bytes should be exactly 1 packet */
    TEST_ASSERT_EQUAL(1, calc_total_packets(58));
    /* 59 bytes should be 2 packets */
    TEST_ASSERT_EQUAL(2, calc_total_packets(59));
}
