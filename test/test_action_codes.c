/**
 * @file test_action_codes.c
 * @brief Tests for the 16-bit action code system — range boundaries,
 *        non-overlapping ranges, ID extraction, and constants.
 */
#include "test_harness.h"

/* ---- Action code constants (guarded — may already be defined in same TU) ---- */

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

#ifndef ACTION_CODE_MEDIA_MIN
#define ACTION_CODE_MEDIA_MIN  0x0100
#define ACTION_CODE_MEDIA_MAX  0x01FF
#endif

#ifndef KB_KEY_TRANSPARENT
#define KB_KEY_TRANSPARENT     0xFFFF
#endif

#ifndef SYS_ACTION_LAYER_BASE
#define SYS_ACTION_LAYER_BASE  (ACTION_CODE_SYSTEM_MIN + 0)
#endif
#ifndef SYS_ACTION_LAYER_FN1
#define SYS_ACTION_LAYER_FN1   (ACTION_CODE_SYSTEM_MIN + 1)
#define SYS_ACTION_LAYER_FN2   (ACTION_CODE_SYSTEM_MIN + 2)
#endif
#ifndef SYS_ACTION_BLE_ON
#define SYS_ACTION_BLE_ON      (ACTION_CODE_SYSTEM_MIN + 3)
#define SYS_ACTION_BLE_OFF     (ACTION_CODE_SYSTEM_MIN + 4)
#define SYS_ACTION_BLE_TOGGLE  (ACTION_CODE_SYSTEM_MIN + 5)
#define SYS_ACTION_BLE_1       (ACTION_CODE_SYSTEM_MIN + 6)
#define SYS_ACTION_BLE_9       (ACTION_CODE_SYSTEM_MIN + 14)
#define SYS_ACTION_VOLUME_UP   (ACTION_CODE_SYSTEM_MIN + 18)
#define SYS_ACTION_VOLUME_DOWN (ACTION_CODE_SYSTEM_MIN + 19)
#define SYS_ACTION_MUTE        (ACTION_CODE_SYSTEM_MIN + 20)
#define MEDIA_ACTION_NEXT      (ACTION_CODE_SYSTEM_MIN + 21)
#define MEDIA_ACTION_PREV      (ACTION_CODE_SYSTEM_MIN + 22)
#define MEDIA_ACTION_TOGGLE    (ACTION_CODE_SYSTEM_MIN + 23)
#endif

/* ---- Tests ---- */

TEST_CASE(action_codes, ranges_dont_overlap) {
    TEST_ASSERT(ACTION_CODE_HID_MAX < ACTION_CODE_MEDIA_MIN);
    TEST_ASSERT(ACTION_CODE_MEDIA_MAX < ACTION_CODE_SYSTEM_MIN);
    TEST_ASSERT(ACTION_CODE_SYSTEM_MAX < ACTION_CODE_CKEY_MIN);
    TEST_ASSERT(ACTION_CODE_CKEY_MAX < ACTION_CODE_MACRO_MIN);
}

TEST_CASE(action_codes, none_is_zero) {
    TEST_ASSERT_EQUAL(0, ACTION_CODE_NONE);
}

TEST_CASE(action_codes, transparent_is_max_uint16) {
    TEST_ASSERT_EQUAL_HEX(0xFFFF, KB_KEY_TRANSPARENT);
}

TEST_CASE(action_codes, hid_range) {
    TEST_ASSERT_EQUAL_HEX(0x0001, ACTION_CODE_HID_MIN);
    TEST_ASSERT_EQUAL_HEX(0x00FF, ACTION_CODE_HID_MAX);
    /* 255 possible HID keycodes */
    TEST_ASSERT_EQUAL(255, ACTION_CODE_HID_MAX - ACTION_CODE_HID_MIN + 1);
}

TEST_CASE(action_codes, media_range) {
    TEST_ASSERT_EQUAL_HEX(0x0100, ACTION_CODE_MEDIA_MIN);
    TEST_ASSERT_EQUAL_HEX(0x01FF, ACTION_CODE_MEDIA_MAX);
}

TEST_CASE(action_codes, system_range) {
    TEST_ASSERT_EQUAL_HEX(0x2000, ACTION_CODE_SYSTEM_MIN);
    TEST_ASSERT_EQUAL_HEX(0x20FF, ACTION_CODE_SYSTEM_MAX);
    /* 256 possible system actions */
    TEST_ASSERT_EQUAL(256, ACTION_CODE_SYSTEM_MAX - ACTION_CODE_SYSTEM_MIN + 1);
}

TEST_CASE(action_codes, ckey_range_capacity) {
    uint16_t capacity = ACTION_CODE_CKEY_MAX - ACTION_CODE_CKEY_MIN + 1;
    TEST_ASSERT(capacity >= 120); /* Must support 120 custom keys */
    TEST_ASSERT_EQUAL(4096, capacity);
}

TEST_CASE(action_codes, macro_range_capacity) {
    uint16_t capacity = ACTION_CODE_MACRO_MAX - ACTION_CODE_MACRO_MIN + 1;
    TEST_ASSERT(capacity >= 64); /* Must support 64 macros */
    TEST_ASSERT_EQUAL(4096, capacity);
}

TEST_CASE(action_codes, layer_actions_sequential) {
    TEST_ASSERT_EQUAL_HEX(0x2000, SYS_ACTION_LAYER_BASE);
    TEST_ASSERT_EQUAL_HEX(0x2001, SYS_ACTION_LAYER_FN1);
    TEST_ASSERT_EQUAL_HEX(0x2002, SYS_ACTION_LAYER_FN2);
}

TEST_CASE(action_codes, ble_profile_actions) {
    TEST_ASSERT_EQUAL_HEX(0x2006, SYS_ACTION_BLE_1);
    TEST_ASSERT_EQUAL_HEX(0x200E, SYS_ACTION_BLE_9);
    /* 9 profiles: BLE_1 to BLE_9 */
    TEST_ASSERT_EQUAL(9, SYS_ACTION_BLE_9 - SYS_ACTION_BLE_1 + 1);
}

TEST_CASE(action_codes, media_actions_within_system_range) {
    TEST_ASSERT(SYS_ACTION_VOLUME_UP >= ACTION_CODE_SYSTEM_MIN);
    TEST_ASSERT(MEDIA_ACTION_TOGGLE <= ACTION_CODE_SYSTEM_MAX);
}

TEST_CASE(action_codes, media_actions_contiguous) {
    TEST_ASSERT_EQUAL(SYS_ACTION_VOLUME_UP + 1, SYS_ACTION_VOLUME_DOWN);
    TEST_ASSERT_EQUAL(SYS_ACTION_VOLUME_DOWN + 1, SYS_ACTION_MUTE);
    TEST_ASSERT_EQUAL(SYS_ACTION_MUTE + 1, MEDIA_ACTION_NEXT);
    TEST_ASSERT_EQUAL(MEDIA_ACTION_NEXT + 1, MEDIA_ACTION_PREV);
    TEST_ASSERT_EQUAL(MEDIA_ACTION_PREV + 1, MEDIA_ACTION_TOGGLE);
}

TEST_CASE(action_codes, macro_id_extraction) {
    for (uint16_t id = 0; id < 64; id++) {
        uint16_t code = ACTION_CODE_MACRO_MIN + id;
        TEST_ASSERT_EQUAL(id, code - ACTION_CODE_MACRO_MIN);
        TEST_ASSERT(code >= ACTION_CODE_MACRO_MIN);
        TEST_ASSERT(code <= ACTION_CODE_MACRO_MAX);
    }
}

TEST_CASE(action_codes, ckey_id_extraction) {
    for (uint16_t id = 0; id < 120; id++) {
        uint16_t code = ACTION_CODE_CKEY_MIN + id;
        TEST_ASSERT_EQUAL(id, code - ACTION_CODE_CKEY_MIN);
        TEST_ASSERT(code >= ACTION_CODE_CKEY_MIN);
        TEST_ASSERT(code <= ACTION_CODE_CKEY_MAX);
    }
}

TEST_CASE(action_codes, all_fit_uint16) {
    /* Every action code must fit in 16 bits */
    TEST_ASSERT(ACTION_CODE_MACRO_MAX <= 0xFFFF);
    TEST_ASSERT(KB_KEY_TRANSPARENT <= 0xFFFF);
}
