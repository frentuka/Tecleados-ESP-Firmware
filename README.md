# DF-ONE Keyboard Firmware

> Custom programmable keyboard firmware for the ESP32-S3, built on top of ESP-IDF.
> Supports USB HID, Bluetooth LE, N-Key Rollover, 64 macros, 120 custom keys, 4 layers, and a React-based web configurator.

---

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Hardware](#hardware)
- [Architecture](#architecture)
- [Getting Started](#getting-started)
- [Component Reference](#component-reference)
  - [Event Bus](#event-bus)
  - [Keyboard](#keyboard-component)
  - [Config Module](#config-module)
  - [USB Module](#usb-module)
  - [BLE Module](#ble-module)
  - [RGB Module](#rgb-module)
  - [Button Module](#button-module)
  - [Status Module](#status-module)
- [Key Action System](#key-action-system)
- [Layer System](#layer-system)
- [Macro Engine](#macro-engine)
- [Custom Keys](#custom-keys)
- [BLE Multi-Profile](#ble-multi-profile)
- [Web Configurator](#web-configurator)
- [Communication Protocol](#communication-protocol)
- [Roadmap](#roadmap)

---

## Overview

DF-ONE is a fully programmable USB/Bluetooth keyboard firmware targeting the ESP32-S3. It handles the full pipeline from matrix scanning to HID report delivery, with a rich action system and a browser-based configurator for live configuration without reflashing.

The firmware is designed around a modular component architecture using ESP-IDF. Each subsystem (keyboard matrix, USB, BLE, config storage, RGB, etc.) is a self-contained component with a clean public API. Cross-module communication is handled through a system-wide event bus built on the ESP-IDF `esp_event` loop, so modules react to domain events without direct dependencies on each other.

---

## Features

- **USB HID** — Boot-compatible 6KRO + full N-Key Rollover (NKRO) via TinyUSB
- **Bluetooth LE** — HOGP-compliant HID peripheral; up to **9 independent pairing profiles**
- **Simultaneous USB + BLE** — Both transports active at once, with routing priority control
- **N-Key Rollover** — 32-byte virtual bitmap tracking all 256 HID keycodes simultaneously
- **4 Layers** — Base, FN1, FN2, FN3 (FN1+FN2 simultaneously), with transparent key fall-through
- **64 Macros** — Up to 256 events per macro, 8 execution modes (once, repeat, burst, toggle, etc.)
- **120 Custom Keys** — PressRelease mode (press ≠ release) and MultiAction mode (tap / double-tap / hold)
- **Tap/Hold Engine** — Generic gesture recognizer for 5 event types with configurable timeouts
- **RGB Feedback** — WS2812 LED on GPIO 48; reflects Caps Lock and can be extended
- **Persistent Storage** — All config (keymaps, macros, custom keys, BLE profiles) stored in NVS flash
- **Binary/JSON Dual-Path** — Fast binary read path with automatic JSON migration fallback for older firmware formats
- **PSRAM-Aware** — Large cJSON ASTs allocated in PSRAM to preserve internal DRAM
- **Web Configurator** — React 19 + TypeScript UI over WebHID (no drivers, no native app)
- **Multi-Packet Protocol** — Custom Blast+Reconcile protocol with CRC-8 for large config payloads over USB

---

## Hardware

**Target MCU:** ESP32-S3

**Matrix:** 6 rows × 18 columns = **108 physical keys**

| Peripheral | GPIO Pins | Notes |
|------------|-----------|-------|
| Matrix rows (input) | 1 – 6 | Internal pull-up |
| Matrix columns (output) | 7–18, 21, 38–42 | Push-pull |
| RGB LED (WS2812) | 48 | Driven by RMT via `espressif__led_strip` |
| BOOT button | ESP32-S3 default | Single-press and double-press callbacks |

**Flash Partition Layout:**

| Partition | Type | Offset | Size | Purpose |
|-----------|------|--------|------|---------|
| `nvs` | data/nvs | `0x9000` | 512 KB | All persistent configuration |
| `phy_init` | data/phy | `0x89000` | 4 KB | RF calibration data |
| `factory` | app/factory | `0x90000` | 1 MB | Firmware image |

**USB Identity:**

| Field | Value |
|-------|-------|
| Vendor ID | `0x303A` (Espressif) |
| Product ID | `0x1324` |
| Manufacturer | `Tecleados` |
| Product | `DF-ONE` |
| Serial | `13548` |

---

## Architecture

The firmware is a collection of independent ESP-IDF components that are wired together in `main/main.c`.

```
main/
 └─ main.c
              event_bus_init()  →  event_bus ──── ESP default event loop ──────────────────────┐
              button_init()     →  button_module                                                │
              cfg_init()        →  config_module ── publishes CONFIG_EVENTS ───────────────────>│
              rgb_init()        →  rgb_module    ── subscribes to KB_EVENT_LED_STATE ──────────<│
              usb_init()        →  usb_module                                                   │
              ble_hid_init()    →  ble_module    ── publishes BLE_EVENTS ──────────────────────>│
              ble_controller_init()              ── subscribes to KB_EVENT_SYSTEM_ACTION ───────<│
              status_module_init()               ── subscribes to BLE_EVENTS, CONFIG_EVENTS ────<│
              kb_manager_start() →  keyboard     ── publishes KB_EVENTS ───────────────────────>│

  keyboard/ ──────────────────────────────────────────────────────────────────────────────────┐ │
  │  kb_manager       Matrix scan task (1200 Hz), debounce, dispatch                          │ │
  │  kb_macro         Virtual NKRO bitmap, action router, macro executor                      │ │
  │  kb_system_action Tap/Hold engine → publishes KB_EVENT_SYSTEM_ACTION                      │ │
  │  kb_custom_key    PressRelease + MultiAction → subscribes to KB_EVENT_SYSTEM_ACTION        │ │
  │  kb_layout        Keymap lookup (PSRAM + DRAM caches)                                     │ │
  │  kb_report        USB/BLE HID report routing (direct call, transport selection)            │ │
  │  kb_state         LED state tracker → publishes KB_EVENT_LED_STATE                         │ │
  │  kb_matrix        GPIO matrix driver + ISR                                                 │ │
  └────────────────────────────────────────────────────────────────────────────────────────────┘ │
                                                                                                  │
  event_bus/ ◄──────────────────────────────────────────────────────────────────────────────────┘
     Thin wrapper around esp_event_loop_create_default()
     Owns: KB_EVENTS, BLE_EVENTS, CONFIG_EVENTS definitions

  usb_module/ ─── Interface 0: Keyboard HID │ Interface 1: Comm channel (config protocol)
  ble_module/ ─── HOGP peripheral, 9 pairing profiles, NimBLE, bond keys in NVS
  config_module/ ─ NVS storage, cJSON serialization, USB wire protocol
```

**Event Domains:**

| Base | Published by | Events |
|------|-------------|--------|
| `KB_EVENTS` | `kb_system_action`, `kb_state` | `KB_EVENT_SYSTEM_ACTION`, `KB_EVENT_LED_STATE` |
| `BLE_EVENTS` | `blemod` | `ROUTING_CHANGED`, `PROFILE_CONNECTED`, `PROFILE_DISCONNECTED`, `PAIRING_STARTED`, `PAIRING_COMPLETE`, `PAIRING_FAILED`, `PAIRING_TIMEOUT` |
| `CONFIG_EVENTS` | `cfgmod` | `CONFIG_EVENT_KIND_UPDATED` |

**Initialization Order (`main.c`):**

1. `event_bus_init()` — Create the default ESP event loop (**must be first**)
2. `button_init()` — Register single/double-press callbacks
3. `cfg_init()` — NVS init, register config kinds, install PSRAM cJSON hooks, install USB config callback; `cfg_ble_init()` subscribes to `BLE_EVENT_PAIRING_COMPLETE`
4. `rgb_init(GPIO_NUM_48)` — WS2812 LED driver; subscribes to `KB_EVENT_LED_STATE`
5. `usb_init()` — TinyUSB stack (keyboard + comm interfaces)
6. `ble_hid_init()` — NimBLE stack, GATT services, security config, advertising
7. `ble_controller_init()` — Subscribes to `KB_EVENT_SYSTEM_ACTION` to route BLE profile operations
8. `status_module_init()` — Subscribes to `BLE_EVENTS` and `CONFIG_EVENTS`; seeds initial state from config
9. `kb_manager_start()` — Starts matrix scanning task; `kb_custom_key_init()` subscribes to `KB_EVENT_SYSTEM_ACTION` and `CONFIG_EVENT_KIND_UPDATED`

---

## Getting Started

### Prerequisites

- [ESP-IDF v5.5.3](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/)
- Target chip: ESP32-S3

### Build and Flash

```bash
# Set up ESP-IDF environment (run once per shell session)
. $IDF_PATH/export.sh

# Build
idf.py build

# Flash
idf.py -p PORT flash

# Monitor serial output
idf.py -p PORT monitor
```

### Web Configurator

```bash
cd configurator
npm install
npm run dev
```

Then open the app in **Chrome, Edge, or Opera** (WebHID is not supported in Firefox) and click **Connect** to pair with the keyboard.

---

## Component Reference

### Event Bus

**Location:** `components/event_bus/`

Provides the system-wide publish/subscribe mechanism used by all modules for cross-module communication. Built on the ESP-IDF `esp_event` default loop.

**Public header:** `event_bus.h` — the single place that defines all event bases, event IDs, and payload structs for the entire firmware.

**API:**

```c
esp_err_t event_bus_init(void);  // creates the default event loop; call first
```

Modules publish and subscribe using the standard ESP-IDF API directly:

```c
// Publish
esp_event_post(KB_EVENTS, KB_EVENT_SYSTEM_ACTION, &payload, sizeof(payload), 0);

// Subscribe
esp_event_handler_register(BLE_EVENTS, BLE_EVENT_PAIRING_COMPLETE, my_handler, NULL);
```

**Event definitions:**

```c
// KB_EVENTS
KB_EVENT_SYSTEM_ACTION  // payload: kb_sys_action_event_t { action_code, event }
KB_EVENT_LED_STATE      // payload: uint8_t (HID LED bitmask)

// BLE_EVENTS
BLE_EVENT_ROUTING_CHANGED     // payload: bool active
BLE_EVENT_PROFILE_CONNECTED   // payload: int profile_idx
BLE_EVENT_PROFILE_DISCONNECTED // payload: int profile_idx
BLE_EVENT_PAIRING_STARTED     // payload: int profile_idx
BLE_EVENT_PAIRING_COMPLETE    // payload: ble_pairing_result_t { profile_idx, addr_type, addr[6] }
BLE_EVENT_PAIRING_FAILED      // payload: int profile_idx
BLE_EVENT_PAIRING_TIMEOUT     // payload: int profile_idx

// CONFIG_EVENTS
CONFIG_EVENT_KIND_UPDATED     // payload: config_update_event_t { kind, key[16] }
```

**Who publishes / who subscribes:**

| Event | Publisher | Subscribers |
|-------|-----------|-------------|
| `KB_EVENT_SYSTEM_ACTION` | `kb_system_action` | `ble_controller`, `kb_custom_key` |
| `KB_EVENT_LED_STATE` | `kb_state` | `rgb_module` |
| `BLE_EVENT_PAIRING_COMPLETE` | `blemod` | `cfg_ble`, `status_module` |
| `BLE_EVENT_ROUTING_CHANGED` | `blemod` | `status_module` |
| `BLE_EVENT_PROFILE_CONNECTED/DISCONNECTED` | `blemod` | `status_module` |
| `BLE_EVENT_PAIRING_STARTED/FAILED/TIMEOUT` | `blemod` | `status_module` |
| `CONFIG_EVENT_KIND_UPDATED` | `cfgmod` | `kb_custom_key`, `status_module` |

---

### Keyboard Component

**Location:** `components/keyboard/`

The core HID pipeline. Responsible for everything from raw GPIO matrix state to final HID report delivery.

**Key files:**

| File | Responsibility |
|------|----------------|
| `kb_manager.c` | Scan task (1200 Hz), debounce (5 scans), report scheduling |
| `kb_macro.c` | Virtual NKRO bitmap, action dispatcher, macro + tap worker tasks |
| `kb_system_action.c` | Generic tap/hold state machine (5 gesture types); publishes `KB_EVENT_SYSTEM_ACTION` |
| `kb_custom_key.c` | Custom key modes (PressRelease, MultiAction); subscribes to `KB_EVENT_SYSTEM_ACTION` and `CONFIG_EVENT_KIND_UPDATED` |
| `kb_matrix.c` | GPIO driver, ISR, column drive sequencing |
| `kb_report.c` | USB/BLE HID report routing with protocol selection |
| `kb_layout.c` | Keymap lookup with PSRAM cache + DRAM mirrors |
| `kb_state.c` | Host LED status tracking; publishes `KB_EVENT_LED_STATE` on change |

**Scanning:**

- Columns driven LOW one at a time; rows read (LOW = pressed, due to diodes)
- Debounce: **5 consecutive scans** required before a state change is registered
- Polling target: **1200 Hz**
- Idle: scan task sleeps; GPIO interrupt on any key wakes it
- Minimum report rate: **1 Hz** (keeps the host alive even when nothing changes)

**FreeRTOS Tasks:**

| Task | Priority | Stack | Purpose |
|------|----------|-------|---------|
| `kb_mgr` | 5 | 6 KB | Matrix scan, debounce, action dispatch, report send |
| `kb_sys_action` | 5 | 4 KB | Tap/hold timeout polling (every 10 ms) |
| `kb_macro` | 4 | 5 KB | Macro execution from 32-item queue |
| `kb_tap_0`–`kb_tap_3` | 4 | 3 KB each | Fire-tap workers (4 parallel) |

**Report Routing Priority:**

1. BLE — if routing is active and at least one profile is connected
2. USB — if device is mounted and endpoint is ready
   - Boot protocol (6KRO): 1 modifier byte + 6 keycode bytes
   - Report protocol (NKRO): 29-byte bitmap (up to 231 simultaneous keys)

---

### Config Module

**Location:** `components/config_module/`

The unified source of truth for all persistent configuration. No other component talks directly to NVS; they all go through the config module.

**Sub-modules:**

| Sub-module | NVS Namespace | Contents |
|------------|---------------|----------|
| `cfg_layouts` | `cfg_lay` | 4 keymap layers (6×18 keys each) |
| `cfg_macros` | `cfg_mac` | Up to 64 macros (0–63) |
| `cfg_custom_keys` | `cfg_ck` | Up to 120 custom keys (0–119) |
| `cfg_ble` | `cfg` (key `k2_ble_cfg`) | 9 BLE profiles + routing state |
| `cfg_system` | `cfg` (key `k3_sys`) | Device name, sleep timeout, RGB brightness, BLE enabled |
| `cfg_physical` | `cfg` (key `k4_physical`) | Raw JSON describing keyboard geometry |

**Registration Model:**

Every config kind registers a set of callbacks:

```c
cfgmod_register_kind(
    kind,           // e.g. CFGMOD_KIND_MACRO
    default_fn,     // populate struct with firmware defaults
    deserialize_fn, // cJSON → struct
    serialize_fn,   // struct → cJSON
    update_fn,      // called after USB SET (reload from NVS)
    struct_size
);
```

After every successful write, `cfgmod` publishes `CONFIG_EVENT_KIND_UPDATED` so interested modules can react without holding a direct reference to each other. The `update_fn` callbacks registered with `cfgmod_register_kind()` are still called for internal reload (e.g. `on_ble_updated` in `cfg_ble.c`); the event is an additional broadcast on top of them.

**Storage Strategy:**

- **Write path:** Binary blobs (fast, compact)
- **Read path:**
  1. Apply defaults
  2. Try binary load (exact size match = current firmware format)
  3. Fall back to JSON deserialization (transparent format migration)

**Layer Cache (3-tier):**

| Tier | Location | Contents |
|------|----------|----------|
| `s_psram_cache` | PSRAM | All 4 layers |
| `s_dram_base` | DRAM | Layer 0 (Base) always resident |
| `s_dram_swap` | DRAM | One additional layer at a time |

**Limits:**

| Resource | Limit |
|----------|-------|
| Layers | 4 |
| Keys per layer | 108 (6 × 18) |
| Macros | 64 (IDs 0–63) |
| Events per macro | 256 |
| Custom keys | 120 (IDs 0–119) |
| BLE profiles | 9 |

---

### USB Module

**Location:** `components/usb_module/`

Dual-interface HID device powered by TinyUSB. Interface 0 carries keyboard reports; Interface 1 is a vendor-defined comm channel for configuration.

**Interface 0 — Keyboard HID:**

| Report ID | Name | Size | Direction | Purpose |
|-----------|------|------|-----------|---------|
| 1 | `KEYBOARD` | 8 bytes | IN | 6KRO boot keyboard |
| 2 | `NKRO` | 29 bytes | IN | N-Key Rollover bitmap |
| 4 | `CONSUMER` | 2 bytes | IN | Media/volume keys |
| — | Output | 1 byte | OUT | LED state (Caps Lock, etc.) |

**Interface 1 — Comm Channel:**

| Report ID | Size | Direction |
|-----------|------|-----------|
| 3 | 63 bytes | Bidirectional |

A custom Blast+Reconcile protocol with CRC-8 validation handles payloads up to ~21 KB. See [Communication Protocol](#communication-protocol) for details.

**FreeRTOS Tasks:**

| Task | Priority | Core | Stack | Purpose |
|------|----------|------|-------|---------|
| `usb_task` | 5 | 1 | 4 KB | Polls `tud_task()` |
| `usb_processing_task` | 5 | Any | 8 KB | Routes incoming packets |
| `timeouts_task` | 5 | Any | 4 KB | Monitors RX/TX inactivity |
| `usb_tx_task` | 10 | Any | 4 KB | TX state machine |

**Public API:**

```c
void usb_init(void);
bool usb_keyboard_use_boot_protocol(void);
bool usb_send_keyboard_6kro(uint8_t modifier, const uint8_t keycodes[6]);
bool usb_send_keyboard_nkro(uint8_t modifier, const uint8_t *bitmap, uint16_t len);
bool usb_send_consumer_report(uint16_t keycode);
void usbmod_register_callback(usb_msg_module_t module, usb_data_callback_t callback);
```

---

### BLE Module

**Location:** `components/ble_module/`

HOGP-compliant BLE HID peripheral using the NimBLE stack. Supports up to 9 independent pairing profiles with static random address rotation.

**GATT Services:**

| Service | UUID | Notable Characteristics |
|---------|------|------------------------|
| Device Information | `0x180A` | PnP ID (Vendor `0x02E5`, Product `0x0121`) |
| Battery | `0x180F` | Battery Level (hardcoded 69%, notify) |
| HID | `0x1812` | Report Map, Keyboard Input (8 bytes), Consumer Input (2 bytes), LED Output, Protocol Mode |

**HID Report Format (BLE):**

- **Keyboard (Report ID 1, 8 bytes):** 1 modifier + 1 reserved + 6 keycodes (6KRO)
- **Consumer (Report ID 2, 2 bytes):** 16-bit consumer control usage code

**Advertising State Machine:**

| Mode | Condition | Discoverable | Interval |
|------|-----------|--------------|----------|
| PAIRING | Profile being paired | Yes | 20–30 ms (fast, 60 s timeout) |
| RECONNECTING | Directed reconnect | No | 20–30 ms (fast, 15 s timeout) |
| BACKGROUND | No active operation | No | 800–1000 ms (indefinite) |

**Address Rotation:**

Each profile advertises with a unique static random address derived from the device MAC:

```
rand_addr    = public_mac
rand_addr[5] |= 0xC0           // mark as Static Random
rand_addr[0]  = (rand_addr[0] + profile_id + addr_nonce) & 0xFF
```

When a profile is re-paired, `addr_nonce` increments, so the host sees a brand-new device.

**Security:** Just Works (no PIN), Secure Connections (BLE 4.2+ ECDH), bonding enabled. Keys stored in NVS via `ble_store_config`.

**Event publishing:** `blemod` posts a `BLE_EVENT_*` on every significant state transition — connection, disconnection, routing toggle, pairing start/complete/fail/timeout. Pairing credentials are **not** saved directly by `blemod`; it fires `BLE_EVENT_PAIRING_COMPLETE` and `cfg_ble` handles the NVS write, keeping storage concerns out of the BLE stack.

**Public API:**

```c
void ble_hid_init(void);
bool ble_hid_is_connected(void);
esp_err_t ble_hid_send_keyboard_report(uint8_t *report, uint16_t len);
esp_err_t ble_hid_send_consumer_report(uint16_t keycode);
void ble_hid_profile_pair(uint8_t id);
void ble_hid_profile_connect_and_select(uint8_t id);
void ble_hid_profile_toggle_connection(uint8_t id);
void ble_hid_set_routing_active(bool active);
bool ble_hid_is_routing_active(void);
uint16_t ble_hid_get_connected_profiles_bitmap(void);
int ble_hid_get_pairing_profile(void);
```

---

### RGB Module

**Location:** `components/rgb_module/`

Drives the onboard WS2812 LED on GPIO 48 using the ESP32-S3's RMT peripheral through the `espressif__led_strip` managed component.

Subscribes to `KB_EVENT_LED_STATE` during `rgb_init()`. When Caps Lock is active the LED turns red; when off it is extinguished. Because RGB reacts to an event rather than being called directly by the keyboard module, the `keyboard` component has no dependency on `rgb_module`.

---

### Button Module

**Location:** `components/button_module/`

Handles the BOOT button with debounced single-press and double-press callbacks.

In `main.c`, the button is wired to inject test keystrokes via `kb_macro_process_action()` for development purposes.

---

### Status Module

**Location:** `components/status_module/`

Publishes device status information (connection state, active profiles, etc.) that can be queried over the comm channel (`MODULE_STATUS`, `0x03`).

---

## Key Action System

Every key in every layer stores a **16-bit action code**. The range of the code determines how `kb_macro_process_action()` handles it:

| Range | Hex | Description |
|-------|-----|-------------|
| None | `0x0000` | No operation |
| HID Keys | `0x0001`–`0x00FF` | Standard USB HID keycodes |
| Media Keys | `0x0100`–`0x01FF` | Consumer control codes |
| System Actions | `0x2000`–`0x20FF` | Layer switches, BLE control, media, RGB |
| Custom Keys | `0x3000`–`0x3FFF` | User-defined custom key (`id = code - 0x3000`) |
| Macros | `0x4000`–`0x4FFF` | Macro trigger (`id = code - 0x4000`) |
| Transparent | `0xFFFF` | Fall through to the layer below |

**System Action Codes (`0x2000`–`0x20FF`):**

| Action | Purpose |
|--------|---------|
| `SYS_ACTION_LAYER_BASE/FN1/FN2` | Switch active layer |
| `SYS_ACTION_BLE_TOGGLE` | Enable/disable BLE routing |
| `SYS_ACTION_BLE_1`–`SYS_ACTION_BLE_9` | Select/pair BLE profile 1–9 |
| `MEDIA_ACTION_NEXT/PREV/PLAY` | Media track control |
| `SYS_ACTION_MUTE` | Toggle mute |
| `SYS_ACTION_VOLUME_UP/DOWN` | Volume control |
| `SYS_ACTION_RGB_MODE_NEXT` | Cycle RGB animation mode |
| `SYS_ACTION_RGB_SPEED_NEXT` | Cycle RGB speed |
| `SYS_ACTION_RGB_BRIGHTNESS_UP/DOWN` | RGB brightness |
| `SYS_ACTION_BRIGHTNESS_UP/DOWN` | Display brightness |

---

## Layer System

The firmware supports **4 independent keymap layers**:

| Layer | Index | Activation |
|-------|-------|------------|
| Base | 0 | Always active (default) |
| FN1 | 1 | FN1 key held |
| FN2 | 2 | FN2 key held |
| FN3 | 3 | FN1 **and** FN2 held simultaneously |

**Transparent Keys (`0xFFFF`):** When the active layer has a transparent action on a key, the lookup falls through to Base (layer 0). This lets you selectively override only the keys you care about in FN layers.

---

## Macro Engine

**Limits:** 64 macros (IDs 0–63), 256 events per macro.

**Event Types:**

| Type | Description |
|------|-------------|
| `MACRO_EVT_KEY_PRESS` | Virtual key-down |
| `MACRO_EVT_KEY_RELEASE` | Virtual key-up |
| `MACRO_EVT_KEY_TAP` | Press, hold for duration, release |
| `MACRO_EVT_DELAY_MS` | Sleep for N milliseconds |

Macros can call other macros (up to **5 levels of recursion**).

**Execution Modes:**

| Mode | ID | Behavior |
|------|----|----------|
| `ONCE_STACK_ONCE` | 0 | Default. Queue one extra execution if triggered while running |
| `ONCE_NO_STACK` | 1 | Ignore re-presses while running |
| `ONCE_STACK_N` | 2 | Queue up to `stack_max` executions |
| `HOLD_REPEAT` | 3 | Repeat continuously while key is held; finish current cycle on release |
| `HOLD_REPEAT_CANCEL` | 4 | Repeat while held; abort immediately on release |
| `TOGGLE_REPEAT` | 5 | Toggle looping on/off with each press |
| `TOGGLE_REPEAT_CANCEL` | 6 | Toggle; abort mid-cycle when stopping |
| `BURST_N` | 7 | Fire `repeat_count` times in rapid succession |

---

## Custom Keys

Custom keys are mapped to action codes in the `0x3000`–`0x3FFF` range. Unlike macros, they intercept the raw tap/hold gesture events from the keyboard engine rather than just receiving a trigger.

**Mode 0 — PressRelease:**

Assigns a different action to key-down vs. key-up:

| Trigger | Action |
|---------|--------|
| Key pressed | `press_action` fired as a tap (after `press_tap_release_delay_ms`) |
| Key released | `release_action` fired as a tap (after `release_tap_release_delay_ms`) |

The `wait_for_finish` flag makes the release action wait until the press action completes.

**Mode 1 — MultiAction:**

Routes three distinct gestures to three independent actions:

| Gesture | Threshold | Action |
|---------|-----------|--------|
| Single tap | Released before `hold_threshold_ms`, no second press within `double_tap_threshold_ms` | `tap_action` |
| Double tap | Second press within `double_tap_threshold_ms` of first release | `double_tap_action` |
| Hold | Key held past `hold_threshold_ms` | `hold_action` |

All inner actions are fired as taps via `kb_macro_fire_tap()`.

---

## BLE Multi-Profile

The BLE module manages up to **9 independent pairing profiles**. Each profile stores:

- Peer address type and 6-byte MAC
- `is_valid` flag
- `addr_nonce` (incremented on re-pair to rotate the advertising address)

**Profile Operations:**

| Operation | Key Combo (default) | Behavior |
|-----------|---------------------|----------|
| Select profile N | FN + Profile key (tap) | Connect to profile N, route reports there |
| Pair profile N | FN + Profile key (hold) | Erase old bond, start pairing advertising |
| Toggle BLE | `SYS_ACTION_BLE_TOGGLE` | Enable/disable BLE report routing |

**Multiple profiles can be connected at the same time**, but only the selected profile receives HID reports. Switching profiles is instantaneous — no re-pairing needed.

**Advertising:** After pairing, the device returns to low-power background advertising (800–1000 ms interval) if the selected profile is disconnected, so it reconnects automatically when the host comes back.

---

## Web Configurator

**Location:** `configurator/`

A React 19 + TypeScript single-page application that communicates with the keyboard over WebHID.

**Requirements:** Chrome, Edge, or Opera (WebHID is not available in Firefox).

**Features:**

- **Visual key editor** — Click any key, assign any action. Supports all 4 layers. KLE (Keyboard Layout Editor) import/export for physical layout.
- **Macro editor** — Full CRUD for all 64 macros. Record macros by pressing physical keys, configure execution mode, import/export.
- **Custom key editor** — Full CRUD for all 120 custom keys. Configure PressRelease or MultiAction mode with timing controls.
- **BLE status panel** — See which profiles are connected, initiate pairing, toggle routing.
- **Developer mode** — Raw GET/SET form + packet log with flag decoding, for inspecting the comm protocol.
- **Auto-reconnect** — On device disconnect, the UI polls every 2 seconds and reconnects automatically. All data is re-fetched on reconnection.

**Technology Stack:**

| Layer | Technology |
|-------|------------|
| Framework | React 19 |
| Language | TypeScript |
| Build | Vite |
| State | Zustand (stores), React hooks (`useMacros`, `useCustomKeys`) |
| Transport | WebHID + custom `HIDTransport` class |
| High-level API | `DeviceController` (typed commands over transport) |

**Connection:** VID `0x303A`, PID `0x1324`, usage page `0xFFFF` (vendor-defined HID interface).

---

## Communication Protocol

The keyboard exposes a bidirectional comm channel on USB HID Interface 1 (Report ID 3, 63 bytes per report). A custom **Blast+Reconcile** protocol with CRC-8 integrity checking handles payloads up to ~21 KB.

**Packet structure (63 bytes):**

```
Byte  0     : flags       — control bitfield
Bytes 1–2   : remaining   — packets remaining after this one (little-endian u16)
Byte  3     : payload_len — valid bytes in payload field (0–58)
Bytes 4–61  : payload     — application data (zero-padded)
Byte  62    : crc8        — CRC-8 over bytes 0–61 (polynomial 0x07)
```

**For payloads ≤ 58 bytes** (single packet):
```
Host ──[FIRST|LAST, data]──> Device
Host <──[ACK|OK or ERR]───── Device
```

**For payloads > 58 bytes** (Blast+Reconcile):
```
Phase 1 — Handshake:    Host ──[FIRST]──> Device, Device ──[ACK]──> Host
Phase 2 — Blast:        Host sends all MID packets without waiting for ACKs
Phase 3 — Reconcile:    Host ──[STATUS_REQ]──> Device ──[BITMAP]──> Host
                        Host retransmits any missing packets (up to 5 rounds)
Phase 4 — Commit:       Host ──[LAST]──> Device, Device ──[ACK|OK]──> Host
```

**Application payload format (after reassembly):**

```
[module_id: 1][cmd: 1][key_id: 1][JSON data: variable]
```

**Module IDs:**

| ID | Name | Purpose |
|----|------|---------|
| `0x00` | `MODULE_CONFIG` | Config read/write |
| `0x01` | `MODULE_SYSTEM` | System commands (key injection, reboot) |
| `0x02` | `MODULE_ACTION` | Reserved |
| `0x03` | `MODULE_STATUS` | Device status |

**Config Key IDs:**

| ID | Name | GET | SET |
|----|------|-----|-----|
| `0x00` | `CFG_KEY_TEST` | Test blob | Store test JSON |
| `0x01` | `CFG_KEY_HELLO` | Hello message | — |
| `0x02` | `CFG_KEY_PHYSICAL_LAYOUT` | Physical layout JSON | Store layout JSON |
| `0x03` | `CFG_KEY_LAYER_0` | Base layer | Store Base layer |
| `0x04` | `CFG_KEY_LAYER_1` | FN1 layer | Store FN1 layer |
| `0x05` | `CFG_KEY_LAYER_2` | FN2 layer | Store FN2 layer |
| `0x06` | `CFG_KEY_LAYER_3` | FN3 layer | Store FN3 layer |
| `0x07` | `CFG_KEY_MACROS` | Macro outline (IDs + names) | — |
| `0x08` | `CFG_KEY_MACRO_LIMITS` | `{maxEvents, maxMacros}` | — |
| `0x09` | `CFG_KEY_MACRO_SINGLE` | Full macro by `{id}` | Upsert / `{delete: id}` |
| `0x0A` | `CFG_KEY_CKEYS` | CKey outline | — |
| `0x0B` | `CFG_KEY_CKEY_SINGLE` | Full CKey by `{id}` | Upsert / `{delete: id}` |

For the full specification including failure recovery, CRC details, and system commands, see [COMM_PROTOCOL.md](COMM_PROTOCOL.md).

---

## Roadmap

- [x] USB HID keyboard (6KRO + NKRO)
- [x] Bluetooth LE HID (HOGP, 9 profiles)
- [x] 4-layer keymap system with transparent keys
- [x] Macro engine (64 macros, 8 execution modes)
- [x] Custom keys (PressRelease + MultiAction with tap/hold/double-tap)
- [x] Tap/hold gesture recognition engine
- [x] Persistent NVS storage with binary/JSON dual-path
- [x] React web configurator over WebHID
- [x] Blast+Reconcile multi-packet USB protocol with CRC-8
- [x] BLE multi-profile with static random address rotation
- [ ] Battery level reporting (BAS characteristic currently hardcoded)
- [ ] 2.4 GHz wireless via ESP-NOW dongle
- [ ] Split keyboard support (host/slave communication)
- [ ] QMK firmware compatibility layer
- [ ] OLED display support
