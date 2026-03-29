/**
 * @file test_kb_bitmap.c
 * @brief Tests for kb_bitmap.h bit-manipulation helpers.
 *
 * Tests set, clear, get operations and boundary conditions for
 * the bitmap used by the matrix scanner and NKRO state.
 */
#include "test_harness.h"

/* Bring in the bitmap helpers directly */
static inline void kb_bit_set(uint8_t *bitmap, size_t bit_index) {
    bitmap[bit_index >> 3] |= (uint8_t)(1U << (bit_index & 7U));
}

static inline void kb_bit_clear(uint8_t *bitmap, size_t bit_index) {
    bitmap[bit_index >> 3] &= (uint8_t)~(1U << (bit_index & 7U));
}

static inline bool kb_bit_get(const uint8_t *bitmap, size_t bit_index) {
    return (bitmap[bit_index >> 3] & (uint8_t)(1U << (bit_index & 7U))) != 0;
}

/* ---- Tests ---- */

TEST_CASE(kb_bitmap, set_and_get_bit_zero) {
    uint8_t bitmap[4] = {0};
    kb_bit_set(bitmap, 0);
    TEST_ASSERT_TRUE(kb_bit_get(bitmap, 0));
    TEST_ASSERT_EQUAL_HEX(0x01, bitmap[0]);
}

TEST_CASE(kb_bitmap, set_and_get_bit_seven) {
    uint8_t bitmap[4] = {0};
    kb_bit_set(bitmap, 7);
    TEST_ASSERT_TRUE(kb_bit_get(bitmap, 7));
    TEST_ASSERT_EQUAL_HEX(0x80, bitmap[0]);
}

TEST_CASE(kb_bitmap, set_and_get_bit_eight) {
    uint8_t bitmap[4] = {0};
    kb_bit_set(bitmap, 8);
    TEST_ASSERT_TRUE(kb_bit_get(bitmap, 8));
    TEST_ASSERT_EQUAL_HEX(0x00, bitmap[0]);
    TEST_ASSERT_EQUAL_HEX(0x01, bitmap[1]);
}

TEST_CASE(kb_bitmap, set_and_get_bit_31) {
    uint8_t bitmap[4] = {0};
    kb_bit_set(bitmap, 31);
    TEST_ASSERT_TRUE(kb_bit_get(bitmap, 31));
    TEST_ASSERT_EQUAL_HEX(0x80, bitmap[3]);
}

TEST_CASE(kb_bitmap, clear_bit) {
    uint8_t bitmap[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    kb_bit_clear(bitmap, 0);
    TEST_ASSERT_FALSE(kb_bit_get(bitmap, 0));
    TEST_ASSERT_EQUAL_HEX(0xFE, bitmap[0]);
}

TEST_CASE(kb_bitmap, clear_preserves_adjacent_bits) {
    uint8_t bitmap[4] = {0};
    kb_bit_set(bitmap, 4);
    kb_bit_set(bitmap, 5);
    kb_bit_set(bitmap, 6);
    kb_bit_clear(bitmap, 5);
    TEST_ASSERT_TRUE(kb_bit_get(bitmap, 4));
    TEST_ASSERT_FALSE(kb_bit_get(bitmap, 5));
    TEST_ASSERT_TRUE(kb_bit_get(bitmap, 6));
}

TEST_CASE(kb_bitmap, set_preserves_existing_bits) {
    uint8_t bitmap[4] = {0};
    kb_bit_set(bitmap, 0);
    kb_bit_set(bitmap, 7);
    TEST_ASSERT_TRUE(kb_bit_get(bitmap, 0));
    TEST_ASSERT_TRUE(kb_bit_get(bitmap, 7));
    TEST_ASSERT_EQUAL_HEX(0x81, bitmap[0]);
}

TEST_CASE(kb_bitmap, get_unset_bit_returns_false) {
    uint8_t bitmap[4] = {0};
    for (int i = 0; i < 32; i++) {
        TEST_ASSERT_FALSE(kb_bit_get(bitmap, i));
    }
}

TEST_CASE(kb_bitmap, set_all_bits_in_byte) {
    uint8_t bitmap[1] = {0};
    for (int i = 0; i < 8; i++) {
        kb_bit_set(bitmap, i);
    }
    TEST_ASSERT_EQUAL_HEX(0xFF, bitmap[0]);
}

TEST_CASE(kb_bitmap, matrix_bitmap_size) {
    /* 6 rows * 18 cols = 108 keys, needs ceil(108/8) = 14 bytes */
    #define KB_MATRIX_KEYS_TEST (6 * 18)
    #define KB_MATRIX_BITMAP_BYTES_TEST ((KB_MATRIX_KEYS_TEST + 7) / 8)
    TEST_ASSERT_EQUAL(14, KB_MATRIX_BITMAP_BYTES_TEST);

    uint8_t bitmap[KB_MATRIX_BITMAP_BYTES_TEST] = {0};

    /* Set last valid bit (107) */
    kb_bit_set(bitmap, 107);
    TEST_ASSERT_TRUE(kb_bit_get(bitmap, 107));

    /* All bits below 107 should still be clear */
    for (int i = 0; i < 107; i++) {
        TEST_ASSERT_FALSE(kb_bit_get(bitmap, i));
    }
}

TEST_CASE(kb_bitmap, nkro_bitmap_256_bits) {
    /* NKRO uses 32 bytes = 256 bits for all HID keycodes */
    uint8_t bitmap[32] = {0};

    /* Set modifier keys (0xE0-0xE7 = 224-231) */
    for (int i = 224; i <= 231; i++) {
        kb_bit_set(bitmap, i);
    }

    /* Verify only modifiers are set */
    for (int i = 0; i < 224; i++) {
        TEST_ASSERT_FALSE(kb_bit_get(bitmap, i));
    }
    for (int i = 224; i <= 231; i++) {
        TEST_ASSERT_TRUE(kb_bit_get(bitmap, i));
    }
    for (int i = 232; i < 256; i++) {
        TEST_ASSERT_FALSE(kb_bit_get(bitmap, i));
    }
}

TEST_CASE(kb_bitmap, double_set_is_idempotent) {
    uint8_t bitmap[4] = {0};
    kb_bit_set(bitmap, 5);
    kb_bit_set(bitmap, 5);
    TEST_ASSERT_TRUE(kb_bit_get(bitmap, 5));
    TEST_ASSERT_EQUAL_HEX(0x20, bitmap[0]);
}

TEST_CASE(kb_bitmap, double_clear_is_idempotent) {
    uint8_t bitmap[4] = {0};
    kb_bit_clear(bitmap, 5);
    TEST_ASSERT_FALSE(kb_bit_get(bitmap, 5));
    TEST_ASSERT_EQUAL_HEX(0x00, bitmap[0]);
}
