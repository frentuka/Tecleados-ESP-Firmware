/**
 * @file test_harness.h
 * @brief Minimal C test framework for DF-ONE firmware host tests.
 *
 * Provides test registration, assertions, and a runner — no external
 * dependencies. Designed to compile with any C99 compiler on any platform.
 */
#pragma once

#ifdef _MSC_VER
  #ifndef _CRT_SECURE_NO_WARNINGS
    #define _CRT_SECURE_NO_WARNINGS
  #endif
  #pragma warning(disable: 4996) /* strncpy, strcpy warnings */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

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

/* ======================================================================
 * Bookkeeping
 * ====================================================================== */

static int _th_tests_run    = 0;
static int _th_tests_passed = 0;
static int _th_tests_failed = 0;
static int _th_asserts      = 0;
static int _th_current_fail = 0;
static const char *_th_current_test = NULL;

/* ======================================================================
 * Assertion macros
 * ====================================================================== */

#define TEST_ASSERT(cond) do { \
    _th_asserts++; \
    if (!(cond)) { \
        printf("  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        _th_current_fail = 1; \
    } \
} while (0)

#define TEST_ASSERT_MSG(cond, msg) do { \
    _th_asserts++; \
    if (!(cond)) { \
        printf("  FAIL: %s:%d: %s — %s\n", __FILE__, __LINE__, #cond, msg); \
        _th_current_fail = 1; \
    } \
} while (0)

#define TEST_ASSERT_EQUAL(expected, actual) do { \
    _th_asserts++; \
    if ((expected) != (actual)) { \
        printf("  FAIL: %s:%d: expected %lld, got %lld\n", \
               __FILE__, __LINE__, (long long)(expected), (long long)(actual)); \
        _th_current_fail = 1; \
    } \
} while (0)

#define TEST_ASSERT_EQUAL_HEX(expected, actual) do { \
    _th_asserts++; \
    if ((expected) != (actual)) { \
        printf("  FAIL: %s:%d: expected 0x%llX, got 0x%llX\n", \
               __FILE__, __LINE__, (unsigned long long)(expected), (unsigned long long)(actual)); \
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
        _th_current_fail = 1; \
    } \
} while (0)

#define TEST_ASSERT_STR_EQUAL(expected, actual) do { \
    _th_asserts++; \
    if (strcmp((expected), (actual)) != 0) { \
        printf("  FAIL: %s:%d: expected \"%s\", got \"%s\"\n", \
               __FILE__, __LINE__, (expected), (actual)); \
        _th_current_fail = 1; \
    } \
} while (0)

/* ======================================================================
 * Test registration and execution
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

/* Cross-compiler auto-registration: GCC/Clang use __attribute__((constructor)),
   MSVC uses .CRT$XCU section to run functions before main(). */
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
 * Runner
 * ====================================================================== */

static inline int test_run_all(void) {
    printf("\n========================================\n");
    printf(" DF-ONE Firmware Test Suite\n");
    printf("========================================\n\n");

    const char *current_suite = NULL;

    for (int i = 0; i < _th_test_count; i++) {
        if (!current_suite || strcmp(current_suite, _th_tests[i].suite) != 0) {
            current_suite = _th_tests[i].suite;
            printf("[%s]\n", current_suite);
        }

        _th_current_fail = 0;
        _th_current_test = _th_tests[i].name;
        _th_tests_run++;

        _th_tests[i].fn();

        if (_th_current_fail) {
            _th_tests_failed++;
            printf("  FAILED: %s\n", _th_tests[i].name);
        } else {
            _th_tests_passed++;
            printf("  PASSED: %s\n", _th_tests[i].name);
        }
    }

    printf("\n========================================\n");
    printf(" Results: %d/%d passed, %d failed (%d assertions)\n",
           _th_tests_passed, _th_tests_run, _th_tests_failed, _th_asserts);
    printf("========================================\n\n");

    return _th_tests_failed > 0 ? 1 : 0;
}
