# Keyboard Module Documentation

Component path: `components/keyboard/`

---

## Overview

The keyboard module implements a full HID keyboard pipeline: physical matrix scanning → debouncing → layer-aware key mapping → action dispatch → virtual NKRO state → USB/BLE HID report delivery. It also drives a tap/hold engine for gesture-based actions and a macro execution engine for complex key sequences.

---

## File Structure

| File | Role |
|------|------|
| `kb_manager.c` | Main task: scan loop, debounce, edge detection, report scheduling |
| `kb_macro.c` | Virtual NKRO state, action dispatcher, macro + tap-worker tasks |
| `kb_system_action.c` | Generic tap/hold state machine |
| `kb_custom_key.c` | Custom key modes (PressRelease, MultiAction) |
| `kb_matrix.c` | GPIO matrix hardware driver + ISR |
| `kb_report.c` | USB/BLE HID report routing |
| `kb_layout.c` | Default keymap + thin wrapper over config module lookup |
| `kb_state.c` | Host LED status (Caps Lock, etc.) tracking |
| `kb_bitmap.h` | **Internal** — shared bit-manipulation helpers (not exported) |

Public headers live in `include/`.

---

## Entry Point

```c
// main.c — called once after USB/BLE init
kb_manager_start();
```

`kb_manager_start()` in order:
1. `kb_state_init()` — clears LED state
2. `kb_macro_init()` — allocates NKRO state, loads macros, spawns tasks
3. `kb_system_action_init()` — spawns tap/hold timing task
4. `kb_custom_key_init()` — loads custom keys, chains callback
5. `cfg_layout_load_all()` — loads keymap from NVS (falls back to `keymaps[]`)
6. Registers USB system callback for test key injection
7. `kb_matrix_gpio_init()` — configures row/column GPIOs
8. Spawns `kb_mgr` task

---

## Action Code Spaces

All key assignments are 16-bit **action codes**:

| Range | Type |
|-------|------|
| `0x0000` | None |
| `0x0001–0x00FF` | Standard HID keycodes |
| `0x0100–0x01FF` | Reserved (future individual consumer codes) |
| `0x2000–0x20FF` | System actions (layers, BLE, media, RGB) |
| `0x3000–0x3FFF` | Custom key IDs |
| `0x4000–0x4FFF` | Macro IDs |
| `0xFFFF` | `KB_KEY_TRANSPARENT` — falls through to base layer |

System action constants are defined in `kb_layout.h` (`SYS_ACTION_*`, `MEDIA_ACTION_*`).

---

## Signal Flow

```
Physical key press
  └─ kb_manager_task: kb_matrix_scan() → debounce → edge detection
       └─ kb_layout_get_action_code(row, col, active_layer)
            └─ kb_macro_process_action(action_code, is_pressed)
                 ├─ HID (0x01–0xFF):   kb_macro_virtual_press/release()
                 ├─ System (0x2000+):  process_system_action()
                 │    ├─ Layer keys:   s_active_layer update
                 │    ├─ Media/volume: kb_send_consumer_report()
                 │    └─ BLE:          kb_system_action_process() → tap/hold engine
                 ├─ Macro (0x4000+):   enqueue to macro_task
                 └─ CKey (0x3000+):    kb_custom_key_process_action()
  └─ kb_macro_send_report()
       └─ kb_send_report(v_nkro)
            ├─ BLE active + connected: ble_hid_send_keyboard_report()
            └─ USB mounted:            usb_send_keyboard_6kro() or _nkro()
```

---

## Scan Loop (`kb_manager_task`)

- Runs at priority 5, 6 KB stack, internal RAM.
- Target rate: **1200 Hz** (`MAX_POLLING_RATE_HZ`).
- **Idle sleep**: when no keys are pressed, enables GPIO interrupts on all rows (columns driven LOW) and sleeps via `ulTaskNotifyTake`. Woken by ISR on key press.
- **Debounce**: integrator with `KB_DEBOUNCE_SCANS = 5`. A key must be stable for 5 consecutive scans to change state.
- **Layer-sticky action codes**: on press, the action code for the current layer is stored per key; the matching release always fires the same code even if the layer changes while the key is held.
- **Forced reports**: a report is also sent every `MIN_REPORT_RATE_HZ` (1 Hz) to keep the host alive even with no matrix changes.
- **Boot protocol**: auto-detected via `usb_keyboard_use_boot_protocol()`; report format switches between 6KRO and NKRO transparently.

---

## Virtual NKRO State (`kb_macro.c`)

A 32-byte (256-bit) bitmap `s_v_nkro[]` holds every currently "pressed" HID keycode. Protected by `s_v_nkro_mutex`.

- `kb_macro_virtual_press(kc)` — sets bit `kc`
- `kb_macro_virtual_release(kc)` — clears bit `kc`
- `kb_macro_send_report()` — takes mutex, calls `kb_send_report(s_v_nkro)`, retries up to 100× on endpoint-busy

Layer switching is tracked via `s_is_fn1_held` / `s_is_fn2_held`:

| FN1 | FN2 | Active Layer |
|-----|-----|--------------|
| 0 | 0 | BASE |
| 1 | 0 | FN1 |
| 0 | 1 | FN2 |
| 1 | 1 | FN3 |

---

## Macro Engine

### Execution Modes

| Mode | Behaviour |
|------|-----------|
| `MACRO_EXEC_ONCE_STACK_ONCE` | Execute; allow 1 pending execution while running |
| `MACRO_EXEC_ONCE_NO_STACK` | Execute once; ignore additional presses while running |
| `MACRO_EXEC_ONCE_STACK_N` | Execute; allow up to `stack_max` pending |
| `MACRO_EXEC_HOLD_REPEAT` | Loop while key held |
| `MACRO_EXEC_HOLD_REPEAT_CANCEL` | Loop while held; key release cancels mid-execution |
| `MACRO_EXEC_TOGGLE_REPEAT` | Toggle looping on/off per press |
| `MACRO_EXEC_TOGGLE_REPEAT_CANCEL` | Toggle; second press also cancels mid-execution |
| `MACRO_EXEC_BURST_N` | Execute exactly `repeat_count` times |

### Event Types (within a macro)

| Type | Behaviour |
|------|-----------|
| `MACRO_EVT_KEY_PRESS` | Virtual key down (or recursive macro call) |
| `MACRO_EVT_KEY_RELEASE` | Virtual key up |
| `MACRO_EVT_KEY_TAP` | Press + hold `press_duration_ms` + release |
| `MACRO_EVT_DELAY_MS` | Sleep; split into 10 ms chunks in cancellable modes |

Macros may call other macros recursively (max depth 5).

### Tasks

- **`kb_macro`** (priority 4, 5 KB): consumes `s_macro_queue` (32-item), executes macros.
- **`kb_tap_0..3`** (priority 4, 3 KB each): consume `s_tap_queue` (32-item), fire single taps from custom keys / system actions.

---

## Tap/Hold Engine (`kb_system_action.c`)

A generic state machine that converts raw press/release into richer gesture events.

### States

```
IDLE → PRESSED_WAIT_HOLD → HELD
          └─ (release) → RELEASED_WAIT_DOUBLE → IDLE
                              └─ (re-press) → IDLE (DOUBLE_TAP fired)
```

### Events

| Event | When |
|-------|------|
| `KB_EV_PRESS` | Immediately on key down |
| `KB_EV_RELEASE` | Immediately on key up |
| `KB_EV_HOLD` | After `hold_threshold_ms` (default 500 ms) with no release |
| `KB_EV_SINGLE_TAP` | After `double_tap_threshold_ms` (default 300 ms) from **release** with no second press |
| `KB_EV_DOUBLE_TAP` | Second press within `double_tap_threshold_ms` of **release** |

> **Important**: the double-tap window is measured from the **key release**, not the original press. This ensures a consistent gesture window regardless of how long the first tap was held.

Per-action timing can be overridden via `kb_system_action_process_ex()` with a `kb_sys_action_timing_t` struct (0 = use engine default).

Up to 10 concurrent action trackers. The background task `kb_sys_action` (priority 5, 4 KB) checks timeouts every 10 ms.

### Callback

A single callback slot (`s_action_cb`) is registered with `kb_system_action_register_cb()`. Custom keys chain-register by saving the previous callback and forwarding non-CKey events:

```
main → ble_controller_init() → kb_system_action_register_cb(on_kb_sys_action)
                   ↓
kb_custom_key_init() saves on_kb_sys_action as s_prev_action_cb,
registers ckey_action_event_cb() — CKey events handled here,
everything else forwarded to s_prev_action_cb.
```

---

## Custom Keys (`kb_custom_key.c`)

Two modes, selected per-key in `cfg_custom_keys`:

### PressRelease mode
- **On press**: fires `press_action` as a tap with `press_tap_release_delay_ms` hold time.
- **On release**: waits until the press tap finishes (if `wait_for_finish`), then fires `release_action` as a tap.

### MultiAction mode
Routes the outcome of the tap/hold engine to one of three inner actions:

| Gesture | Action |
|---------|--------|
| `KB_EV_SINGLE_TAP` | `tap_action` |
| `KB_EV_DOUBLE_TAP` | `double_tap_action` |
| `KB_EV_HOLD` | `hold_action` |

Timing thresholds (`double_tap_threshold_ms`, `hold_threshold_ms`) can be set per custom key.

All inner actions are fired as taps via `kb_macro_fire_tap()` (non-blocking, queued to a tap worker).

---

## Report Routing (`kb_report.c`)

```c
bool kb_hid_ready(void);           // Is any HID interface ready?
esp_err_t kb_send_report(v_nkro);  // Keyboard report
esp_err_t kb_send_consumer_report(media_keycode); // Consumer/media report
```

Priority:
1. **BLE** — if `ble_hid_is_routing_active()` and connected: send via `ble_hid_send_keyboard_report()`.
2. **USB** — if `tud_mounted()` and endpoint ready: send 6KRO (boot protocol) or NKRO depending on `usb_keyboard_use_boot_protocol()`.

NKRO→6KRO conversion extracts modifier bits (keycodes `0xE0–0xE7`) into the modifier byte and packs up to 6 remaining keycodes.

---

## Hardware Matrix (`kb_matrix.c`)

- **6 rows** (GPIO 1–6): inputs with pull-up.
- **18 columns** (GPIO 7–18, 21, 38–42): push-pull outputs.
- Scanning: drive each column LOW, read all rows (LOW = pressed via diode).
- Bit encoding: `bit_index = row * KB_MATRIX_COL_COUNT + col`.
- ISR: falling-edge on all rows (with all columns LOW) wakes `kb_mgr` during idle.

---

## Layout / Keymap (`kb_layout.c`)

- The factory-default keymap `keymaps[4][6][18]` is defined **once** in `kb_layout.c` and exported via `extern const` in `kb_layout.h`.
- `kb_layout_get_action_code()` delegates to `cfg_layout_get_action_code()` which serves from a PSRAM cache (layer 0 also mirrored in DRAM for hot-path speed).
- On first boot, NVS is empty; `cfg_layout_load_all()` copies from `keymaps[]` to the cache.

---

## LED State (`kb_state.c`)

`kb_state_update_leds(led_status)` is called by the USB module on HID output reports. Currently, Caps Lock lights the RGB LED red.

---

## Test Key Injection

`kb_manager_test_nkro_keypress(row, col)` — sends a single NKRO report directly over USB (bypasses the matrix scan path). Used for factory/CI testing.

Over USB: the `MODULE_SYSTEM` HID channel accepts:
- `SYS_CMD_INJECT_KEY (0x01)` + `row, col, state` — injects a virtual key into the scan bitmap.
- `SYS_CMD_CLEAR_INJECTED (0x02)` — clears all injected keys.

---

## Concurrency Summary

| Task | Priority | Stack | Responsibility |
|------|----------|-------|----------------|
| `kb_mgr` | 5 | 6 KB | Scan, debounce, action dispatch, report send |
| `kb_sys_action` | 5 | 4 KB | Tap/hold timeout polling (10 ms interval) |
| `kb_macro` | 4 | 5 KB | Macro execution |
| `kb_tap_0..3` | 4 | 3 KB | Fire-tap workers (4 parallel slots) |

**Synchronisation**:
- `s_v_nkro_mutex` (mutex): guards the virtual NKRO bitmap across all tasks.
- `s_injected_matrix_lock` (portMUX spinlock): guards the test injection bitmap between ISR/task context.
- `s_paused` (volatile bool): set by `kb_manager_set_paused()` to suppress report sends.

---

## BLE Integration (`main/ble_controller.c`)

Registers `on_kb_sys_action` as the system action callback **before** `kb_manager_start()`. The callback handles:

| Action | Gesture | Behaviour |
|--------|---------|-----------|
| `SYS_ACTION_BLE_TOGGLE` | Single tap | Toggle BLE routing active |
| `SYS_ACTION_BLE_1..9` | Single tap | Connect profile N |
| `SYS_ACTION_BLE_1..9` | Hold | Pair profile N |
| `SYS_ACTION_BLE_1..9` | Double tap | Toggle connection for profile N |
