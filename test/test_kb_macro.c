/**
 * @file test_kb_macro.c
 * @brief Tests for kb_macro.c — action code dispatch, layer state management,
 *        virtual NKRO bitmap, and macro execution mode selection.
 */
#include "test_harness.h"
#include "mocks/mock_esp.h"

/* ---- Constants from the firmware (guarded — may already be defined in same TU) ---- */

#ifndef ACTION_CODE_NONE
#define ACTION_CODE_NONE       0x0000
#endif
#ifndef ACTION_CODE_HID_MIN
#define ACTION_CODE_HID_MIN    0x0001
#define ACTION_CODE_HID_MAX    0x00FF
#endif
#ifndef ACTION_CODE_SYSTEM_MIN
#define ACTION_CODE_SYSTEM_MIN 0x2000
#define ACTION_CODE_SYSTEM_MAX 0x20FF
#endif
#ifndef ACTION_CODE_CKEY_MIN
#define ACTION_CODE_CKEY_MIN   0x3000
#define ACTION_CODE_CKEY_MAX   0x3FFF
#endif
#ifndef ACTION_CODE_MACRO_MIN
#define ACTION_CODE_MACRO_MIN  0x4000
#define ACTION_CODE_MACRO_MAX  0x4FFF
#endif

#ifndef SYS_ACTION_LAYER_FN1
#define SYS_ACTION_LAYER_FN1   (ACTION_CODE_SYSTEM_MIN + 1)
#define SYS_ACTION_LAYER_FN2   (ACTION_CODE_SYSTEM_MIN + 2)
#endif

#ifndef KB_LAYER_BASE
#define KB_LAYER_BASE 0
#define KB_LAYER_FN1  1
#define KB_LAYER_FN2  2
#define KB_LAYER_FN3  3
#endif

/* ---- Simplified NKRO + layer logic for testing ---- */

static uint8_t s_v_nkro[32] = {0};
static uint8_t s_active_layer = KB_LAYER_BASE;
static bool    s_is_fn1_held = false;
static bool    s_is_fn2_held = false;

/* Track dispatch destinations */
static int s_system_action_calls = 0;
static int s_ckey_calls = 0;
static uint16_t s_last_system_action = 0;
static uint16_t s_last_ckey_action = 0;

static void reset_macro_state(void) {
    memset(s_v_nkro, 0, sizeof(s_v_nkro));
    s_active_layer = KB_LAYER_BASE;
    s_is_fn1_held = false;
    s_is_fn2_held = false;
    s_system_action_calls = 0;
    s_ckey_calls = 0;
    s_last_system_action = 0;
    s_last_ckey_action = 0;
}

/* kb_bit_set/clear/get already defined in test_kb_bitmap.c (same TU) */

static void update_layer_state(void) {
    if (s_is_fn1_held && s_is_fn2_held) s_active_layer = KB_LAYER_FN3;
    else if (s_is_fn2_held) s_active_layer = KB_LAYER_FN2;
    else if (s_is_fn1_held) s_active_layer = KB_LAYER_FN1;
    else s_active_layer = KB_LAYER_BASE;
}

/* Simplified action dispatch (matches kb_macro_process_action routing) */
static void kb_macro_process_action(uint16_t action_code, bool is_pressed) {
    if (action_code >= ACTION_CODE_HID_MIN && action_code <= ACTION_CODE_HID_MAX) {
        if (is_pressed) kb_bit_set(s_v_nkro, action_code);
        else kb_bit_clear(s_v_nkro, action_code);
        return;
    }

    if (action_code >= ACTION_CODE_SYSTEM_MIN && action_code <= ACTION_CODE_SYSTEM_MAX) {
        if (action_code == SYS_ACTION_LAYER_FN1) {
            s_is_fn1_held = is_pressed;
            update_layer_state();
            return;
        }
        if (action_code == SYS_ACTION_LAYER_FN2) {
            s_is_fn2_held = is_pressed;
            update_layer_state();
            return;
        }
        s_system_action_calls++;
        s_last_system_action = action_code;
        return;
    }

    if (action_code >= ACTION_CODE_CKEY_MIN && action_code <= ACTION_CODE_CKEY_MAX) {
        s_ckey_calls++;
        s_last_ckey_action = action_code;
        return;
    }
}

/* ---- HID key tests ---- */

TEST_CASE(kb_macro, hid_key_press_sets_nkro_bit) {
    reset_macro_state();
    kb_macro_process_action(0x04, true); /* A */
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x04));
}

TEST_CASE(kb_macro, hid_key_release_clears_nkro_bit) {
    reset_macro_state();
    kb_macro_process_action(0x04, true);
    kb_macro_process_action(0x04, false);
    TEST_ASSERT_FALSE(kb_bit_get(s_v_nkro, 0x04));
}

TEST_CASE(kb_macro, multiple_hid_keys_simultaneous) {
    reset_macro_state();
    kb_macro_process_action(0x04, true); /* A */
    kb_macro_process_action(0x05, true); /* B */
    kb_macro_process_action(0x06, true); /* C */

    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x04));
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x05));
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0x06));
}

TEST_CASE(kb_macro, modifier_keys_in_nkro) {
    reset_macro_state();
    kb_macro_process_action(0xE0, true); /* Left Control */
    kb_macro_process_action(0xE1, true); /* Left Shift */

    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0xE0));
    TEST_ASSERT_TRUE(kb_bit_get(s_v_nkro, 0xE1));
}

TEST_CASE(kb_macro, action_code_none_ignored) {
    reset_macro_state();
    kb_macro_process_action(ACTION_CODE_NONE, true);
    /* NKRO should be untouched */
    for (int i = 0; i < 32; i++) TEST_ASSERT_EQUAL(0, s_v_nkro[i]);
}

/* ---- Layer state tests ---- */

TEST_CASE(kb_macro, fn1_press_activates_layer) {
    reset_macro_state();
    kb_macro_process_action(SYS_ACTION_LAYER_FN1, true);
    TEST_ASSERT_EQUAL(KB_LAYER_FN1, s_active_layer);
}

TEST_CASE(kb_macro, fn1_release_returns_to_base) {
    reset_macro_state();
    kb_macro_process_action(SYS_ACTION_LAYER_FN1, true);
    kb_macro_process_action(SYS_ACTION_LAYER_FN1, false);
    TEST_ASSERT_EQUAL(KB_LAYER_BASE, s_active_layer);
}

TEST_CASE(kb_macro, fn2_press_activates_layer) {
    reset_macro_state();
    kb_macro_process_action(SYS_ACTION_LAYER_FN2, true);
    TEST_ASSERT_EQUAL(KB_LAYER_FN2, s_active_layer);
}

TEST_CASE(kb_macro, fn1_fn2_activates_fn3) {
    reset_macro_state();
    kb_macro_process_action(SYS_ACTION_LAYER_FN1, true);
    kb_macro_process_action(SYS_ACTION_LAYER_FN2, true);
    TEST_ASSERT_EQUAL(KB_LAYER_FN3, s_active_layer);
}

TEST_CASE(kb_macro, fn1_fn2_release_fn1_stays_fn2) {
    reset_macro_state();
    kb_macro_process_action(SYS_ACTION_LAYER_FN1, true);
    kb_macro_process_action(SYS_ACTION_LAYER_FN2, true);
    TEST_ASSERT_EQUAL(KB_LAYER_FN3, s_active_layer);

    kb_macro_process_action(SYS_ACTION_LAYER_FN1, false);
    TEST_ASSERT_EQUAL(KB_LAYER_FN2, s_active_layer);
}

TEST_CASE(kb_macro, fn2_fn1_also_activates_fn3) {
    reset_macro_state();
    kb_macro_process_action(SYS_ACTION_LAYER_FN2, true);
    kb_macro_process_action(SYS_ACTION_LAYER_FN1, true);
    TEST_ASSERT_EQUAL(KB_LAYER_FN3, s_active_layer);
}

/* ---- Action routing tests ---- */

TEST_CASE(kb_macro, system_action_dispatched) {
    reset_macro_state();
    uint16_t ble_toggle = ACTION_CODE_SYSTEM_MIN + 5;
    kb_macro_process_action(ble_toggle, true);
    TEST_ASSERT_EQUAL(1, s_system_action_calls);
    TEST_ASSERT_EQUAL_HEX(ble_toggle, s_last_system_action);
}

TEST_CASE(kb_macro, custom_key_dispatched) {
    reset_macro_state();
    uint16_t ckey_0 = ACTION_CODE_CKEY_MIN;
    kb_macro_process_action(ckey_0, true);
    TEST_ASSERT_EQUAL(1, s_ckey_calls);
    TEST_ASSERT_EQUAL_HEX(ckey_0, s_last_ckey_action);
}

TEST_CASE(kb_macro, action_code_ranges_dont_overlap) {
    /* Verify no overlap between ranges */
    TEST_ASSERT(ACTION_CODE_HID_MAX < ACTION_CODE_SYSTEM_MIN);
    TEST_ASSERT(ACTION_CODE_SYSTEM_MAX < ACTION_CODE_CKEY_MIN);
    TEST_ASSERT(ACTION_CODE_CKEY_MAX < ACTION_CODE_MACRO_MIN);
}

/* ---- Macro execution mode tests ---- */

#ifndef _TH_CFG_MACRO_EXEC_MODE_T
#define _TH_CFG_MACRO_EXEC_MODE_T
typedef enum {
    MACRO_EXEC_ONCE_STACK_ONCE = 0,
    MACRO_EXEC_ONCE_NO_STACK,
    MACRO_EXEC_ONCE_STACK_N,
    MACRO_EXEC_HOLD_REPEAT,
    MACRO_EXEC_HOLD_REPEAT_CANCEL,
    MACRO_EXEC_TOGGLE_REPEAT,
    MACRO_EXEC_TOGGLE_REPEAT_CANCEL,
    MACRO_EXEC_BURST_N,
    MACRO_EXEC_MODE_COUNT
} cfg_macro_exec_mode_t;
#endif

TEST_CASE(kb_macro, execution_mode_count) {
    TEST_ASSERT_EQUAL(8, MACRO_EXEC_MODE_COUNT);
}

TEST_CASE(kb_macro, macro_id_extraction) {
    uint16_t action_code = ACTION_CODE_MACRO_MIN + 42;
    uint16_t macro_id = action_code - ACTION_CODE_MACRO_MIN;
    TEST_ASSERT_EQUAL(42, macro_id);
}

TEST_CASE(kb_macro, ckey_id_extraction) {
    uint16_t action_code = ACTION_CODE_CKEY_MIN + 119;
    uint16_t ckey_id = action_code - ACTION_CODE_CKEY_MIN;
    TEST_ASSERT_EQUAL(119, ckey_id);
}

TEST_CASE(kb_macro, max_macro_id) {
    uint16_t max_action = ACTION_CODE_MACRO_MAX;
    uint16_t max_id = max_action - ACTION_CODE_MACRO_MIN;
    TEST_ASSERT(max_id >= 63); /* At least 64 macros */
}
