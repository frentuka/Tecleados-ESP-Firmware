/**
 * @file main.c
 * @brief Test runner entry point for DF-ONE firmware host tests.
 *
 * Includes all test files and runs the full suite. Compiles and runs
 * on the host machine (no ESP32 required).
 *
 * Build:  gcc -std=c99 -Wall -Wextra -Iinclude -o test_runner main.c -lm
 * Run:    ./test_runner
 */

/* Include the test harness — provides main runner, assertions, and registration */
#include "test_harness.h"

/* Include all test files (they use self-registering TEST_CASE macros) */
#include "test_usb_crc.c"
#include "test_kb_bitmap.c"
#include "test_kb_state.c"
#include "test_kb_report.c"
#include "test_kb_layout.c"
#include "test_kb_macro.c"
#include "test_kb_system_action.c"
#include "test_kb_custom_key.c"
#include "test_cfg_layouts.c"
#include "test_cfgmod.c"
#include "test_usb_rx.c"
#include "test_usb_tx.c"
#include "test_status_module.c"
#include "test_event_bus.c"
#include "test_action_codes.c"
#include "test_macros_config.c"

int main(void) {
    return test_run_all();
}
