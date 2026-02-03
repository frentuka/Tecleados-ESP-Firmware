#pragma once

#include <stdio.h>
#include <stdint.h>

#include "driver/gpio.h"

/*

    All available GPIO pins for matrix usage:
    GPIO 1-18
    GPIO 21
    GPIO 26-28
    GPIO 33-44
    GPIO 47-48

*/

/**
 * 5 ROWS [0..=4]
 */
#define KB_MATRIX_ROW_COUNT 6
#define GPIO_ROWS \
{ 0, GPIO_NUM_1 }, \
{ 1, GPIO_NUM_2 }, \
{ 2, GPIO_NUM_3 }, \
{ 3, GPIO_NUM_4 }, \
{ 4, GPIO_NUM_5 }, \
{ 5, GPIO_NUM_6 } // (reserved for future layouts)

/**
 * 13 COLS [0..=12]
 */
#define KB_MATRIX_COL_COUNT 20

#define GPIO_COLS \
{ 0,  GPIO_NUM_7 }, \
{ 1,  GPIO_NUM_8 }, \
{ 2,  GPIO_NUM_9 }, \
{ 3,  GPIO_NUM_10 }, \
{ 4,  GPIO_NUM_11 }, \
{ 5,  GPIO_NUM_12 }, \
{ 6,  GPIO_NUM_13 }, \
{ 7,  GPIO_NUM_14 }, \
{ 8,  GPIO_NUM_15 }, \
{ 9,  GPIO_NUM_16 }, \
{ 10, GPIO_NUM_17 }, \
{ 11, GPIO_NUM_18 }, \
{ 12, GPIO_NUM_21 }, \
{ 13, GPIO_NUM_26 }, \
{ 14, GPIO_NUM_27 }, \
{ 15, GPIO_NUM_28 }, \
{ 16, GPIO_NUM_33 }, \
{ 17, GPIO_NUM_34 }, \
{ 18, GPIO_NUM_35 }, \
{ 19, GPIO_NUM_36 }

#define KB_MATRIX_KEYS (KB_MATRIX_ROW_COUNT * KB_MATRIX_COL_COUNT)
#define KB_MATRIX_BITMAP_BYTES ((KB_MATRIX_KEYS + 7) / 8)

// Writes a row-major bitmap of pressed keys into out_active_rc_pairs.
// Bit index = (row_index * KB_MATRIX_COL_COUNT) + col_index.
// Buffer length must be KB_MATRIX_BITMAP_BYTES.
void scan(uint8_t* out_active_rc_pairs);
void kb_matrix_gpio_init(void);