# TEF Firmware Test Suite

Host-based unit tests for the TEF ESP32-S3 keyboard firmware. Compiles and runs entirely on the development machine — no ESP32 hardware or ESP-IDF toolchain required.

## Architecture

**Single-translation-unit build.** `main.c` `#include`s production `.c` files and all test `.c` files into one compilation unit. This avoids linker complexity but requires care with duplicate definitions.

**Production code linked directly.** Nine production modules are compiled as real code (not re-implementations):
- `event_bus.c` — event base definitions, bus init
- `kb_state.c` — LED state tracking
- `usb_crc.c` — CRC-8 computation
- `kb_report.c` — HID report routing, NKRO-to-6KRO conversion
- `kb_system_action.c` — tap/hold gesture engine
- `kb_layout.c` — factory keymap tables
- `cfg_layouts.c` — layout cache (3-tier DRAM/PSRAM/NVS), layer lookup with transparent fallthrough
- `usb_callbacks_rx.c` — USB Blast-mode receive, bitmap tracking, packet commit
- `statusmod.c` — BLE event-driven status cache and JSON push

These are included in `main.c` via `#include "../../components/.../file.c"`. A `TAG` macro trick (`#define TAG _TAG_JOIN(_tag_, __COUNTER__)`) prevents collisions from each file's `static const char *TAG`.

**Shim header interception.** `test/include/` contains shim headers that shadow ESP-IDF headers (e.g., `esp_log.h`, `freertos/FreeRTOS.h`, `tusb.h`). The test include path (`/Iinclude`) is listed first, so when production code does `#include "esp_log.h"`, the shim is found instead — redirecting to lightweight mocks. Production headers that don't need shimming (e.g., `event_bus.h`, `kb_layout.h`) resolve from component include paths listed after.

**Self-registering tests.** Each `TEST_CASE(suite, name)` macro auto-registers the test function before `main()` runs — via `__attribute__((constructor))` on GCC/Clang, or `.CRT$XCU` section on MSVC. No manual test list to maintain.

**Per-suite setup/teardown.** `TEST_SETUP(suite)` and `TEST_TEARDOWN(suite)` define functions that run before/after each test in that suite. Used to reset mocks and re-init production state.

**Mock layer.** ESP-IDF APIs are replaced by lightweight mocks in `include/mocks/`:
- `mock_esp.h` — esp_err_t, ESP_LOG* (no-op), esp_timer (controllable via `mock_timer_set`/`mock_timer_advance`), heap_caps
- `mock_esp_event.h` — event posting with recording, handler registration, dispatch
- `mock_freertos.h` — semaphore tracking (force-fail, taken state), queue simulation (force-full/empty), tick simulation, task stubs
- `mock_nvs.h` — NVS key-value store with injection
- `mock_tinyusb.h` — USB HID report capture, BLE routing state

## Directory Layout

```
test/
  main.c                  Entry point — includes production .c + all test files
  _build.bat              MSVC build script (Windows)
  TEST.md                 This file
  .gitignore              Ignores binaries and build artifacts
  include/
    test_harness.h        Test framework: assertions, registration, setup/teardown,
                          filtering (-k), JUnit XML (--junit), runner
    test_constants.h      Imports production headers + defines test-only extras
    esp_log.h             Shim → mock_esp.h
    esp_err.h             Shim → mock_esp.h
    esp_timer.h           Shim → mock_esp.h
    esp_heap_caps.h       Shim → mock_esp.h
    esp_event.h           Shim → mock_esp_event.h
    nvs.h / nvs_flash.h   Shim → mock_nvs.h
    tusb.h / tinyusb.h    Shim → mock_tinyusb.h
    usb_descriptors.h     Constants-only version for tests
    usb_crc.h             Mirrors production declarations
    usb_defs.h            C17/MSVC-compatible version (no C23 syntax)
    usb_send.h            Shim → mock_tinyusb.h (send_payload)
    usb_callbacks.h       Stubs for usb_callbacks_rx.c dependencies
    usb_callbacks_rx.h    RX function declarations
    usb_callbacks_tx.h    TX constants (MAX_TX_BUF_SIZE, TX_TIMEOUT_MS)
    usbmod.h              Shim → mock_tinyusb.h
    statusmod.h           MSVC-compatible packed struct macros for status types
    cfg_ble.h             Controllable BLE state mock (mock_ble_state_set/reset)
    cfgmod.h              Mock NVS store for cfg_layouts.c (mock_nvs_inject/reset)
    cfg_storage_keys.h    Layer NVS key names (ly0–ly3)
    cJSON.h               Minimal stubs (cfg_layouts.c parses via cfgmod mock path)
    basic_utils.h         No-op stub for print_bytes_as_chars
    freertos/             FreeRTOS shims → mock_freertos.h
    driver/gpio.h         GPIO type stubs
    class/hid/hid.h       HID keycode constants
    mocks/
      mock_esp.h          ESP-IDF core stubs
      mock_esp_event.h    Event system mock (post, register, dispatch_all)
      mock_freertos.h     FreeRTOS mock (semaphores, queues, tasks)
      mock_nvs.h          NVS flash mock
      mock_tinyusb.h      TinyUSB HID mock + send_payload capture
  keyboard/               Keyboard subsystem tests (4 files)
    test_kb_state.c       LED state tracking (real kb_state.c)
    test_kb_report.c      HID routing + NKRO-to-6KRO (real kb_report.c)
    test_kb_layout.c      Factory defaults, transparent fallthrough, DRAM cache,
                          NVS-injected layers (real cfg_layouts.c + kb_layout.c)
    test_kb_system_action.c  Tap/hold gesture engine (real kb_system_action.c)
  usb/                    USB communication tests (2 files)
    test_usb_crc.c        CRC-8 prepare/verify (real usb_crc.c)
    test_usb_rx.c         Blast-mode RX, bitmap, packet ordering, commit
                          (real usb_callbacks_rx.c)
  system/                 System-level tests (2 files)
    test_event_bus.c      Event bases, posting, retrieval (real event_bus.c)
    test_status_module.c  BLE event-driven state cache, profile bitmap, pairing,
                          routing mode, USB callback (real statusmod.c)
```

## Building and Running

### MSVC (Windows)

Use the provided batch file:

```
cd test
_build.bat
test_runner.exe
```

Or manually (open a Developer Command Prompt or run `vcvarsall.bat x64` first):

```
cd test
cl.exe /nologo /std:c17 /W3 /Fe:test_runner.exe ^
  /Iinclude ^
  /I../../components/keyboard/include ^
  /I../../components/event_bus/include ^
  /I../../components/config_module/include ^
  /I../../components/ble_module/include ^
  main.c
test_runner.exe
```

**Include path order matters:** `test/include` must come first so shim headers intercept ESP-IDF includes. Component paths follow for clean production headers.

### Test Filtering

Run a subset of tests by name or suite:

```
test_runner.exe -k usb_crc        # All tests in usb_crc suite
test_runner.exe -k hold            # All tests with "hold" in the name
```

### JUnit XML Output

```
test_runner.exe --junit            # Writes test_results.xml
test_runner.exe -k kb_state --junit  # Combine filter + JUnit
```

### GCC / Clang (Linux, macOS, MinGW)

```
cd test
gcc -std=c99 -Wall -Wextra -Iinclude \
  -I../../components/keyboard/include \
  -I../../components/event_bus/include \
  -I../../components/config_module/include \
  -I../../components/ble_module/include \
  -o test_runner main.c -lm
./test_runner
```

## Writing New Tests

### Adding a test case

Use the `TEST_CASE(suite, name)` macro. The suite name groups tests in output; the name should be `snake_case` and descriptive:

```c
TEST_CASE(usb_crc, all_zeros_round_trip) {
    uint8_t buf[9] = {0};
    usb_crc_prepare(buf, 8);
    TEST_ASSERT_TRUE(usb_crc_verify(buf, 8));
}
```

No registration code needed — the macro handles it automatically.

### Adding per-suite setup/teardown

```c
TEST_SETUP(my_suite) {
    mock_events_reset();
    my_module_init();
}

TEST_TEARDOWN(my_suite) {
    my_module_cleanup();
}
```

Setup runs before each test in the suite; teardown runs after.

### Adding a new test file

1. Create the `.c` file in the appropriate subfolder (`keyboard/`, `config/`, `usb/`, or `system/`).
2. Start the file with `#include "test_harness.h"` (this also pulls in `test_constants.h`).
3. Add a `#include` line in `main.c` under the matching category comment.
4. If the file needs ESP-IDF mocks beyond what test_harness.h provides, include the relevant `mocks/mock_*.h`.

### Linking new production code

To test a new production `.c` file with real code instead of re-implementing:

1. Add `#include "../../components/.../file.c"` in `main.c` between the `#define TAG` block and `#undef TAG`.
2. Create any missing shim headers in `test/include/` for ESP-IDF headers the production file includes.
3. Add the component's include directory to the build command (`/I../../components/.../include`).
4. Rewrite the test file to call real production functions instead of inline copies.

### Shared constants

All firmware constants live in `include/test_constants.h`, which imports production headers (`event_bus.h`, `kb_system_action.h`, `kb_layout.h`) and adds test-only extras. Never duplicate `#define` values — import them from this header.

### Avoiding duplicate definitions

Because all test files compile as one translation unit, shared types must be guarded. For types already defined in production headers (included via test_constants.h), use the `_TH_*` guards:

```c
#ifndef _TH_MY_STRUCT_T
#define _TH_MY_STRUCT_T
typedef struct { ... } my_struct_t;
#endif
```

The following guards are pre-defined in `test_constants.h` (types come from `event_bus.h`):
- `_TH_KB_SYS_ACTION_EVENT_T`
- `_TH_BLE_PAIRING_RESULT_T`
- `_TH_CONFIG_UPDATE_EVENT_T`

## Available Assertions

| Macro | Description |
|---|---|
| `TEST_ASSERT(cond)` | Fails if `cond` is false |
| `TEST_ASSERT_MSG(cond, msg)` | Same, with custom message |
| `TEST_ASSERT_TRUE(cond)` | Alias for `TEST_ASSERT` |
| `TEST_ASSERT_FALSE(cond)` | Fails if `cond` is true |
| `TEST_ASSERT_EQUAL(expected, actual)` | Integer equality (prints values on failure) |
| `TEST_ASSERT_EQUAL_HEX(expected, actual)` | Integer equality (hex output) |
| `TEST_ASSERT_NULL(ptr)` | Fails if pointer is non-NULL |
| `TEST_ASSERT_NOT_NULL(ptr)` | Fails if pointer is NULL |
| `TEST_ASSERT_MEM_EQUAL(exp, act, len)` | `memcmp`-based byte comparison |
| `TEST_ASSERT_STR_EQUAL(exp, act)` | `strcmp`-based string comparison |

All assertions increment the global assertion counter. A failed assertion prints the file, line, and condition but does **not** abort the test — the remaining assertions in that test still run.

## Cross-Platform Notes

- **Packed structs:** Use `PACKED_STRUCT_BEGIN` / `PACKED_STRUCT_END` / `PACKED_ATTR` macros (defined in `test_harness.h`) instead of `__attribute__((packed))`.
- **Constructor registration:** Handled automatically by `TEST_CASE`. No platform-specific code needed in test files.
- **MSVC warnings:** `_CRT_SECURE_NO_WARNINGS` and warnings 4996/4100/4189 are suppressed in `test_harness.h`.
- **C23 syntax:** `usb_defs.h` in production uses C23 typed enums — the test shim (`test/include/usb_defs.h`) provides a C17-compatible version.
- **TAG collisions:** Production `.c` files each define `static const char *TAG`. The `TAG` macro in `main.c` generates unique identifiers via `__COUNTER__` to avoid redefinition errors.
