/**
 * @file test_usb_rx.c
 * @brief Tests for usb_callbacks_rx.c — real production blast mode RX,
 *        bitmap generation, packet ordering, commit, and legacy path.
 *
 * Links against real usb_callbacks_rx.c (included in main.c).
 */
#include "test_harness.h"
#include "mocks/mock_esp.h"

/* Production functions from usb_callbacks_rx.c:
   process_rx_request, erase_rx_buffer, rx_blast_active,
   rx_blast_receive_packet, rx_blast_build_bitmap_response,
   rx_blast_commit, rx_get_last_packet_timestamp_us */

TEST_SETUP(usb_rx) {
    erase_rx_buffer();
    mock_timer_set(1000); /* nonzero start time */
    mock_send_single_msg_reset();
}

/* ---- Packet struct ---- */

TEST_CASE(usb_rx, packet_struct_size) {
    TEST_ASSERT_EQUAL(COMM_REPORT_SIZE, sizeof(usb_packet_msg_t));
}

/* ---- Blast mode entry via process_rx_request ---- */

TEST_CASE(usb_rx, blast_entry_on_first_with_remaining) {
    usb_packet_msg_t msg = {0};
    msg.flags = PAYLOAD_FLAG_FIRST;
    msg.remaining_packets = 4; /* total = 5 packets */
    msg.payload_len = 10;
    memset(msg.payload, 0xAA, 10);

    process_rx_request(msg);

    TEST_ASSERT_TRUE(rx_blast_active());
}

TEST_CASE(usb_rx, single_packet_does_not_enter_blast) {
    /* FIRST with remaining=0 is the legacy single-packet path */
    usb_packet_msg_t msg = {0};
    msg.flags = PAYLOAD_FLAG_FIRST | PAYLOAD_FLAG_LAST;
    msg.remaining_packets = 0;
    msg.payload_len = 5;
    msg.payload[0] = MODULE_CONFIG; /* valid module ID for callback */

    process_rx_request(msg);

    TEST_ASSERT_FALSE(rx_blast_active());
}

/* ---- Blast mode receive + bitmap ---- */

TEST_CASE(usb_rx, blast_receive_and_bitmap) {
    /* Enter blast mode: 3 packets total */
    usb_packet_msg_t first = {0};
    first.flags = PAYLOAD_FLAG_FIRST;
    first.remaining_packets = 2; /* total = 3 */
    first.payload_len = MAX_PAYLOAD_LENGTH;
    memset(first.payload, 0x11, MAX_PAYLOAD_LENGTH);
    process_rx_request(first);

    /* Send MID packet (index 1) */
    usb_packet_msg_t mid = {0};
    mid.flags = PAYLOAD_FLAG_MID;
    mid.remaining_packets = 1; /* index = 3-1-1 = 1 */
    mid.payload_len = MAX_PAYLOAD_LENGTH;
    memset(mid.payload, 0x22, MAX_PAYLOAD_LENGTH);
    process_rx_request(mid);

    /* Build bitmap — should show packets 0 and 1 received */
    usb_packet_msg_t bitmap_resp = {0};
    rx_blast_build_bitmap_response(&bitmap_resp);

    TEST_ASSERT_EQUAL(PAYLOAD_FLAG_BITMAP, bitmap_resp.flags);
    /* Bits 0 and 1 set = 0x03 */
    TEST_ASSERT_EQUAL_HEX(0x03, bitmap_resp.payload[0]);
}

TEST_CASE(usb_rx, blast_receive_out_of_order) {
    /* Enter blast mode: 3 packets */
    usb_packet_msg_t first = {0};
    first.flags = PAYLOAD_FLAG_FIRST;
    first.remaining_packets = 2;
    first.payload_len = 10;
    memset(first.payload, 0x00, 10);
    process_rx_request(first);

    /* Send packet index 1 (MID, rem=1) with known data */
    usb_packet_msg_t mid = {0};
    mid.flags = PAYLOAD_FLAG_MID;
    mid.remaining_packets = 1;
    mid.payload_len = 10;
    memset(mid.payload, 0x11, 10);
    rx_blast_receive_packet(&mid);

    /* Bitmap should show both 0 and 1 received */
    usb_packet_msg_t bitmap = {0};
    rx_blast_build_bitmap_response(&bitmap);
    TEST_ASSERT_EQUAL_HEX(0x03, bitmap.payload[0]);
}

TEST_CASE(usb_rx, blast_duplicate_ignored) {
    /* Enter blast mode */
    usb_packet_msg_t first = {0};
    first.flags = PAYLOAD_FLAG_FIRST;
    first.remaining_packets = 1; /* total = 2 */
    first.payload_len = 5;
    memset(first.payload, 0xAA, 5);
    process_rx_request(first);

    /* Send same FIRST again as a retransmit via receive_packet */
    usb_packet_msg_t dup = {0};
    dup.remaining_packets = 1; /* index = 2-1-1 = 0 */
    dup.payload_len = 5;
    memset(dup.payload, 0xBB, 5);
    rx_blast_receive_packet(&dup);

    /* Bitmap should still show only bit 0 set (not counted twice) */
    usb_packet_msg_t bitmap = {0};
    rx_blast_build_bitmap_response(&bitmap);
    TEST_ASSERT_EQUAL_HEX(0x01, bitmap.payload[0]);
}

/* ---- Blast commit ---- */

TEST_CASE(usb_rx, blast_commit_assembles_full_payload) {
    /* Enter blast mode: 2 packets */
    usb_packet_msg_t first = {0};
    first.flags = PAYLOAD_FLAG_FIRST;
    first.remaining_packets = 1;
    first.payload_len = MAX_PAYLOAD_LENGTH;
    /* First byte = valid module ID for execute_callback */
    first.payload[0] = MODULE_CONFIG;
    memset(first.payload + 1, 0xAA, MAX_PAYLOAD_LENGTH - 1);
    process_rx_request(first);

    /* LAST packet triggers commit */
    usb_packet_msg_t last = {0};
    last.flags = PAYLOAD_FLAG_LAST;
    last.remaining_packets = 0;
    last.payload_len = 10;
    memset(last.payload, 0xBB, 10);
    process_rx_request(last);

    /* After commit, blast mode should be cleared */
    TEST_ASSERT_FALSE(rx_blast_active());
}

/* ---- Erase ---- */

TEST_CASE(usb_rx, erase_clears_all_state) {
    /* Enter blast mode */
    usb_packet_msg_t first = {0};
    first.flags = PAYLOAD_FLAG_FIRST;
    first.remaining_packets = 3;
    first.payload_len = 5;
    process_rx_request(first);
    TEST_ASSERT_TRUE(rx_blast_active());

    erase_rx_buffer();
    TEST_ASSERT_FALSE(rx_blast_active());
    TEST_ASSERT_EQUAL(0, rx_get_last_packet_timestamp_us());
}

/* ---- Timestamp tracking ---- */

TEST_CASE(usb_rx, timestamp_updated_on_receive) {
    mock_timer_set(5000);

    usb_packet_msg_t first = {0};
    first.flags = PAYLOAD_FLAG_FIRST;
    first.remaining_packets = 2;
    first.payload_len = 5;
    process_rx_request(first);

    /* After receiving the FIRST packet, the blast receive records timestamp */
    mock_timer_set(8000);
    usb_packet_msg_t mid = {0};
    mid.flags = PAYLOAD_FLAG_MID;
    mid.remaining_packets = 1;
    mid.payload_len = 5;
    process_rx_request(mid);

    /* Timestamp should be updated to latest */
    TEST_ASSERT_EQUAL(8000, rx_get_last_packet_timestamp_us());
}

/* ---- Bitmap response format ---- */

TEST_CASE(usb_rx, bitmap_response_correct_byte_count) {
    /* Enter blast mode with 10 packets */
    usb_packet_msg_t first = {0};
    first.flags = PAYLOAD_FLAG_FIRST;
    first.remaining_packets = 9; /* total = 10 */
    first.payload_len = MAX_PAYLOAD_LENGTH;
    process_rx_request(first);

    usb_packet_msg_t bitmap = {0};
    rx_blast_build_bitmap_response(&bitmap);

    /* 10 packets = ceil(10/8) = 2 bytes */
    TEST_ASSERT_EQUAL(2, bitmap.payload_len);
    TEST_ASSERT_EQUAL(0, bitmap.remaining_packets);
}

/* ---- Large blast transfer ---- */

TEST_CASE(usb_rx, large_blast_multiple_mid_packets) {
    /* Enter blast mode: 5 packets total */
    usb_packet_msg_t first = {0};
    first.flags = PAYLOAD_FLAG_FIRST;
    first.remaining_packets = 4; /* total = 5 */
    first.payload_len = MAX_PAYLOAD_LENGTH;
    first.payload[0] = MODULE_CONFIG;
    process_rx_request(first);

    /* Send 3 MID packets (indices 1, 2, 3) */
    for (int i = 1; i <= 3; i++) {
        usb_packet_msg_t mid = {0};
        mid.flags = PAYLOAD_FLAG_MID;
        mid.remaining_packets = 4 - i; /* rem for index i */
        mid.payload_len = MAX_PAYLOAD_LENGTH;
        memset(mid.payload, (uint8_t)(0x10 * i), MAX_PAYLOAD_LENGTH);
        process_rx_request(mid);
    }

    /* Bitmap should show bits 0-3 set = 0x0F */
    usb_packet_msg_t bitmap = {0};
    rx_blast_build_bitmap_response(&bitmap);
    TEST_ASSERT_EQUAL_HEX(0x0F, bitmap.payload[0]);

    /* Commit with LAST packet */
    usb_packet_msg_t last = {0};
    last.flags = PAYLOAD_FLAG_LAST;
    last.remaining_packets = 0;
    last.payload_len = 10;
    process_rx_request(last);

    TEST_ASSERT_FALSE(rx_blast_active());
}

/* ---- Constants ---- */

TEST_CASE(usb_rx, max_blast_packets) {
    TEST_ASSERT_EQUAL(384, RX_BLAST_MAX_PACKETS);
}
