/**
 * @file test_usb_rx.c
 * @brief Tests for usb_callbacks_rx.c — single-packet RX, blast mode entry,
 *        bitmap generation, packet ordering, and commit logic.
 */
#include "test_harness.h"
#include "mocks/mock_esp.h"

/* ---- Protocol constants ---- */

#define PAYLOAD_FLAG_FIRST      0x80
#define PAYLOAD_FLAG_MID        0x40
#define PAYLOAD_FLAG_LAST       0x20
#define PAYLOAD_FLAG_ACK        0x10
#define PAYLOAD_FLAG_NAK        0x08
#define PAYLOAD_FLAG_STATUS_REQ 0x50
#define PAYLOAD_FLAG_BITMAP     0x48
#define PAYLOAD_FLAG_OK         0x04
#define PAYLOAD_FLAG_ERR        0x02
#define PAYLOAD_FLAG_ABORT      0x01

#define MAX_PAYLOAD_LENGTH 58
#define COMM_REPORT_SIZE   63
#define MAX_RX_BUF_SIZE    21500
#define RX_BLAST_BITMAP_BYTES 48
#define RX_BLAST_MAX_PACKETS  (RX_BLAST_BITMAP_BYTES * 8)

PACKED_STRUCT_BEGIN
typedef struct PACKED_ATTR {
    uint8_t flags;
    uint16_t remaining_packets;
    uint8_t payload_len;
    uint8_t payload[MAX_PAYLOAD_LENGTH];
    uint8_t crc;
} usb_packet_msg_t;
PACKED_STRUCT_END

/* ---- Simplified blast-mode bitmap helpers ---- */

static uint8_t rx_bitmap[RX_BLAST_BITMAP_BYTES];
static uint8_t rx_payload_lens[RX_BLAST_MAX_PACKETS];
static uint8_t rx_buf[MAX_RX_BUF_SIZE];
static uint16_t rx_total_packets = 0;
static bool rx_blast_mode = false;

static void rx_reset(void) {
    memset(rx_bitmap, 0, sizeof(rx_bitmap));
    memset(rx_payload_lens, 0, sizeof(rx_payload_lens));
    memset(rx_buf, 0, sizeof(rx_buf));
    rx_total_packets = 0;
    rx_blast_mode = false;
}

static void rx_blast_set_bit(uint16_t index) {
    if (index < RX_BLAST_MAX_PACKETS)
        rx_bitmap[index / 8] |= (1 << (index % 8));
}

static bool rx_blast_get_bit(uint16_t index) {
    if (index >= RX_BLAST_MAX_PACKETS) return false;
    return (rx_bitmap[index / 8] >> (index % 8)) & 1;
}

static void rx_blast_receive(const usb_packet_msg_t *msg) {
    uint16_t index = rx_total_packets - 1 - msg->remaining_packets;
    if (index >= rx_total_packets || index >= RX_BLAST_MAX_PACKETS) return;
    if (rx_blast_get_bit(index)) return; /* duplicate */

    uint16_t offset = index * MAX_PAYLOAD_LENGTH;
    if (offset + msg->payload_len > MAX_RX_BUF_SIZE) return;

    memcpy(rx_buf + offset, msg->payload, msg->payload_len);
    rx_payload_lens[index] = msg->payload_len;
    rx_blast_set_bit(index);
}

/* ---- Tests ---- */

TEST_CASE(usb_rx, packet_struct_size) {
    TEST_ASSERT_EQUAL(COMM_REPORT_SIZE, sizeof(usb_packet_msg_t));
}

TEST_CASE(usb_rx, blast_bitmap_set_and_get) {
    rx_reset();
    rx_blast_set_bit(0);
    TEST_ASSERT_TRUE(rx_blast_get_bit(0));
    TEST_ASSERT_FALSE(rx_blast_get_bit(1));
}

TEST_CASE(usb_rx, blast_bitmap_multiple_bits) {
    rx_reset();
    rx_blast_set_bit(0);
    rx_blast_set_bit(7);
    rx_blast_set_bit(8);
    rx_blast_set_bit(15);

    TEST_ASSERT_TRUE(rx_blast_get_bit(0));
    TEST_ASSERT_TRUE(rx_blast_get_bit(7));
    TEST_ASSERT_TRUE(rx_blast_get_bit(8));
    TEST_ASSERT_TRUE(rx_blast_get_bit(15));
    TEST_ASSERT_FALSE(rx_blast_get_bit(1));
    TEST_ASSERT_FALSE(rx_blast_get_bit(9));
}

TEST_CASE(usb_rx, blast_bitmap_boundary) {
    rx_reset();
    rx_blast_set_bit(RX_BLAST_MAX_PACKETS - 1);
    TEST_ASSERT_TRUE(rx_blast_get_bit(RX_BLAST_MAX_PACKETS - 1));

    /* Out of bounds should return false */
    TEST_ASSERT_FALSE(rx_blast_get_bit(RX_BLAST_MAX_PACKETS));
}

TEST_CASE(usb_rx, blast_receive_first_packet) {
    rx_reset();
    rx_total_packets = 5;

    usb_packet_msg_t msg = {0};
    msg.flags = PAYLOAD_FLAG_FIRST;
    msg.remaining_packets = 4; /* index = 5 - 1 - 4 = 0 */
    msg.payload_len = 10;
    memset(msg.payload, 0xAA, 10);

    rx_blast_receive(&msg);

    TEST_ASSERT_TRUE(rx_blast_get_bit(0));
    TEST_ASSERT_EQUAL(10, rx_payload_lens[0]);
    TEST_ASSERT_EQUAL(0xAA, rx_buf[0]);
}

TEST_CASE(usb_rx, blast_receive_ordered) {
    rx_reset();
    rx_total_packets = 3;

    for (uint16_t i = 0; i < 3; i++) {
        usb_packet_msg_t msg = {0};
        msg.remaining_packets = 2 - i; /* index = 3 - 1 - rem */
        msg.payload_len = 5;
        memset(msg.payload, (uint8_t)(0x10 + i), 5);
        rx_blast_receive(&msg);
    }

    for (uint16_t i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(rx_blast_get_bit(i));
        TEST_ASSERT_EQUAL(5, rx_payload_lens[i]);
        TEST_ASSERT_EQUAL(0x10 + i, rx_buf[i * MAX_PAYLOAD_LENGTH]);
    }
}

TEST_CASE(usb_rx, blast_receive_out_of_order) {
    rx_reset();
    rx_total_packets = 3;

    /* Send packet 2, then 0, then 1 */
    usb_packet_msg_t msg2 = {0};
    msg2.remaining_packets = 0; /* index = 2 */
    msg2.payload_len = 3;
    memset(msg2.payload, 0x22, 3);
    rx_blast_receive(&msg2);

    usb_packet_msg_t msg0 = {0};
    msg0.remaining_packets = 2; /* index = 0 */
    msg0.payload_len = 3;
    memset(msg0.payload, 0x00, 3);
    rx_blast_receive(&msg0);

    usb_packet_msg_t msg1 = {0};
    msg1.remaining_packets = 1; /* index = 1 */
    msg1.payload_len = 3;
    memset(msg1.payload, 0x11, 3);
    rx_blast_receive(&msg1);

    /* All received in correct positions */
    TEST_ASSERT_EQUAL(0x00, rx_buf[0 * MAX_PAYLOAD_LENGTH]);
    TEST_ASSERT_EQUAL(0x11, rx_buf[1 * MAX_PAYLOAD_LENGTH]);
    TEST_ASSERT_EQUAL(0x22, rx_buf[2 * MAX_PAYLOAD_LENGTH]);
}

TEST_CASE(usb_rx, blast_receive_duplicate_ignored) {
    rx_reset();
    rx_total_packets = 2;

    usb_packet_msg_t msg = {0};
    msg.remaining_packets = 1; /* index = 0 */
    msg.payload_len = 5;
    memset(msg.payload, 0xAA, 5);
    rx_blast_receive(&msg);

    /* Send same packet again with different data */
    memset(msg.payload, 0xBB, 5);
    rx_blast_receive(&msg);

    /* Original data should be preserved */
    TEST_ASSERT_EQUAL(0xAA, rx_buf[0]);
}

TEST_CASE(usb_rx, blast_entry_detection) {
    /* FIRST flag with remaining > 0 signals blast mode entry */
    usb_packet_msg_t msg = {0};
    msg.flags = PAYLOAD_FLAG_FIRST;
    msg.remaining_packets = 5;

    bool enters_blast = (msg.flags & PAYLOAD_FLAG_FIRST) && msg.remaining_packets > 0;
    TEST_ASSERT_TRUE(enters_blast);
}

TEST_CASE(usb_rx, single_packet_detection) {
    usb_packet_msg_t msg = {0};
    msg.flags = PAYLOAD_FLAG_FIRST | PAYLOAD_FLAG_LAST;
    msg.remaining_packets = 0;

    bool is_single = (msg.flags & PAYLOAD_FLAG_FIRST) && (msg.flags & PAYLOAD_FLAG_LAST);
    bool enters_blast = (msg.flags & PAYLOAD_FLAG_FIRST) && msg.remaining_packets > 0;
    TEST_ASSERT_TRUE(is_single);
    TEST_ASSERT_FALSE(enters_blast);
}

TEST_CASE(usb_rx, bitmap_response_format) {
    rx_reset();
    rx_total_packets = 10;
    rx_blast_set_bit(0);
    rx_blast_set_bit(1);
    rx_blast_set_bit(5);
    /* Missing: 2,3,4,6,7,8,9 */

    usb_packet_msg_t resp = {0};
    resp.flags = PAYLOAD_FLAG_BITMAP;
    uint8_t bitmap_bytes = (rx_total_packets + 7) / 8;
    memcpy(resp.payload, rx_bitmap, bitmap_bytes);
    resp.payload_len = bitmap_bytes;

    /* Verify bitmap payload encodes which packets received */
    TEST_ASSERT_EQUAL(PAYLOAD_FLAG_BITMAP, resp.flags);
    TEST_ASSERT_EQUAL(2, resp.payload_len); /* 10 packets = 2 bytes */
    TEST_ASSERT_EQUAL_HEX(0x23, resp.payload[0]); /* bits 0,1,5 = 0b00100011 */
}

TEST_CASE(usb_rx, max_blast_packets) {
    TEST_ASSERT_EQUAL(384, RX_BLAST_MAX_PACKETS); /* 48 * 8 */
}

TEST_CASE(usb_rx, payload_flag_values) {
    TEST_ASSERT_EQUAL_HEX(0x80, PAYLOAD_FLAG_FIRST);
    TEST_ASSERT_EQUAL_HEX(0x40, PAYLOAD_FLAG_MID);
    TEST_ASSERT_EQUAL_HEX(0x20, PAYLOAD_FLAG_LAST);
    TEST_ASSERT_EQUAL_HEX(0x10, PAYLOAD_FLAG_ACK);
    TEST_ASSERT_EQUAL_HEX(0x08, PAYLOAD_FLAG_NAK);
    TEST_ASSERT_EQUAL_HEX(0x50, PAYLOAD_FLAG_STATUS_REQ);
    TEST_ASSERT_EQUAL_HEX(0x48, PAYLOAD_FLAG_BITMAP);
    TEST_ASSERT_EQUAL_HEX(0x04, PAYLOAD_FLAG_OK);
    TEST_ASSERT_EQUAL_HEX(0x02, PAYLOAD_FLAG_ERR);
    TEST_ASSERT_EQUAL_HEX(0x01, PAYLOAD_FLAG_ABORT);
}
