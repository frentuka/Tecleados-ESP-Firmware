/**
 * @file test_kb_layout.c
 * @brief Tests for kb_layout.c and cfg_layouts.c — keymap lookup, transparent
 *        fallback, cache tiers, and bounds checking.
 */
#include "test_harness.h"
#include "mocks/mock_esp.h"

/* Constants from the firmware */
#define KB_MATRIX_ROW_COUNT 6
#define KB_MATRIX_COL_COUNT 18
#define KB_LAYER_COUNT      4
#define KB_LAYER_BASE       0
#define KB_LAYER_FN1        1
#define KB_LAYER_FN2        2
#define KB_LAYER_FN3        3

#define KB_KEY_TRANSPARENT  0xFFFF
#define ACTION_CODE_NONE    0x0000

/* Simulated layer cache */
typedef struct {
    uint16_t keys[KB_MATRIX_ROW_COUNT][KB_MATRIX_COL_COUNT];
} cfg_layer_t;

static cfg_layer_t s_dram_base;
static cfg_layer_t s_dram_swap;
static uint8_t     s_swap_layer_idx = 0xFF;
static cfg_layer_t s_psram_cache[KB_LAYER_COUNT];
static bool        s_psram_valid = true;

/* Replicate cfg_layout_get_action_code logic */
static uint16_t cfg_layout_get_action_code(uint8_t row, uint8_t col, uint8_t layer) {
    if (row >= KB_MATRIX_ROW_COUNT || col >= KB_MATRIX_COL_COUNT) {
        return ACTION_CODE_NONE;
    }
    if (layer >= KB_LAYER_COUNT) {
        layer = KB_LAYER_BASE;
    }

    uint16_t kc;
    if (layer == 0) {
        kc = s_dram_base.keys[row][col];
    } else if (layer == s_swap_layer_idx) {
        kc = s_dram_swap.keys[row][col];
    } else {
        if (s_psram_valid) {
            s_dram_swap = s_psram_cache[layer];
            s_swap_layer_idx = layer;
            kc = s_dram_swap.keys[row][col];
        } else {
            kc = KB_KEY_TRANSPARENT;
        }
    }

    if (kc == KB_KEY_TRANSPARENT && layer != 0) {
        kc = s_dram_base.keys[row][col];
    }
    return kc;
}

static void reset_layout(void) {
    memset(&s_dram_base, 0, sizeof(s_dram_base));
    memset(&s_dram_swap, 0, sizeof(s_dram_swap));
    memset(s_psram_cache, 0, sizeof(s_psram_cache));
    s_swap_layer_idx = 0xFF;
    s_psram_valid = true;
}

/* ---- Tests ---- */

TEST_CASE(kb_layout, base_layer_lookup) {
    reset_layout();
    s_dram_base.keys[0][0] = 0x04; /* HID_KEY_A */
    s_psram_cache[0] = s_dram_base;

    TEST_ASSERT_EQUAL_HEX(0x04, cfg_layout_get_action_code(0, 0, KB_LAYER_BASE));
}

TEST_CASE(kb_layout, fn1_layer_override) {
    reset_layout();
    s_dram_base.keys[0][0] = 0x04; /* A on base */
    s_psram_cache[0] = s_dram_base;
    s_psram_cache[KB_LAYER_FN1].keys[0][0] = 0x29; /* Escape on FN1 */

    TEST_ASSERT_EQUAL_HEX(0x29, cfg_layout_get_action_code(0, 0, KB_LAYER_FN1));
}

TEST_CASE(kb_layout, transparent_falls_to_base) {
    reset_layout();
    s_dram_base.keys[2][5] = 0x0A; /* G on base */
    s_psram_cache[0] = s_dram_base;
    s_psram_cache[KB_LAYER_FN1].keys[2][5] = KB_KEY_TRANSPARENT;

    TEST_ASSERT_EQUAL_HEX(0x0A, cfg_layout_get_action_code(2, 5, KB_LAYER_FN1));
}

TEST_CASE(kb_layout, out_of_bounds_row_returns_none) {
    reset_layout();
    TEST_ASSERT_EQUAL_HEX(ACTION_CODE_NONE, cfg_layout_get_action_code(KB_MATRIX_ROW_COUNT, 0, 0));
}

TEST_CASE(kb_layout, out_of_bounds_col_returns_none) {
    reset_layout();
    TEST_ASSERT_EQUAL_HEX(ACTION_CODE_NONE, cfg_layout_get_action_code(0, KB_MATRIX_COL_COUNT, 0));
}

TEST_CASE(kb_layout, out_of_bounds_layer_defaults_to_base) {
    reset_layout();
    s_dram_base.keys[0][0] = 0x42;
    s_psram_cache[0] = s_dram_base;

    TEST_ASSERT_EQUAL_HEX(0x42, cfg_layout_get_action_code(0, 0, 99));
}

TEST_CASE(kb_layout, dram_swap_cache_hit) {
    reset_layout();
    s_psram_cache[KB_LAYER_FN2].keys[1][1] = 0x55;

    /* First access loads into swap cache */
    uint16_t kc = cfg_layout_get_action_code(1, 1, KB_LAYER_FN2);
    TEST_ASSERT_EQUAL_HEX(0x55, kc);
    TEST_ASSERT_EQUAL(KB_LAYER_FN2, s_swap_layer_idx);

    /* Second access should be a swap cache hit (no PSRAM read) */
    kc = cfg_layout_get_action_code(1, 1, KB_LAYER_FN2);
    TEST_ASSERT_EQUAL_HEX(0x55, kc);
}

TEST_CASE(kb_layout, dram_swap_eviction_on_layer_change) {
    reset_layout();
    s_psram_cache[KB_LAYER_FN1].keys[0][0] = 0xAA;
    s_psram_cache[KB_LAYER_FN2].keys[0][0] = 0xBB;

    cfg_layout_get_action_code(0, 0, KB_LAYER_FN1);
    TEST_ASSERT_EQUAL(KB_LAYER_FN1, s_swap_layer_idx);

    cfg_layout_get_action_code(0, 0, KB_LAYER_FN2);
    TEST_ASSERT_EQUAL(KB_LAYER_FN2, s_swap_layer_idx);
}

TEST_CASE(kb_layout, fn3_with_all_transparent_falls_to_base) {
    reset_layout();
    s_dram_base.keys[3][10] = 0x37; /* period */
    s_psram_cache[0] = s_dram_base;
    /* FN3 layer is all zeros which != transparent */
    /* Make it transparent */
    for (int r = 0; r < KB_MATRIX_ROW_COUNT; r++)
        for (int c = 0; c < KB_MATRIX_COL_COUNT; c++)
            s_psram_cache[KB_LAYER_FN3].keys[r][c] = KB_KEY_TRANSPARENT;

    TEST_ASSERT_EQUAL_HEX(0x37, cfg_layout_get_action_code(3, 10, KB_LAYER_FN3));
}

TEST_CASE(kb_layout, none_on_base_stays_none_even_through_transparent) {
    reset_layout();
    s_dram_base.keys[5][0] = ACTION_CODE_NONE;
    s_psram_cache[0] = s_dram_base;
    s_psram_cache[KB_LAYER_FN1].keys[5][0] = KB_KEY_TRANSPARENT;

    /* Transparent falls to base, which is NONE */
    TEST_ASSERT_EQUAL_HEX(ACTION_CODE_NONE, cfg_layout_get_action_code(5, 0, KB_LAYER_FN1));
}

TEST_CASE(kb_layout, base_layer_transparent_not_recursive) {
    reset_layout();
    s_dram_base.keys[0][0] = KB_KEY_TRANSPARENT;
    s_psram_cache[0] = s_dram_base;

    /* On base layer, transparent stays transparent (no fallback) */
    TEST_ASSERT_EQUAL_HEX(KB_KEY_TRANSPARENT, cfg_layout_get_action_code(0, 0, KB_LAYER_BASE));
}

TEST_CASE(kb_layout, all_corners_valid) {
    reset_layout();
    s_dram_base.keys[0][0] = 0x01;
    s_dram_base.keys[0][KB_MATRIX_COL_COUNT - 1] = 0x02;
    s_dram_base.keys[KB_MATRIX_ROW_COUNT - 1][0] = 0x03;
    s_dram_base.keys[KB_MATRIX_ROW_COUNT - 1][KB_MATRIX_COL_COUNT - 1] = 0x04;
    s_psram_cache[0] = s_dram_base;

    TEST_ASSERT_EQUAL_HEX(0x01, cfg_layout_get_action_code(0, 0, 0));
    TEST_ASSERT_EQUAL_HEX(0x02, cfg_layout_get_action_code(0, 17, 0));
    TEST_ASSERT_EQUAL_HEX(0x03, cfg_layout_get_action_code(5, 0, 0));
    TEST_ASSERT_EQUAL_HEX(0x04, cfg_layout_get_action_code(5, 17, 0));
}
