/**
 * @file test_harness.h
 * @brief Minimal C test framework for TEF-Oxide firmware host tests.
 *
 * Provides test registration, per-suite setup/teardown, assertions,
 * test filtering (-k), JUnit XML output (--junit), and a runner.
 * No external dependencies. Designed for any C99+ compiler.
 */
#pragma once

#ifdef _MSC_VER
  #ifndef _CRT_SECURE_NO_WARNINGS
    #define _CRT_SECURE_NO_WARNINGS
  #endif
  #pragma warning(disable: 4996) /* strncpy, strcpy warnings */
  #pragma warning(disable: 4100) /* unreferenced formal parameter */
  #pragma warning(disable: 4189) /* local variable initialized but not referenced */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <time.h>

/* Cross-platform packed struct macros */
#ifdef _MSC_VER
  #define PACKED_STRUCT_BEGIN __pragma(pack(push, 1))
  #define PACKED_STRUCT_END   __pragma(pack(pop))
  #define PACKED_ATTR
#else
  #define PACKED_STRUCT_BEGIN
  #define PACKED_STRUCT_END
  #define PACKED_ATTR __attribute__((packed))
#endif

/* Shared firmware constants via production headers + test extras */
#include "test_constants.h"

/* ======================================================================
 * Bookkeeping
 * ====================================================================== */

static int _th_tests_run    = 0;
static int _th_tests_passed = 0;
static int _th_tests_failed = 0;
static int _th_tests_skipped = 0;
static int _th_asserts      = 0;
static int _th_current_fail = 0;
static const char *_th_current_test = NULL;

/* Per-test failure message capture (for JUnit XML) */
static char _th_first_failure[512] = {0};

/* ======================================================================
 * Assertion macros
 * ====================================================================== */

/* Helper: capture first failure message for JUnit output */
#define _TH_CAPTURE_FAILURE(msg_fmt, ...) do { \
    if (!_th_current_fail) { \
        snprintf(_th_first_failure, sizeof(_th_first_failure), \
                 "%s:%d: " msg_fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while (0)

#define TEST_ASSERT(cond) do { \
    _th_asserts++; \
    if (!(cond)) { \
        printf("  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        _TH_CAPTURE_FAILURE("%s", #cond); \
        _th_current_fail = 1; \
    } \
} while (0)

#define TEST_ASSERT_MSG(cond, msg) do { \
    _th_asserts++; \
    if (!(cond)) { \
        printf("  FAIL: %s:%d: %s — %s\n", __FILE__, __LINE__, #cond, msg); \
        _TH_CAPTURE_FAILURE("%s — %s", #cond, msg); \
        _th_current_fail = 1; \
    } \
} while (0)

#define TEST_ASSERT_EQUAL(expected, actual) do { \
    _th_asserts++; \
    if ((expected) != (actual)) { \
        printf("  FAIL: %s:%d: expected %lld, got %lld\n", \
               __FILE__, __LINE__, (long long)(expected), (long long)(actual)); \
        _TH_CAPTURE_FAILURE("expected %lld, got %lld", (long long)(expected), (long long)(actual)); \
        _th_current_fail = 1; \
    } \
} while (0)

#define TEST_ASSERT_EQUAL_HEX(expected, actual) do { \
    _th_asserts++; \
    if ((expected) != (actual)) { \
        printf("  FAIL: %s:%d: expected 0x%llX, got 0x%llX\n", \
               __FILE__, __LINE__, (unsigned long long)(expected), (unsigned long long)(actual)); \
        _TH_CAPTURE_FAILURE("expected 0x%llX, got 0x%llX", (unsigned long long)(expected), (unsigned long long)(actual)); \
        _th_current_fail = 1; \
    } \
} while (0)

#define TEST_ASSERT_TRUE(cond)  TEST_ASSERT(cond)
#define TEST_ASSERT_FALSE(cond) TEST_ASSERT(!(cond))
#define TEST_ASSERT_NULL(ptr)   TEST_ASSERT((ptr) == NULL)
#define TEST_ASSERT_NOT_NULL(ptr) TEST_ASSERT((ptr) != NULL)

#define TEST_ASSERT_MEM_EQUAL(expected, actual, len) do { \
    _th_asserts++; \
    if (memcmp((expected), (actual), (len)) != 0) { \
        printf("  FAIL: %s:%d: memory mismatch (%zu bytes)\n", \
               __FILE__, __LINE__, (size_t)(len)); \
        _TH_CAPTURE_FAILURE("memory mismatch (%zu bytes)", (size_t)(len)); \
        _th_current_fail = 1; \
    } \
} while (0)

#define TEST_ASSERT_STR_EQUAL(expected, actual) do { \
    _th_asserts++; \
    if (strcmp((expected), (actual)) != 0) { \
        printf("  FAIL: %s:%d: expected \"%s\", got \"%s\"\n", \
               __FILE__, __LINE__, (expected), (actual)); \
        _TH_CAPTURE_FAILURE("expected \"%s\", got \"%s\"", (expected), (actual)); \
        _th_current_fail = 1; \
    } \
} while (0)

/* ======================================================================
 * Test registration
 * ====================================================================== */

typedef void (*test_fn_t)(void);

#define MAX_TESTS 1024

typedef struct {
    const char *suite;
    const char *name;
    test_fn_t   fn;
} _th_test_entry_t;

static _th_test_entry_t _th_tests[MAX_TESTS];
static int _th_test_count = 0;

static inline void _th_register(const char *suite, const char *name, test_fn_t fn) {
    if (_th_test_count < MAX_TESTS) {
        _th_tests[_th_test_count].suite = suite;
        _th_tests[_th_test_count].name  = name;
        _th_tests[_th_test_count].fn    = fn;
        _th_test_count++;
    }
}

/* Cross-compiler auto-registration */
#ifdef _MSC_VER
  #pragma section(".CRT$XCU", read)
  #define TEST_CASE(suite, name) \
      static void test_##suite##_##name(void); \
      static void _register_##suite##_##name(void) { \
          _th_register(#suite, #name, test_##suite##_##name); \
      } \
      __declspec(allocate(".CRT$XCU")) \
          static void (*_reg_ptr_##suite##_##name)(void) = _register_##suite##_##name; \
      static void test_##suite##_##name(void)
#else
  #define TEST_CASE(suite, name) \
      static void test_##suite##_##name(void); \
      __attribute__((constructor)) static void _register_##suite##_##name(void) { \
          _th_register(#suite, #name, test_##suite##_##name); \
      } \
      static void test_##suite##_##name(void)
#endif

/* ======================================================================
 * Per-suite setup / teardown
 * ====================================================================== */

#define MAX_SUITES 64

typedef struct {
    const char *suite;
    test_fn_t   setup;
    test_fn_t   teardown;
} _th_suite_hooks_t;

static _th_suite_hooks_t _th_hooks[MAX_SUITES];
static int _th_hook_count = 0;

static inline void _th_register_setup(const char *suite, test_fn_t fn) {
    for (int i = 0; i < _th_hook_count; i++) {
        if (strcmp(_th_hooks[i].suite, suite) == 0) {
            _th_hooks[i].setup = fn;
            return;
        }
    }
    if (_th_hook_count < MAX_SUITES) {
        _th_hooks[_th_hook_count].suite = suite;
        _th_hooks[_th_hook_count].setup = fn;
        _th_hooks[_th_hook_count].teardown = NULL;
        _th_hook_count++;
    }
}

static inline void _th_register_teardown(const char *suite, test_fn_t fn) {
    for (int i = 0; i < _th_hook_count; i++) {
        if (strcmp(_th_hooks[i].suite, suite) == 0) {
            _th_hooks[i].teardown = fn;
            return;
        }
    }
    if (_th_hook_count < MAX_SUITES) {
        _th_hooks[_th_hook_count].suite = suite;
        _th_hooks[_th_hook_count].setup = NULL;
        _th_hooks[_th_hook_count].teardown = fn;
        _th_hook_count++;
    }
}

static inline _th_suite_hooks_t *_th_find_hooks(const char *suite) {
    for (int i = 0; i < _th_hook_count; i++) {
        if (strcmp(_th_hooks[i].suite, suite) == 0) return &_th_hooks[i];
    }
    return NULL;
}

/* Registration macros for setup/teardown */
#ifdef _MSC_VER
  #define TEST_SETUP(suite_name) \
      static void _setup_##suite_name(void); \
      static void _register_setup_##suite_name(void) { \
          _th_register_setup(#suite_name, _setup_##suite_name); \
      } \
      __declspec(allocate(".CRT$XCU")) \
          static void (*_reg_setup_ptr_##suite_name)(void) = _register_setup_##suite_name; \
      static void _setup_##suite_name(void)

  #define TEST_TEARDOWN(suite_name) \
      static void _teardown_##suite_name(void); \
      static void _register_teardown_##suite_name(void) { \
          _th_register_teardown(#suite_name, _teardown_##suite_name); \
      } \
      __declspec(allocate(".CRT$XCU")) \
          static void (*_reg_td_ptr_##suite_name)(void) = _register_teardown_##suite_name; \
      static void _teardown_##suite_name(void)
#else
  #define TEST_SETUP(suite_name) \
      static void _setup_##suite_name(void); \
      __attribute__((constructor)) static void _register_setup_##suite_name(void) { \
          _th_register_setup(#suite_name, _setup_##suite_name); \
      } \
      static void _setup_##suite_name(void)

  #define TEST_TEARDOWN(suite_name) \
      static void _teardown_##suite_name(void); \
      __attribute__((constructor)) static void _register_teardown_##suite_name(void) { \
          _th_register_teardown(#suite_name, _teardown_##suite_name); \
      } \
      static void _teardown_##suite_name(void)
#endif

/* ======================================================================
 * Runner
 * ====================================================================== */

/* Filter: substring match on suite or test name */
static inline bool _th_matches(const char *suite, const char *name, const char *filter) {
    if (!filter) return true;
    return strstr(suite, filter) != NULL || strstr(name, filter) != NULL;
}

/* JUnit XML writer */
static inline void _th_write_junit(const char *filename,
                                    _th_test_entry_t *tests, int count,
                                    double *elapsed, bool *passed, char failures[][512]) {
    FILE *f = fopen(filename, "w");
    if (!f) { printf("Warning: could not write %s\n", filename); return; }

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<testsuites tests=\"%d\" failures=\"%d\" time=\"0\">\n",
            _th_tests_run, _th_tests_failed);

    const char *cur_suite = NULL;
    int suite_tests = 0, suite_fails = 0;

    for (int i = 0; i <= count; i++) {
        bool new_suite = (i == count) ||
                         (!cur_suite || strcmp(cur_suite, tests[i].suite) != 0);

        if (new_suite && cur_suite) {
            /* Close previous suite */
            fprintf(f, "  </testsuite>\n");
        }

        if (i == count) break;

        if (!cur_suite || strcmp(cur_suite, tests[i].suite) != 0) {
            cur_suite = tests[i].suite;
            suite_tests = 0; suite_fails = 0;
            /* Count suite stats */
            for (int j = i; j < count && strcmp(tests[j].suite, cur_suite) == 0; j++) {
                suite_tests++;
                if (!passed[j]) suite_fails++;
            }
            fprintf(f, "  <testsuite name=\"%s\" tests=\"%d\" failures=\"%d\">\n",
                    cur_suite, suite_tests, suite_fails);
        }

        fprintf(f, "    <testcase name=\"%s\" classname=\"%s\" time=\"%.4f\"",
                tests[i].name, tests[i].suite, elapsed[i]);
        if (!passed[i]) {
            fprintf(f, ">\n      <failure message=\"%s\"/>\n    </testcase>\n",
                    failures[i]);
        } else {
            fprintf(f, "/>\n");
        }
    }

    fprintf(f, "</testsuites>\n");
    fclose(f);
    printf("JUnit XML written to %s\n", filename);
}

static inline int test_run_all(int argc, char *argv[]) {
    /* Parse arguments */
    const char *filter = NULL;
    bool junit = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
            filter = argv[++i];
        } else if (strcmp(argv[i], "--junit") == 0) {
            junit = true;
        }
    }

    /* Allocate per-test result arrays */
    static double test_elapsed[MAX_TESTS];
    static bool   test_passed[MAX_TESTS];
    static char   test_failures[MAX_TESTS][512];
    memset(test_elapsed, 0, sizeof(test_elapsed));
    memset(test_passed, 0, sizeof(test_passed));
    memset(test_failures, 0, sizeof(test_failures));

    printf("\n========================================\n");
    printf(" TEF-Oxide Firmware Test Suite\n");
    printf("========================================\n");
    if (filter) printf(" Filter: \"%s\"\n", filter);
    printf("\n");

    const char *current_suite = NULL;

    for (int i = 0; i < _th_test_count; i++) {
        /* Filter check */
        if (!_th_matches(_th_tests[i].suite, _th_tests[i].name, filter)) {
            _th_tests_skipped++;
            test_passed[i] = true;
            continue;
        }

        if (!current_suite || strcmp(current_suite, _th_tests[i].suite) != 0) {
            current_suite = _th_tests[i].suite;
            printf("[%s]\n", current_suite);
        }

        _th_current_fail = 0;
        _th_first_failure[0] = '\0';
        _th_current_test = _th_tests[i].name;
        _th_tests_run++;

        /* Setup */
        _th_suite_hooks_t *hooks = _th_find_hooks(_th_tests[i].suite);
        if (hooks && hooks->setup) hooks->setup();

        /* Run with timing */
        clock_t t0 = clock();
        _th_tests[i].fn();
        clock_t t1 = clock();
        test_elapsed[i] = (double)(t1 - t0) / CLOCKS_PER_SEC;

        /* Teardown */
        if (hooks && hooks->teardown) hooks->teardown();

        if (_th_current_fail) {
            _th_tests_failed++;
            test_passed[i] = false;
            strncpy(test_failures[i], _th_first_failure, 511);
            printf("  FAILED: %s\n", _th_tests[i].name);
        } else {
            _th_tests_passed++;
            test_passed[i] = true;
            printf("  PASSED: %s\n", _th_tests[i].name);
        }
    }

    printf("\n========================================\n");
    printf(" Results: %d/%d passed, %d failed",
           _th_tests_passed, _th_tests_run, _th_tests_failed);
    if (_th_tests_skipped > 0) printf(", %d skipped", _th_tests_skipped);
    printf(" (%d assertions)\n", _th_asserts);
    printf("========================================\n\n");

    if (junit) {
        _th_write_junit("test_results.xml", _th_tests, _th_test_count,
                         test_elapsed, test_passed, test_failures);
    }

    return _th_tests_failed > 0 ? 1 : 0;
}
