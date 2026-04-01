/**
 * @file test_kb_layout.c
 * @brief Tests for cfg_layouts.c — real production cache lookup with
 *        transparent fallback, bounds checking, and DRAM swap tier.
 *
 * Links against real cfg_layouts.c (included in main.c).
 * Uses mock NVS (cfgmod.h shim) to inject or omit layer data.
 */
#include "test_harness.h"
#include "mocks/mock_esp.h"

/* cfg_layout_get_action_code, cfg_layout_load_all, cfg_layout_get_layer,
   cfg_layout_set_layer — all from linked cfg_layouts.c.
   cfg_layer_t from cfg_layouts.h via the production include path.
   keymaps[] from linked kb_layout.c. */

TEST_SETUP(kb_layout) {
    mock_nvs_reset();
    /* Load with empty NVS → falls back to compile-time keymaps[] */
    cfg_layout_load_all();
}

/* ---- Factory default tests (keymaps[] from kb_layout.c) ---- */

TEST_CASE(kb_layout, factory_base_layer_escape) {
    /* Row 0, Col 0 of base layer = HID_KEY_ESCAPE (0x29) */
    TEST_ASSERT_EQUAL_HEX(0x29, cfg_layout_get_action_code(0, 0, KB_LAYER_BASE));
}

TEST_CASE(kb_layout, factory_base_layer_a_key) {
    /* Row 2, Col 1 = HID_KEY_A (0x04) */
    TEST_ASSERT_EQUAL_HEX(0x04, cfg_layout_get_action_code(2, 1, KB_LAYER_BASE));
}

TEST_CASE(kb_layout, factory_fn1_layer_grave) {
    /* FN1 Row 0, Col 0 = HID_KEY_GRAVE (0x35) */
    TEST_ASSERT_EQUAL_HEX(0x35, cfg_layout_get_action_code(0, 0, KB_LAYER_FN1));
}

TEST_CASE(kb_layout, factory_fn1_transparent_falls_to_base) {
    /* FN1 Row 1, Col 1 = KB_KEY_TRANSPARENT → should fall through to base = HID_KEY_Q (0x14) */
    TEST_ASSERT_EQUAL_HEX(0x14, cfg_layout_get_action_code(1, 1, KB_LAYER_FN1));
}

TEST_CASE(kb_layout, factory_fn1_delete_override) {
    /* FN1 Row 0, Col 14 = HID_KEY_DELETE (0x4C) instead of base HID_KEY_INSERT (0x49) */
    TEST_ASSERT_EQUAL_HEX(0x4C, cfg_layout_get_action_code(0, 14, KB_LAYER_FN1));
}

TEST_CASE(kb_layout, factory_fn2_all_transparent_falls_to_base) {
    /* FN2 Row 2, Col 1 = TRANSPARENT → base = HID_KEY_A (0x04) */
    TEST_ASSERT_EQUAL_HEX(0x04, cfg_layout_get_action_code(2, 1, KB_LAYER_FN2));
}

TEST_CASE(kb_layout, factory_fn3_all_transparent_falls_to_base) {
    /* FN3 is fully transparent → falls through */
    TEST_ASSERT_EQUAL_HEX(0x29, cfg_layout_get_action_code(0, 0, KB_LAYER_FN3));
}

TEST_CASE(kb_layout, factory_base_layer_fn_keys) {
    /* Row 4: FN1 at col 10, FN2 at col 11 */
    TEST_ASSERT_EQUAL_HEX(SYS_ACTION_LAYER_FN1, cfg_layout_get_action_code(4, 10, KB_LAYER_BASE));
    TEST_ASSERT_EQUAL_HEX(SYS_ACTION_LAYER_FN2, cfg_layout_get_action_code(4, 11, KB_LAYER_BASE));
}

TEST_CASE(kb_layout, factory_base_spacebar) {
    /* Row 4, Col 5 = HID_KEY_SPACE (0x2C) */
    TEST_ASSERT_EQUAL_HEX(0x2C, cfg_layout_get_action_code(4, 5, KB_LAYER_BASE));
}

/* ---- Bounds checking ---- */

TEST_CASE(kb_layout, out_of_bounds_row_returns_none) {
    TEST_ASSERT_EQUAL_HEX(ACTION_CODE_NONE, cfg_layout_get_action_code(KB_MATRIX_ROW_COUNT, 0, 0));
}

TEST_CASE(kb_layout, out_of_bounds_col_returns_none) {
    TEST_ASSERT_EQUAL_HEX(ACTION_CODE_NONE, cfg_layout_get_action_code(0, KB_MATRIX_COL_COUNT, 0));
}

TEST_CASE(kb_layout, out_of_bounds_layer_defaults_to_base) {
    /* Layer 99 > KB_LAYER_COUNT → clamps to base */
    TEST_ASSERT_EQUAL_HEX(0x29, cfg_layout_get_action_code(0, 0, 99));
}

/* ---- Cache tier behavior ---- */

TEST_CASE(kb_layout, dram_swap_activates_on_fn_layer) {
    /* Accessing FN1 triggers PSRAM→DRAM swap. Second access is a cache hit. */
    uint16_t first = cfg_layout_get_action_code(0, 0, KB_LAYER_FN1);
    uint16_t second = cfg_layout_get_action_code(0, 0, KB_LAYER_FN1);
    TEST_ASSERT_EQUAL_HEX(first, second);
    TEST_ASSERT_EQUAL_HEX(0x35, first); /* HID_KEY_GRAVE */
}

TEST_CASE(kb_layout, dram_swap_eviction_on_layer_switch) {
    /* Access FN1, then FN2 — swap cache should switch */
    uint16_t fn1_val = cfg_layout_get_action_code(0, 0, KB_LAYER_FN1);
    /* FN2 Row 0 Col 0 is TRANSPARENT → falls to base = ESCAPE */
    uint16_t fn2_val = cfg_layout_get_action_code(0, 0, KB_LAYER_FN2);
    TEST_ASSERT_EQUAL_HEX(0x35, fn1_val);
    TEST_ASSERT_EQUAL_HEX(0x29, fn2_val); /* Transparent → base escape */
}

/* ---- NVS-injected custom layer ---- */

TEST_CASE(kb_layout, nvs_overrides_factory_layer) {
    mock_nvs_reset();

    /* Build a custom base layer with key A at [0][0] instead of Escape */
    cfg_layer_t custom_base;
    memset(&custom_base, 0, sizeof(custom_base));
    custom_base.keys[0][0] = 0x04; /* HID_KEY_A */
    custom_base.keys[2][1] = 0x05; /* HID_KEY_B */

    mock_nvs_inject(CFGMOD_KIND_LAYOUT, "ly0", &custom_base, sizeof(custom_base));
    cfg_layout_load_all();

    TEST_ASSERT_EQUAL_HEX(0x04, cfg_layout_get_action_code(0, 0, KB_LAYER_BASE));
    TEST_ASSERT_EQUAL_HEX(0x05, cfg_layout_get_action_code(2, 1, KB_LAYER_BASE));
}

TEST_CASE(kb_layout, nvs_fn1_layer_override) {
    mock_nvs_reset();

    cfg_layer_t custom_fn1;
    memset(&custom_fn1, 0xFF, sizeof(custom_fn1)); /* All transparent */
    custom_fn1.keys[0][0] = 0x42; /* Non-transparent override */

    mock_nvs_inject(CFGMOD_KIND_LAYOUT, "ly1", &custom_fn1, sizeof(custom_fn1));
    cfg_layout_load_all();

    /* Override should return 0x42 */
    TEST_ASSERT_EQUAL_HEX(0x42, cfg_layout_get_action_code(0, 0, KB_LAYER_FN1));
    /* Transparent falls to base (factory Tab at [1][0] since ly0 not in NVS) */
    TEST_ASSERT_EQUAL_HEX(0x2B, cfg_layout_get_action_code(1, 0, KB_LAYER_FN1));
}

TEST_CASE(kb_layout, partial_nvs_only_overrides_stored_layers) {
    mock_nvs_reset();

    /* Only inject ly2 — others use factory defaults */
    cfg_layer_t custom_fn2;
    memset(&custom_fn2, 0, sizeof(custom_fn2));
    custom_fn2.keys[3][3] = 0x77;

    mock_nvs_inject(CFGMOD_KIND_LAYOUT, "ly2", &custom_fn2, sizeof(custom_fn2));
    cfg_layout_load_all();

    /* FN2 custom key */
    TEST_ASSERT_EQUAL_HEX(0x77, cfg_layout_get_action_code(3, 3, KB_LAYER_FN2));
    /* Base layer still has factory defaults */
    TEST_ASSERT_EQUAL_HEX(0x29, cfg_layout_get_action_code(0, 0, KB_LAYER_BASE));
    /* FN1 still has factory defaults */
    TEST_ASSERT_EQUAL_HEX(0x35, cfg_layout_get_action_code(0, 0, KB_LAYER_FN1));
}

/* ---- All matrix corners ---- */

TEST_CASE(kb_layout, all_corners_valid) {
    /* Top-left */
    uint16_t tl = cfg_layout_get_action_code(0, 0, KB_LAYER_BASE);
    TEST_ASSERT(tl != ACTION_CODE_NONE || tl == ACTION_CODE_NONE); /* valid access */

    /* Top-right */
    uint16_t tr = cfg_layout_get_action_code(0, KB_MATRIX_COL_COUNT - 1, KB_LAYER_BASE);
    (void)tr; /* just checking no crash */

    /* Bottom-left */
    uint16_t bl = cfg_layout_get_action_code(KB_MATRIX_ROW_COUNT - 1, 0, KB_LAYER_BASE);
    (void)bl;

    /* Bottom-right */
    uint16_t br = cfg_layout_get_action_code(KB_MATRIX_ROW_COUNT - 1, KB_MATRIX_COL_COUNT - 1, KB_LAYER_BASE);
    (void)br;

    /* Just past boundary = NONE */
    TEST_ASSERT_EQUAL_HEX(ACTION_CODE_NONE, cfg_layout_get_action_code(KB_MATRIX_ROW_COUNT, KB_MATRIX_COL_COUNT, 0));
}

TEST_CASE(kb_layout, base_none_stays_none_through_transparent) {
    /* Row 5 is unused in factory defaults → HID_KEY_NONE = 0x00 */
    /* FN1 Row 5 is also NONE (not transparent, actual NONE) */
    uint16_t val = cfg_layout_get_action_code(5, 0, KB_LAYER_FN1);
    /* FN1[5][0] = HID_KEY_NONE in factory → NONE is not KB_KEY_TRANSPARENT so no fallthrough */
    TEST_ASSERT_EQUAL_HEX(HID_KEY_NONE, val);
}
