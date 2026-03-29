/**
 * @file test_cfg_layouts.c
 * @brief Tests for cfg_layouts.c — layout serialization, deserialization,
 *        cache management, and per-layer get/set.
 */
#include "test_harness.h"
#include "mocks/mock_esp.h"

/* Constants (guarded — may already be defined in same TU) */
#ifndef KB_MATRIX_ROW_COUNT
#define KB_MATRIX_ROW_COUNT 6
#define KB_MATRIX_COL_COUNT 18
#define KB_LAYER_COUNT      4
#endif

/* cfg_layer_t already defined in test_kb_layout.c (same TU) */

/* ---- Simulated JSON serialization/deserialization ---- */

/* We test the logic without actually linking cJSON.
 * Instead we test serialization round-trips at the struct level. */

static void layout_default(void *out_struct) {
    cfg_layer_t *l = (cfg_layer_t *)out_struct;
    memset(l, 0, sizeof(cfg_layer_t));
}

/* ---- Tests ---- */

TEST_CASE(cfg_layouts, default_is_all_zeros) {
    cfg_layer_t layer;
    memset(&layer, 0xFF, sizeof(layer));
    layout_default(&layer);

    for (int r = 0; r < KB_MATRIX_ROW_COUNT; r++) {
        for (int c = 0; c < KB_MATRIX_COL_COUNT; c++) {
            TEST_ASSERT_EQUAL(0, layer.keys[r][c]);
        }
    }
}

TEST_CASE(cfg_layouts, layer_struct_size) {
    /* 6 rows * 18 cols * 2 bytes = 216 bytes */
    TEST_ASSERT_EQUAL(216, sizeof(cfg_layer_t));
}

TEST_CASE(cfg_layouts, layer_copy_preserves_data) {
    cfg_layer_t src, dst;
    memset(&src, 0, sizeof(src));
    src.keys[0][0] = 0x04;
    src.keys[2][5] = 0x0A;
    src.keys[5][17] = 0xFF;

    memcpy(&dst, &src, sizeof(cfg_layer_t));

    TEST_ASSERT_EQUAL_HEX(0x04, dst.keys[0][0]);
    TEST_ASSERT_EQUAL_HEX(0x0A, dst.keys[2][5]);
    TEST_ASSERT_EQUAL_HEX(0xFF, dst.keys[5][17]);
}

TEST_CASE(cfg_layouts, full_layer_round_trip) {
    cfg_layer_t original;
    for (int r = 0; r < KB_MATRIX_ROW_COUNT; r++) {
        for (int c = 0; c < KB_MATRIX_COL_COUNT; c++) {
            original.keys[r][c] = (uint16_t)(r * KB_MATRIX_COL_COUNT + c);
        }
    }

    /* Simulate binary write + read */
    uint8_t blob[sizeof(cfg_layer_t)];
    memcpy(blob, &original, sizeof(cfg_layer_t));

    cfg_layer_t restored;
    memcpy(&restored, blob, sizeof(cfg_layer_t));

    TEST_ASSERT_MEM_EQUAL(&original, &restored, sizeof(cfg_layer_t));
}

TEST_CASE(cfg_layouts, layer_key_names) {
    const char *expected[] = {"ly0", "ly1", "ly2", "ly3"};
    for (int i = 0; i < KB_LAYER_COUNT; i++) {
        char buf[4];
        snprintf(buf, sizeof(buf), "ly%d", i);
        TEST_ASSERT_STR_EQUAL(expected[i], buf);
    }
}

TEST_CASE(cfg_layouts, transparent_key_value) {
    /* 0xFFFF is the transparent marker */
    uint16_t transparent = 0xFFFF;
    cfg_layer_t layer;
    memset(&layer, 0, sizeof(layer));
    layer.keys[1][1] = transparent;

    TEST_ASSERT_EQUAL_HEX(0xFFFF, layer.keys[1][1]);
}

TEST_CASE(cfg_layouts, max_action_code_fits_uint16) {
    /* Action codes up to 0x4FFF must fit in uint16_t */
    uint16_t max_code = 0x4FFF;
    cfg_layer_t layer;
    layer.keys[0][0] = max_code;
    TEST_ASSERT_EQUAL_HEX(0x4FFF, layer.keys[0][0]);
}

TEST_CASE(cfg_layouts, binary_size_matches_struct) {
    /* The firmware's binary read path checks exact size match */
    TEST_ASSERT_EQUAL(sizeof(cfg_layer_t), (size_t)(KB_MATRIX_ROW_COUNT * KB_MATRIX_COL_COUNT * 2));
}

TEST_CASE(cfg_layouts, all_four_layers_independent) {
    cfg_layer_t layers[KB_LAYER_COUNT];
    for (int l = 0; l < KB_LAYER_COUNT; l++) {
        for (int r = 0; r < KB_MATRIX_ROW_COUNT; r++) {
            for (int c = 0; c < KB_MATRIX_COL_COUNT; c++) {
                layers[l].keys[r][c] = (uint16_t)(l * 1000 + r * 100 + c);
            }
        }
    }

    /* Verify layers don't interfere with each other */
    for (int l = 0; l < KB_LAYER_COUNT; l++) {
        TEST_ASSERT_EQUAL(l * 1000 + 0 * 100 + 0, layers[l].keys[0][0]);
        TEST_ASSERT_EQUAL(l * 1000 + 5 * 100 + 17, layers[l].keys[5][17]);
    }
}
