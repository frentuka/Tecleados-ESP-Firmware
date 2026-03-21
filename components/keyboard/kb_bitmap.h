/**
 * @file kb_bitmap.h
 * @brief Internal bit-manipulation helpers shared across the keyboard module.
 *
 * These are trivial single-byte-addressed bitmaps used by the matrix scanner,
 * the keyboard manager, and the macro engine to represent sets of pressed keys.
 * Bit index = (row * KB_MATRIX_COL_COUNT + col)  for the physical matrix, or
 * Bit index = HID keycode  for the virtual NKRO state.
 *
 * This file is private to the keyboard component — do not place in include/.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

static inline void kb_bit_set(uint8_t *bitmap, size_t bit_index) {
    bitmap[bit_index >> 3] |= (uint8_t)(1U << (bit_index & 7U));
}

static inline void kb_bit_clear(uint8_t *bitmap, size_t bit_index) {
    bitmap[bit_index >> 3] &= (uint8_t)~(1U << (bit_index & 7U));
}

static inline bool kb_bit_get(const uint8_t *bitmap, size_t bit_index) {
    return (bitmap[bit_index >> 3] & (uint8_t)(1U << (bit_index & 7U))) != 0;
}
