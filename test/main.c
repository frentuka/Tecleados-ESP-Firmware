/**
 * @file main.c
 * @brief Test runner entry point for TEF firmware host tests.
 *
 * Includes production .c files (linked against mocks via shim headers),
 * then all test files. Compiles and runs on the host machine.
 *
 * Build (MSVC):
 *   cl /nologo /std:c17 /W3 /Iinclude
 *      /I../../components/keyboard/include
 *      /I../../components/event_bus/include
 *      /I../../components/config_module/include
 *      /I../../components/ble_module/include
 *      /I../../components/usb_module/include
 *      /I../../components/status_module/include
 *      /Fe:test_runner.exe main.c
 *
 * Run:   test_runner.exe [-k filter] [--junit]
 */

/* Include the test harness — provides assertions, registration, and runner.
 * This also pulls in test_constants.h which includes production headers
 * (event_bus.h, kb_layout.h, kb_system_action.h, kb_matrix.h) via shims. */
#include "test_harness.h"

/* ======================================================================
 * Production code — linked via single-TU #include.
 *
 * Each production .c defines `static const char *TAG = "..."`.
 * In single-TU mode these collide. Since ESP_LOG* are no-ops in tests,
 * TAG is never read — we redefine it as a macro that generates unique
 * identifiers to avoid redefinition errors.
 * ====================================================================== */

#define _TAG_JOIN2(a, b) a##b
#define _TAG_JOIN(a, b) _TAG_JOIN2(a, b)
#ifdef TAG
  #undef TAG
#endif
#define TAG _TAG_JOIN(_tag_, __COUNTER__)

/* Production sources — ESP-IDF includes resolve to mocks via shim headers */
#include "../../components/event_bus/event_bus.c"
#include "../../components/keyboard/kb_state.c"
#include "../../components/usb_module/usb_crc.c"
#include "../../components/keyboard/kb_report.c"
#include "../../components/keyboard/kb_system_action.c"
#include "../../components/keyboard/kb_layout.c"
#include "../../components/config_module/cfg_layouts.c"
#include "../../components/usb_module/usb_callbacks_rx.c"
#include "../../components/status_module/statusmod.c"

#undef TAG

/* ======================================================================
 * Test files (self-registering TEST_CASE macros)
 * ====================================================================== */

/* Keyboard tests */
#include "keyboard/test_kb_state.c"
#include "keyboard/test_kb_report.c"
#include "keyboard/test_kb_layout.c"
#include "keyboard/test_kb_system_action.c"

/* USB tests */
#include "usb/test_usb_crc.c"
#include "usb/test_usb_rx.c"

/* System tests */
#include "system/test_event_bus.c"
#include "system/test_status_module.c"

int main(int argc, char *argv[]) {
    return test_run_all(argc, argv);
}
