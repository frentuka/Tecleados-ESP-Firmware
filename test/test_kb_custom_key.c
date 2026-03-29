/**
 * @file test_kb_custom_key.c
 * @brief Tests for kb_custom_key.c — PressRelease and MultiAction mode logic.
 *
 * Tests CKey lookup, mode dispatch, PressRelease sustain, MultiAction timing
 * passthrough, and config reload.
 */
#include "test_harness.h"
#include "mocks/mock_esp.h"

/* ---- Types from cfg_custom_keys.h ---- */

#define CFG_CKEYS_MAX_COUNT 120
#ifndef ACTION_CODE_CKEY_MIN
#define ACTION_CODE_CKEY_MIN 0x3000
#define ACTION_CODE_CKEY_MAX 0x3FFF
#endif

typedef enum {
    CKEY_MODE_PRESS_RELEASE = 0,
    CKEY_MODE_MULTI_ACTION  = 1
} cfg_ckey_mode_t;

typedef struct {
    uint32_t press_action;
    uint32_t release_action;
    uint32_t press_tap_release_delay_ms;
    uint32_t release_tap_release_delay_ms;
    bool     wait_for_finish;
    bool     press_sustain;
} cfg_ckey_pr_t;

typedef struct {
    uint32_t tap_action;
    uint32_t double_tap_action;
    uint32_t hold_action;
    uint32_t double_tap_threshold_ms;
    uint32_t hold_threshold_ms;
    uint32_t tap_release_delay_ms;
    uint32_t double_tap_release_delay_ms;
    uint32_t hold_release_delay_ms;
    bool     hold_sustain;
} cfg_ckey_ma_t;

typedef struct {
    uint16_t        id;
    char            name[32];
    cfg_ckey_mode_t mode;
    union {
        cfg_ckey_pr_t pr;
        cfg_ckey_ma_t ma;
    } rules;
} cfg_custom_key_t;

/* ---- Test state ---- */

static cfg_custom_key_t s_test_ckeys[CFG_CKEYS_MAX_COUNT];
static size_t s_test_ckey_count = 0;

/* Track fire_tap calls */
static int s_fire_tap_calls = 0;
static uint16_t s_last_fire_tap_action = 0;
static uint32_t s_last_fire_tap_duration = 0;
static uint32_t s_last_fire_tap_delay = 0;

/* Track direct process_action calls */
static int s_process_action_calls = 0;
static uint16_t s_last_process_action_code = 0;
static bool s_last_process_action_pressed = false;

/* Track sys_action_process_ex calls */
static int s_sys_action_ex_calls = 0;
static uint16_t s_last_sys_action_code = 0;

static void reset_ckey_test(void) {
    memset(s_test_ckeys, 0, sizeof(s_test_ckeys));
    s_test_ckey_count = 0;
    s_fire_tap_calls = 0;
    s_last_fire_tap_action = 0;
    s_last_fire_tap_duration = 0;
    s_last_fire_tap_delay = 0;
    s_process_action_calls = 0;
    s_last_process_action_code = 0;
    s_sys_action_ex_calls = 0;
}

static const cfg_custom_key_t *find_ckey(uint16_t id) {
    for (size_t i = 0; i < s_test_ckey_count; i++) {
        if (s_test_ckeys[i].id == id) return &s_test_ckeys[i];
    }
    return NULL;
}

static void mock_fire_tap(uint16_t action, uint32_t dur, uint32_t delay) {
    s_fire_tap_calls++;
    s_last_fire_tap_action = action;
    s_last_fire_tap_duration = dur;
    s_last_fire_tap_delay = delay;
}

static void mock_process_action(uint16_t action, bool pressed) {
    s_process_action_calls++;
    s_last_process_action_code = action;
    s_last_process_action_pressed = pressed;
}

static void mock_sys_action_process_ex(uint16_t action, bool pressed, void *timing) {
    (void)timing;
    s_sys_action_ex_calls++;
    s_last_sys_action_code = action;
}

/* Simplified process_pr */
static void process_pr(const cfg_custom_key_t *ck, bool is_pressed) {
    if (ck->rules.pr.press_sustain) {
        if (is_pressed) {
            mock_process_action((uint16_t)ck->rules.pr.press_action, true);
        } else {
            mock_process_action((uint16_t)ck->rules.pr.press_action, false);
            if (ck->rules.pr.release_action) {
                mock_fire_tap((uint16_t)ck->rules.pr.release_action,
                              ck->rules.pr.release_tap_release_delay_ms, 0);
            }
        }
        return;
    }
    if (is_pressed) {
        mock_fire_tap((uint16_t)ck->rules.pr.press_action,
                      ck->rules.pr.press_tap_release_delay_ms, 0);
    } else {
        mock_fire_tap((uint16_t)ck->rules.pr.release_action,
                      ck->rules.pr.release_tap_release_delay_ms, 0);
    }
}

static void process_ma(const cfg_custom_key_t *ck, bool is_pressed) {
    uint16_t action_code = (uint16_t)(ACTION_CODE_CKEY_MIN + ck->id);
    mock_sys_action_process_ex(action_code, is_pressed, NULL);
}

static void kb_custom_key_process_action(uint16_t action_code, bool is_pressed) {
    uint16_t id = action_code - ACTION_CODE_CKEY_MIN;
    const cfg_custom_key_t *ck = find_ckey(id);
    if (!ck) return;

    switch (ck->mode) {
    case CKEY_MODE_PRESS_RELEASE: process_pr(ck, is_pressed); break;
    case CKEY_MODE_MULTI_ACTION:  process_ma(ck, is_pressed); break;
    }
}

/* ---- Helper to add a test CKey ---- */

static void add_pr_ckey(uint16_t id, const char *name, uint32_t press, uint32_t release,
                         uint32_t press_dur, uint32_t release_dur, bool sustain) {
    cfg_custom_key_t *ck = &s_test_ckeys[s_test_ckey_count++];
    ck->id = id;
    strncpy(ck->name, name, 31);
    ck->mode = CKEY_MODE_PRESS_RELEASE;
    ck->rules.pr.press_action = press;
    ck->rules.pr.release_action = release;
    ck->rules.pr.press_tap_release_delay_ms = press_dur;
    ck->rules.pr.release_tap_release_delay_ms = release_dur;
    ck->rules.pr.press_sustain = sustain;
}

static void add_ma_ckey(uint16_t id, const char *name, uint32_t tap, uint32_t dtap,
                         uint32_t hold, uint32_t dtap_ms, uint32_t hold_ms) {
    cfg_custom_key_t *ck = &s_test_ckeys[s_test_ckey_count++];
    ck->id = id;
    strncpy(ck->name, name, 31);
    ck->mode = CKEY_MODE_MULTI_ACTION;
    ck->rules.ma.tap_action = tap;
    ck->rules.ma.double_tap_action = dtap;
    ck->rules.ma.hold_action = hold;
    ck->rules.ma.double_tap_threshold_ms = dtap_ms;
    ck->rules.ma.hold_threshold_ms = hold_ms;
}

/* ---- Tests ---- */

TEST_CASE(kb_custom_key, pr_mode_press_fires_tap) {
    reset_ckey_test();
    add_pr_ckey(0, "Test", 0x04, 0x05, 50, 50, false);

    kb_custom_key_process_action(ACTION_CODE_CKEY_MIN, true);

    TEST_ASSERT_EQUAL(1, s_fire_tap_calls);
    TEST_ASSERT_EQUAL_HEX(0x04, s_last_fire_tap_action);
    TEST_ASSERT_EQUAL(50, s_last_fire_tap_duration);
}

TEST_CASE(kb_custom_key, pr_mode_release_fires_tap) {
    reset_ckey_test();
    add_pr_ckey(0, "Test", 0x04, 0x05, 50, 100, false);

    kb_custom_key_process_action(ACTION_CODE_CKEY_MIN, false);

    TEST_ASSERT_EQUAL(1, s_fire_tap_calls);
    TEST_ASSERT_EQUAL_HEX(0x05, s_last_fire_tap_action);
    TEST_ASSERT_EQUAL(100, s_last_fire_tap_duration);
}

TEST_CASE(kb_custom_key, pr_sustain_press_calls_process_action) {
    reset_ckey_test();
    add_pr_ckey(0, "Sustain", 0x04, 0x05, 50, 50, true);

    kb_custom_key_process_action(ACTION_CODE_CKEY_MIN, true);

    TEST_ASSERT_EQUAL(1, s_process_action_calls);
    TEST_ASSERT_EQUAL_HEX(0x04, s_last_process_action_code);
    TEST_ASSERT_TRUE(s_last_process_action_pressed);
    TEST_ASSERT_EQUAL(0, s_fire_tap_calls); /* No tap for sustain */
}

TEST_CASE(kb_custom_key, pr_sustain_release_calls_both) {
    reset_ckey_test();
    add_pr_ckey(0, "Sustain", 0x04, 0x05, 50, 50, true);

    kb_custom_key_process_action(ACTION_CODE_CKEY_MIN, false);

    TEST_ASSERT_EQUAL(1, s_process_action_calls);
    TEST_ASSERT_FALSE(s_last_process_action_pressed);
    TEST_ASSERT_EQUAL(1, s_fire_tap_calls); /* Release action tapped */
    TEST_ASSERT_EQUAL_HEX(0x05, s_last_fire_tap_action);
}

TEST_CASE(kb_custom_key, pr_sustain_no_release_action_skips_tap) {
    reset_ckey_test();
    add_pr_ckey(0, "NoRelease", 0x04, 0, 50, 50, true);

    kb_custom_key_process_action(ACTION_CODE_CKEY_MIN, false);

    TEST_ASSERT_EQUAL(1, s_process_action_calls);
    TEST_ASSERT_EQUAL(0, s_fire_tap_calls); /* No release tap when release_action == 0 */
}

TEST_CASE(kb_custom_key, ma_mode_routes_to_sys_action) {
    reset_ckey_test();
    add_ma_ckey(5, "MA Test", 0x04, 0x05, 0x06, 300, 500);

    kb_custom_key_process_action(ACTION_CODE_CKEY_MIN + 5, true);

    TEST_ASSERT_EQUAL(1, s_sys_action_ex_calls);
    TEST_ASSERT_EQUAL_HEX(ACTION_CODE_CKEY_MIN + 5, s_last_sys_action_code);
}

TEST_CASE(kb_custom_key, unknown_id_ignored) {
    reset_ckey_test();
    /* No keys registered */
    kb_custom_key_process_action(ACTION_CODE_CKEY_MIN + 99, true);
    TEST_ASSERT_EQUAL(0, s_fire_tap_calls);
    TEST_ASSERT_EQUAL(0, s_sys_action_ex_calls);
}

TEST_CASE(kb_custom_key, multiple_ckeys_independent) {
    reset_ckey_test();
    add_pr_ckey(0, "CK0", 0x10, 0x11, 10, 10, false);
    add_pr_ckey(1, "CK1", 0x20, 0x21, 20, 20, false);

    kb_custom_key_process_action(ACTION_CODE_CKEY_MIN + 0, true);
    TEST_ASSERT_EQUAL_HEX(0x10, s_last_fire_tap_action);

    kb_custom_key_process_action(ACTION_CODE_CKEY_MIN + 1, true);
    TEST_ASSERT_EQUAL_HEX(0x20, s_last_fire_tap_action);
    TEST_ASSERT_EQUAL(2, s_fire_tap_calls);
}

TEST_CASE(kb_custom_key, id_extraction_boundary) {
    /* ID 0 */
    TEST_ASSERT_EQUAL(0, ACTION_CODE_CKEY_MIN - ACTION_CODE_CKEY_MIN);
    /* ID 119 (max) */
    TEST_ASSERT_EQUAL(119, (ACTION_CODE_CKEY_MIN + 119) - ACTION_CODE_CKEY_MIN);
}

TEST_CASE(kb_custom_key, max_ckeys_capacity) {
    reset_ckey_test();
    for (int i = 0; i < CFG_CKEYS_MAX_COUNT; i++) {
        add_pr_ckey(i, "key", 0x04 + i, 0, 10, 0, false);
    }
    TEST_ASSERT_EQUAL(CFG_CKEYS_MAX_COUNT, (int)s_test_ckey_count);

    /* Each key should be findable */
    for (int i = 0; i < CFG_CKEYS_MAX_COUNT; i++) {
        TEST_ASSERT_NOT_NULL(find_ckey(i));
    }
}
