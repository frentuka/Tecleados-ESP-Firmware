/**
 * @file test_macros_config.c
 * @brief Tests for cfg_macros.h — macro struct layout, event types,
 *        execution modes, limits, and index bitmask operations.
 */
#include "test_harness.h"
#include "mocks/mock_esp.h"

/* ---- Types from cfg_macros.h ---- */

#ifndef CFG_MACRO_MAX_EVENTS
#define CFG_MACRO_MAX_EVENTS 256
#define CFG_MACROS_MAX_COUNT 64
#endif

#ifndef _TH_CFG_MACRO_EVENT_TYPE_T
#define _TH_CFG_MACRO_EVENT_TYPE_T
typedef enum {
    MACRO_EVT_NONE = 0,
    MACRO_EVT_KEY_PRESS,
    MACRO_EVT_KEY_RELEASE,
    MACRO_EVT_DELAY_MS,
    MACRO_EVT_KEY_TAP
} cfg_macro_event_type_t;
#endif

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

typedef struct {
    cfg_macro_event_type_t type;
    uint32_t value;
    uint32_t delay_ms;
    uint32_t press_duration_ms;
} cfg_macro_event_t;

typedef struct {
    uint16_t id;
    char name[32];
    cfg_macro_event_t events[CFG_MACRO_MAX_EVENTS];
    size_t event_count;
    uint8_t exec_mode;
    uint8_t stack_max;
    uint8_t repeat_count;
} cfg_macro_t;

typedef struct {
    cfg_macro_t macros[CFG_MACROS_MAX_COUNT];
    size_t count;
} cfg_macro_list_t;

typedef struct {
    uint64_t active_mask;
} cfg_macro_index_t;

/* ---- Tests ---- */

TEST_CASE(macros_config, max_event_count) {
    TEST_ASSERT_EQUAL(256, CFG_MACRO_MAX_EVENTS);
}

TEST_CASE(macros_config, max_macro_count) {
    TEST_ASSERT_EQUAL(64, CFG_MACROS_MAX_COUNT);
}

TEST_CASE(macros_config, execution_mode_count) {
    TEST_ASSERT_EQUAL(8, MACRO_EXEC_MODE_COUNT);
}

TEST_CASE(macros_config, event_types) {
    TEST_ASSERT_EQUAL(0, MACRO_EVT_NONE);
    TEST_ASSERT_EQUAL(1, MACRO_EVT_KEY_PRESS);
    TEST_ASSERT_EQUAL(2, MACRO_EVT_KEY_RELEASE);
    TEST_ASSERT_EQUAL(3, MACRO_EVT_DELAY_MS);
    TEST_ASSERT_EQUAL(4, MACRO_EVT_KEY_TAP);
}

TEST_CASE(macros_config, event_struct_layout) {
    cfg_macro_event_t evt = {
        .type = MACRO_EVT_KEY_TAP,
        .value = 0x04,
        .delay_ms = 50,
        .press_duration_ms = 100
    };
    TEST_ASSERT_EQUAL(MACRO_EVT_KEY_TAP, evt.type);
    TEST_ASSERT_EQUAL(0x04, evt.value);
    TEST_ASSERT_EQUAL(50, evt.delay_ms);
    TEST_ASSERT_EQUAL(100, evt.press_duration_ms);
}

TEST_CASE(macros_config, macro_name_length) {
    cfg_macro_t m;
    strncpy(m.name, "1234567890123456789012345678901", 31);
    m.name[31] = '\0';
    TEST_ASSERT_EQUAL(31, strlen(m.name));
}

TEST_CASE(macros_config, index_mask_single_bit) {
    cfg_macro_index_t idx = { .active_mask = 0 };
    uint16_t macro_id = 5;
    idx.active_mask |= (1ULL << macro_id);

    TEST_ASSERT(idx.active_mask & (1ULL << 5));
    TEST_ASSERT(!(idx.active_mask & (1ULL << 4)));
}

TEST_CASE(macros_config, index_mask_all_64) {
    cfg_macro_index_t idx = { .active_mask = 0 };
    for (int i = 0; i < 64; i++) {
        idx.active_mask |= (1ULL << i);
    }
    TEST_ASSERT_EQUAL_HEX(0xFFFFFFFFFFFFFFFFULL, idx.active_mask);
}

TEST_CASE(macros_config, index_mask_clear_bit) {
    cfg_macro_index_t idx = { .active_mask = 0xFFFFFFFFFFFFFFFFULL };
    uint16_t id = 32;
    idx.active_mask &= ~(1ULL << id);
    TEST_ASSERT(!(idx.active_mask & (1ULL << 32)));
    TEST_ASSERT(idx.active_mask & (1ULL << 31));
    TEST_ASSERT(idx.active_mask & (1ULL << 33));
}

TEST_CASE(macros_config, macro_list_zero_init) {
    cfg_macro_list_t list;
    memset(&list, 0, sizeof(list));
    TEST_ASSERT_EQUAL(0, list.count);
    TEST_ASSERT_EQUAL(0, list.macros[0].id);
}

TEST_CASE(macros_config, exec_mode_values) {
    TEST_ASSERT_EQUAL(0, MACRO_EXEC_ONCE_STACK_ONCE);
    TEST_ASSERT_EQUAL(1, MACRO_EXEC_ONCE_NO_STACK);
    TEST_ASSERT_EQUAL(2, MACRO_EXEC_ONCE_STACK_N);
    TEST_ASSERT_EQUAL(3, MACRO_EXEC_HOLD_REPEAT);
    TEST_ASSERT_EQUAL(4, MACRO_EXEC_HOLD_REPEAT_CANCEL);
    TEST_ASSERT_EQUAL(5, MACRO_EXEC_TOGGLE_REPEAT);
    TEST_ASSERT_EQUAL(6, MACRO_EXEC_TOGGLE_REPEAT_CANCEL);
    TEST_ASSERT_EQUAL(7, MACRO_EXEC_BURST_N);
}

TEST_CASE(macros_config, macro_struct_has_stack_max) {
    cfg_macro_t m = {0};
    m.exec_mode = MACRO_EXEC_ONCE_STACK_N;
    m.stack_max = 5;
    TEST_ASSERT_EQUAL(5, m.stack_max);
}

TEST_CASE(macros_config, macro_struct_has_repeat_count) {
    cfg_macro_t m = {0};
    m.exec_mode = MACRO_EXEC_BURST_N;
    m.repeat_count = 10;
    TEST_ASSERT_EQUAL(10, m.repeat_count);
}

TEST_CASE(macros_config, nested_macro_events) {
    /* A macro can reference another macro via ACTION_CODE_MACRO_MIN + id */
    cfg_macro_event_t evt = {
        .type = MACRO_EVT_KEY_PRESS,
        .value = 0x4000 + 5, /* Macro ID 5 */
    };
    TEST_ASSERT_EQUAL(0x4005, evt.value);
    TEST_ASSERT(evt.value >= 0x4000 && evt.value <= 0x4FFF);
}
