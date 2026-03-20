# Config Module

## Overview

The Config module (`components/config_module/`) is the single source of truth for all persistent keyboard configuration. It provides:

- **NVS-backed storage** for all config kinds (keyboard layers, macros, custom keys, system settings, BLE state, physical layout).
- **PSRAM-aware allocation** — cJSON ASTs and large arrays are routed to PSRAM to preserve internal DRAM.
- **Binary-first, JSON-fallback storage** — new data is written as raw binary blobs; the module can still read legacy JSON blobs written by older firmware.
- **USB wire protocol handler** — receives GET/SET commands from the web configurator and dispatches them to the appropriate kind.

Entry point: `cfgmod.h`. Public consumers only need to call `cfg_init()` once and then use the kind-specific APIs (`cfg_ble_get_state()`, `cfg_layout_get_action_code()`, etc.).

---

## Architecture

### Registration Model

Every config kind is registered with `cfgmod_register_kind()`, which stores four callbacks and a struct size:

```c
esp_err_t cfgmod_register_kind(
    cfgmod_kind_t          kind,
    cfgmod_default_fn      def_fn,      // fill struct with defaults
    cfgmod_deserialize_fn  des_fn,      // cJSON → struct
    cfgmod_serialize_fn    ser_fn,      // struct → cJSON
    cfgmod_on_update_fn    update_fn,   // called after a USB SET succeeds (may be NULL)
    size_t                 struct_size  // sizeof the config struct
);
```

After registration, `cfgmod_get_config()` and `cfgmod_set_config()` use these callbacks to load/save that kind generically.

**Who owns the registration?**

Most kinds are registered by the config module itself in `cfg_init()`. However, for kinds whose runtime reload callback lives in the keyboard module, the keyboard module re-registers the kind during its own init — overriding the NULL callback set at `cfg_init()` time. This keeps the config module free of keyboard module dependencies.

| Kind              | Registered by       | Re-registered by         |
|-------------------|---------------------|--------------------------|
| LAYOUT            | `cfg_init`          | —                        |
| MACRO             | `cfg_init` (no-op)  | `kb_macro_init`          |
| CONNECTION (BLE)  | `cfg_ble_init`      | —                        |
| SYSTEM            | `cfg_init`          | —                        |
| PHYSICAL          | `cfg_init`          | —                        |
| CKEY              | `cfg_init` (NULL cb)| `kb_custom_key_init`     |

### NVS Storage Layout

Each kind writes to its own NVS namespace for isolation and to stay within NVS's 15-character key limit:

| Kind              | NVS Namespace | Key pattern                   | Notes                           |
|-------------------|---------------|-------------------------------|---------------------------------|
| LAYOUT            | `cfg_lay`     | `ly0`, `ly1`, `ly2`, `ly3`    | One blob per layer              |
| MACRO             | `cfg_mac`     | `mac_0`…`mac_63`, `mac_idx`   | Individual blobs + bitmap index |
| CONNECTION (BLE)  | `cfg`         | `k2_ble_cfg` (prefixed)       | Uses fallback prefix scheme     |
| SYSTEM            | `cfg`         | `k3_sys` (prefixed)           | Uses fallback prefix scheme     |
| PHYSICAL          | `cfg`         | `k4_physical` (prefixed)      | Uses fallback prefix scheme     |
| CKEY              | `cfg_ck`      | `ck_0`…`ck_119`, `ck_idx`     | Individual blobs + bitmap index |

Kinds with a dedicated namespace use the raw key name directly. Kinds using the fallback `cfg` namespace have their key prefixed with `k<kind_int>_` to avoid collisions (enforced by the internal `cfgmod_build_key()` + `resolve_ns_and_key()` helpers in `cfgmod.c`).

### Binary vs JSON Dual-Path Storage

`cfgmod_get_config()` uses a two-pass strategy to load a config:

1. **Apply defaults** via `def_fn` (always, as a safe baseline).
2. **Open NVS** and probe the stored blob size.
3. **Binary load** — if the stored size exactly matches `struct_size`, read directly into the struct. This is the fast path for all data written by current firmware.
4. **JSON fallback** — if sizes differ (data written by older firmware as JSON), read the blob as a string, parse with cJSON, and call `des_fn`. This handles migration transparently with no manual version checks.

New data is always written as raw binary blobs via `cfgmod_set_config()` → `cfgmod_write_storage()`.

### PSRAM Strategy

cJSON is redirected to PSRAM via custom malloc/free hooks installed in `cfg_init()`:

```c
cJSON_Hooks hooks = { .malloc_fn = spiram_cjson_malloc, .free_fn = spiram_cjson_free };
cJSON_InitHooks(&hooks);
```

`spiram_cjson_malloc` tries `heap_caps_malloc(MALLOC_CAP_SPIRAM)` first and silently falls back to `malloc()`. This prevents large config ASTs (e.g. full macro serialization, physical layout JSON) from exhausting the ~320 KB internal DRAM.

---

## USB Wire Protocol

The config module registers with the USB module as `MODULE_CONFIG` via `usbmod_register_callback()`.

### Request Frame

```
[1 byte: module_id] [1 byte: cmd] [1 byte: key_id] [N bytes: JSON payload]
```

- `module_id` — `MODULE_CONFIG` (stripped by `cfgmod_handle_usb_comm`, added back by `cfg_usb_callback`)
- `cmd` — `CFG_CMD_GET (0)` or `CFG_CMD_SET (1)`
- `key_id` — `cfgmod_key_id_t` value (see table below)
- `payload` — JSON string for SET commands or `{"id": N}` for single-item GETs; empty for full GETs

### Response Frame

```
[1 byte: module_id] [1 byte: cmd] [1 byte: key_id] [4 bytes: esp_err_t] [N bytes: JSON payload]
```

`esp_err_t` is `ESP_OK (0)` on success, or an error code. The JSON payload is only present on success for GET commands.

### Key ID Map

| `cfgmod_key_id_t`         | cmd     | Payload (request)             | Payload (response)                   |
|---------------------------|---------|-------------------------------|--------------------------------------|
| `CFG_KEY_TEST`            | GET     | —                             | Raw NVS blob                         |
| `CFG_KEY_HELLO`           | GET     | —                             | Raw NVS blob                         |
| `CFG_KEY_PHYSICAL_LAYOUT` | GET/SET | JSON or —                     | Physical layout JSON                 |
| `CFG_KEY_LAYER_0..3`      | GET/SET | JSON or —                     | `{"keys": [[...], ...]}`             |
| `CFG_KEY_MACROS`          | GET     | —                             | `{"macros": [{id, name, execMode}…]}`|
| `CFG_KEY_MACRO_LIMITS`    | GET     | —                             | `{"maxEvents": 256, "maxMacros": 64}`|
| `CFG_KEY_MACRO_SINGLE`    | GET     | `{"id": N}`                   | Full macro JSON with `elements`      |
| `CFG_KEY_MACRO_SINGLE`    | SET     | Macro JSON or `{"delete": N}` | — (status only)                      |
| `CFG_KEY_CKEYS`           | GET     | —                             | `{"customKeys": [{id, name, mode}…]}`|
| `CFG_KEY_CKEY_SINGLE`     | GET     | `{"id": N}`                   | Full custom key JSON                 |
| `CFG_KEY_CKEY_SINGLE`     | SET     | CKey JSON or `{"delete": N}`  | — (status only)                      |

`CFG_KEY_TEST` and `CFG_KEY_HELLO` are protocol slots for raw NVS read/write, useful for diagnostics. They return `ESP_ERR_NVS_NOT_FOUND` until written via SET.

---

## Subsystems

### cfg_layouts — Keyboard Layer Maps

**Files:** [cfg_layouts.c](cfg_layouts.c), [include/cfg_layouts.h](include/cfg_layouts.h)

Stores four keyboard layers (0–3). Each layer is a `cfg_layer_t` — a 2D array of `uint16_t` action codes with shape `[KB_MATRIX_ROW_COUNT][KB_MATRIX_COL_COUNT]` (5×15 = 75 keys).

**Caching strategy** (three-tier, minimizes PSRAM reads on hot path):
- `s_psram_cache` — all four layers in PSRAM (allocated on first `cfg_layout_load_all()`)
- `s_dram_base` — layer 0 always mirrored in DRAM; zero latency for base-layer lookups
- `s_dram_swap` — one additional layer cached in DRAM at a time; swapped on access from PSRAM

`cfg_layout_get_action_code()` is the hot-path function called from the keyboard scan loop. It returns `ACTION_CODE_NONE` for out-of-bounds access and falls through transparent keys (`KB_KEY_TRANSPARENT`) to layer 0.

**First-boot / empty NVS:** `cfg_layout_load_all()` calls `cfgmod_read_storage()` directly instead of `cfgmod_get_config()`. This is intentional — `cfgmod_get_config()` always returns `ESP_OK` (zeroing the struct via `layout_default` when NVS is empty), making it impossible to distinguish "loaded from NVS" from "NVS was empty". Using `cfgmod_read_storage()` lets the function detect a true NVS miss and fall back to the compile-time `keymaps[]` defaults.

**On USB SET**: `layout_update_cb()` reloads the affected layer into the PSRAM cache and updates the DRAM mirrors if applicable.

**NVS keys:** `ly0`, `ly1`, `ly2`, `ly3` in namespace `cfg_lay`.

---

### cfg_macros — Macro Definitions

**Files:** [cfg_macros.c](cfg_macros.c), [include/cfg_macros.h](include/cfg_macros.h)

Stores up to **64 macros** (IDs 0–63), each with up to **256 events**.

Each macro (`cfg_macro_t`) contains:
- `id`, `name[32]`
- `events[256]` — array of `cfg_macro_event_t` (type, keycode/value, delay_ms, press_duration_ms)
- `exec_mode` (`cfg_macro_exec_mode_t`) — controls repeat/stack behavior on re-trigger
- `stack_max` / `repeat_count` — mode-specific parameters

**Execution modes** (`cfg_macro_exec_mode_t`):

| Value | Name                   | Behavior                                               |
|-------|------------------------|--------------------------------------------------------|
| 0     | `ONCE_STACK_ONCE`      | Queue at most 1 extra execution while running          |
| 1     | `ONCE_NO_STACK`        | Ignore re-presses while running                        |
| 2     | `ONCE_STACK_N`         | Queue up to `stack_max` extra executions               |
| 3     | `HOLD_REPEAT`          | Repeat while held; finish current iteration on release |
| 4     | `HOLD_REPEAT_CANCEL`   | Repeat while held; abort on release                    |
| 5     | `TOGGLE_REPEAT`        | Toggle on/off; finish current on stop                  |
| 6     | `TOGGLE_REPEAT_CANCEL` | Toggle on/off; abort on stop                           |
| 7     | `BURST_N`              | Fire `repeat_count` times on a single press            |

**Event types** (`cfg_macro_event_type_t`):
- `KEY_PRESS` / `KEY_RELEASE` — explicit down/up
- `KEY_TAP` — press+release with configurable `press_duration_ms`
- `DELAY_MS` — sleep for `value` milliseconds

**Storage:** Each macro is stored as an individual NVS blob under key `mac_<id>` in namespace `cfg_mac`. A `cfg_macro_index_t` struct (64-bit bitmask) under key `mac_idx` tracks which IDs exist — this avoids scanning all 64 slots to build the outline.

**Registration note:** `cfg_macros_register()` in `cfg_macros.c` is intentionally empty. The actual `cfgmod_register_kind()` call for `CFGMOD_KIND_MACRO` is made by `kb_macro_init()` in `kb_macro.c`, which owns the `on_macros_updated` reload callback. `macros_serialize()` always returns NULL and exists solely to satisfy the `cfgmod_serialize_fn` function pointer requirement — individual macro serialization is done by `macros_serialize_single()` via the custom USB handler, not the generic path.

---

### cfg_custom_keys — Custom Key Definitions

**Files:** [cfg_custom_keys.c](cfg_custom_keys.c), [include/cfg_custom_keys.h](include/cfg_custom_keys.h)

Stores up to **120 custom keys** (IDs 0–119).

Each custom key (`cfg_custom_key_t`) operates in one of two modes:

**CKEY_MODE_PRESS_RELEASE** — fires `press_action` on key-down, `release_action` on key-up. The `*_tap_release_delay_ms` fields control how long the synthesized virtual key is held before being released (the "tap width"). Set to 0 to skip the delay. Optionally waits for the press action to fully finish before firing release (`wait_for_finish`).

**CKEY_MODE_MULTI_ACTION** — distinguishes tap, double-tap, and hold via timing thresholds:
- `double_tap_threshold_ms` — max inter-tap gap to count as a double-tap (default 300 ms)
- `hold_threshold_ms` — hold duration before triggering hold action (default 500 ms)
- `*_release_delay_ms` — tap width for each resolved action (default 20 ms each)

All actions are action codes in the unified action code space (HID, System, Macro, CKey).

**Storage:** Like macros, each custom key is stored as an individual NVS blob under `ck_<id>` in namespace `cfg_ck`. A `cfg_ckey_index_t` (15-byte bitmask, 120 bits) under `ck_idx` tracks which IDs exist.

**Registration note:** `cfg_init()` registers `CFGMOD_KIND_CKEY` with a NULL update callback. `kb_custom_key_init()` in `kb_custom_key.c` re-registers with `kb_custom_key_reload` as the callback, matching the macro pattern. This ensures the config module has no dependency on the keyboard module.

---

### cfg_system — Device System Settings

**Files:** [cfg_system.c](cfg_system.c), [include/cfg_system.h](include/cfg_system.h)

Single struct (`cfg_system_t`) with:

| Field               | Type       | Default            | Description                 |
|---------------------|------------|--------------------|-----------------------------|
| `device_name`       | `char[32]` | `"Antigravity KB"` | BLE advertised device name  |
| `sleep_timeout_ms`  | `uint32_t` | `300000` (5 min)   | Idle time before deep sleep |
| `rgb_brightness`    | `uint8_t`  | `255`              | RGB LED global brightness   |
| `bluetooth_enabled` | `bool`     | `true`             | Master BLE on/off switch    |

**Caching:** A module-local static `s_sys` caches the loaded value. The cache is invalidated (`s_sys_loaded = false`) by `sys_update_cb` whenever a USB SET updates the value. The next `cfg_system_get()` call reloads from NVS.

**NVS key:** `k3_sys` (prefixed) in namespace `cfg`.

---

### cfg_physical — Physical Keyboard Geometry

**Files:** [cfg_physical.c](cfg_physical.c), [include/cfg_physical.h](include/cfg_physical.h)

Stores the physical key positions and sizes as a raw JSON blob (up to `CFG_PHYSICAL_JSON_BUFSIZE` = 4096 bytes). The format is used by the web configurator to render the keyboard layout correctly.

Format: `{"rows": N, "cols": M, "layout": [[row, col, w, h, x, y, ...], ...]}`

The default is a 65% keyboard layout hardcoded in `DEFAULT_PHYS_JSON`. Since the struct is really a char buffer, this kind is somewhat special — its `struct_size` is 4096 bytes rather than a true C struct size. The serialize/deserialize callbacks operate on the JSON string directly (parse ↔ print round-trip).

**NVS key:** `k4_physical` (prefixed) in namespace `cfg`.

---

### cfg_ble — BLE Connection State

**Files:** [cfg_ble.c](cfg_ble.c), [include/cfg_ble.h](include/cfg_ble.h)

Stores up to **9 BLE pairing profiles** plus global BLE routing state.

`cfg_ble_state_t`:
- `profiles[9]` — array of `cfg_ble_profile_t`, each with:
  - `addr_type` — `BLE_ADDR_PUBLIC` or `BLE_ADDR_RANDOM`
  - `val[6]` — 6-byte MAC address
  - `is_valid` — whether this slot has a paired device
  - `addr_nonce` — incremented on each re-pair to rotate the random MAC
- `selected_profile` — active profile index (0–8)
- `ble_routing_enabled` — whether BLE HID reports are sent (default `true`)

**Access pattern:** Module-local `g_cfg_ble_state` (static — not exported) is kept in sync with NVS. Callers use the getter/setter API exclusively:
- `cfg_ble_get_state()` — returns `const` pointer to the in-memory state (no NVS read)
- `cfg_ble_save_state(state)` — copies `state` into `g_cfg_ble_state` and persists to NVS

`on_ble_updated()` (the `cfgmod_on_update_fn` callback) reloads `g_cfg_ble_state` from NVS so that any future USB SET path targeting `CFGMOD_KIND_CONNECTION` keeps the in-memory state consistent.

**NVS key:** `k2_ble_cfg` (prefixed) in namespace `cfg`.

---

## Public API Summary

All functions are declared in [include/cfgmod.h](include/cfgmod.h).

| Function                                                   | Description                                                                 |
|------------------------------------------------------------|-----------------------------------------------------------------------------|
| `cfg_init()`                                               | Initialize NVS, cJSON PSRAM hooks, register all kinds, install USB callback |
| `cfg_deinit()`                                             | Placeholder for future cleanup (currently no-op)                            |
| `cfg_is_init()`                                            | Returns `true` after `cfg_init()` succeeds                                  |
| `cfgmod_register_kind(...)`                                | Register a config kind with its callbacks and struct size                   |
| `cfgmod_get_config(kind, key, out)`                        | Load a config struct: defaults → NVS binary → NVS JSON fallback             |
| `cfgmod_set_config(kind, key, in)`                         | Save a config struct as binary to NVS; calls `update_fn` on success         |
| `cfgmod_read_storage(kind, key, buf, len)`                 | Low-level NVS blob read (used by macro/ckey per-item storage)               |
| `cfgmod_write_storage(kind, key, data, len)`               | Low-level NVS blob write with commit                                        |
| `cfgmod_handle_usb_comm(data, len, out, out_len, out_max)` | Dispatch one USB request frame and build the response                       |

Kind-specific APIs in their respective headers:
- `cfg_ble_get_state()` / `cfg_ble_save_state()` — BLE state
- `cfg_layout_load_all()` / `cfg_layout_get_action_code()` / `cfg_layout_get_layer()` / `cfg_layout_set_layer()` — keyboard layers
- `cfg_system_get()` / `cfg_system_set()` — system config
- `macros_load_all()` / `macros_upsert_single()` / `macros_delete_single()` / `macros_serialize_*()` — macros
- `ckeys_load_all()` / `ckeys_upsert_single()` / `ckeys_delete_single()` / `ckeys_serialize_*()` — custom keys

---

## Initialization Sequence

The module initializes in three phases spread across `cfg_init()` and the keyboard module inits:

1. **`cfg_init()`** — called from `main.c`:
   - Installs cJSON PSRAM hooks
   - Calls `nvs_flash_init()` (auto-erases on version mismatch)
   - Registers: LAYOUT, SYSTEM, PHYSICAL, BLE (with final callbacks), CKEY (with NULL callback — to be overridden), MACRO (no-op)
   - Installs `cfg_usb_callback` as the USB handler for `MODULE_CONFIG`

2. **`kb_macro_init()`** — called from `kb_manager_start()`:
   - Re-registers `CFGMOD_KIND_MACRO` with `on_macros_updated` callback
   - Calls `macros_load_all()` to populate the in-memory macro list

3. **`kb_custom_key_init()`** — called from `kb_manager_start()` after `kb_macro_init()`:
   - Allocates PSRAM arrays for runtime custom key state
   - Re-registers `CFGMOD_KIND_CKEY` with `kb_custom_key_reload` callback
   - Calls `kb_custom_key_reload("init")` to populate the in-memory custom key array

After step 3, the USB module accepts connections and all config kinds are fully functional with their reload callbacks wired up.
