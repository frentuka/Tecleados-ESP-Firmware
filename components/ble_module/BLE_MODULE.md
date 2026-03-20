# BLE Module

## Overview

The BLE module implements a **HOGP-compliant BLE HID keyboard peripheral** using the ESP-IDF NimBLE stack. It exposes the device as a Bluetooth keyboard to up to `CFG_BLE_MAX_PROFILES` paired hosts simultaneously, manages all advertising modes, handles pairing and bonding lifecycle, and provides a clean API for sending keyboard and media control reports.

Entry point for the rest of the firmware is `blemod.h`. Consumers call `ble_hid_init()` once from `main.c`, then use the profile and report APIs to drive BLE behaviour from the keyboard logic.

---

## Architecture

### Profile Model

A **profile** is a saved pairing record for one host device (phone, laptop, etc). The module supports `CFG_BLE_MAX_PROFILES` (currently 9) simultaneous profiles, stored in NVS via the config module.

Each profile (from `cfg_ble.h`) holds:
- `is_valid` — whether the profile has ever been successfully paired
- `addr_type` / `val` — the peer's Bluetooth address (used to validate bonding)
- `addr_nonce` — incremented on each re-pair, causing the ESP32's advertised address to rotate so the host sees it as a new device

The **selected profile** (`cfg_ble_state_t.selected_profile`) is the one currently targeted for keyboard output. Multiple profiles can be connected at once (the NimBLE stack manages multiple simultaneous connections); only the selected one receives HID reports.

Runtime connection state is tracked separately in `s_conn_handles[CFG_BLE_MAX_PROFILES]` — a NimBLE connection handle per profile slot, or `BLE_HS_CONN_HANDLE_NONE` when disconnected.

### Advertising State Machine

The module operates one of three advertising modes at any time, selected by two sentinel flags:

| Mode             | Condition                  | Disc mode               | Intervals          | Duration            |
|------------------|----------------------------|-------------------------|--------------------|---------------------|
| **PAIRING**      | `s_pairing_profile != -1`  | GEN_DISC (discoverable) | 20–30 ms (fast)    | 60 s (master timer) |
| **RECONNECTING** | `s_directed_profile != -1` | NON_DISC                | 20–30 ms (fast)    | 15 s (stack timer)  |
| **BACKGROUND**   | neither flag set           | NON_DISC                | 800–1000 ms (slow) | forever             |

The Background mode only advertises when `ble_routing_enabled` is true AND the selected profile is disconnected and valid. If neither condition holds, advertising stops entirely.

`ble_hid_advertise()` is the single function that recalculates which mode applies and restarts advertising from scratch. It is called:
- On BLE stack sync (after init)
- On disconnect
- On advertising timeout (ETIMEOUT only)
- From the cooldown and pairing timeout timer callbacks
- From all public profile API functions

**Advertising stops automatically** if:
- BLE routing is disabled
- The active profile index is out of range (corrupt NVS guard)
- The selected profile is already connected and no explicit mode is active

### Static Random Address Rotation

Each profile advertises with a deterministic **Static Random Address** derived from the device's public MAC address:

```
rand_addr = public_mac
rand_addr[5] |= 0xC0          // mark as Static Random (BT spec requirement)
rand_addr[0] = (rand_addr[0] + profile_id + addr_nonce) & 0xFF
```

This means each profile slot has a distinct Bluetooth identity. When a profile is re-paired (`ble_hid_profile_pair()`), `addr_nonce` is incremented, causing the address to change — the host device sees a completely new keyboard and proceeds with fresh pairing instead of attempting to resume the old bond.

### Timers

Two `esp_timer` one-shot timers are created in `ble_hid_init()`:

**`s_adv_cooldown_timer` (10 s)** — Prevents instant reconnection loops after a manual disconnect (e.g. user disconnects from the host side). After a manual disconnect, advertising is held back for 10 seconds before resuming background mode. If the device re-enters pairing mode during the cooldown, the timer is cancelled and advertising starts immediately.

**`s_pairing_timeout_timer` (60 s)** — Absolute pairing guard. When pairing mode starts (`ble_hid_profile_pair()`), this timer is started. If 60 seconds pass without successful encryption, `s_pairing_profile` is cleared and the module falls back to background advertising. This prevents the device from being stuck in discoverable mode indefinitely if no host connects.

Both timers call `ble_hid_advertise()` on expiry, which recalculates the correct mode and restarts advertising (or stops it if no longer needed).

### Security Model

The module uses **"Just Works" BLE pairing** — no PIN or passkey required:

| Parameter           | Value                       | Meaning                                          |
|---------------------|-----------------------------|--------------------------------------------------|
| `sm_io_cap`         | `BLE_HS_IO_NO_INPUT_OUTPUT` | No display or keyboard on device                 |
| `sm_bonding`        | 1                           | Persist bond keys across connections             |
| `sm_sc`             | 1                           | Secure Connections (BLE 4.2+, ECDH key exchange) |
| `sm_mitm`           | 0                           | No MITM protection (acceptable for "Just Works") |
| `sm_our_key_dist`   | ENC + ID                    | Distribute encryption and identity keys          |
| `sm_their_key_dist` | ENC + ID                    | Accept encryption and identity keys              |

Bond keys are stored in NVS via `ble_store_config` (registered in `ble_hs_cfg.store_read_cb` / `store_write_cb`).

**Re-pairing** (host deleted its bond and tries to pair again) is handled by `BLE_GAP_EVENT_REPEAT_PAIRING`: the old bond is deleted with `ble_store_util_delete_peer()` and the connection retries with `BLE_GAP_REPEAT_PAIRING_RETRY`.

Pairing credentials (peer MAC address) are captured in `BLE_GAP_EVENT_CONNECT` (stored in `s_pending_addr`) and only committed to the config on `BLE_GAP_EVENT_ENC_CHANGE` with `status == 0` (encryption complete = pairing successful). This prevents saving invalid credentials from failed pairing attempts.

---

## GATT Service Table (`ble_hid_service.c`)

Three primary GATT services are registered via `ble_hid_svc_register()`:

### Device Information Service (DIS) — UUID 0x180A

| Char.  | UUID   | Props    | Value                                     |
|--------|--------|----------|-------------------------------------------|
| PnP ID | 0x2A50 | READ+ENC | Vendor=ESPRESSIF (0x02E5), Product=0x0121 |

### Battery Service (BAS) — UUID 0x180F

| Char.         | UUID   | Properties       | Notes                               |
|---------------|--------|------------------|-------------------------------------|
| Battery Level | 0x2A19 | READ+ENC, NOTIFY | Hardcoded 69% (no battery hardware) |

NimBLE auto-creates the CCCD (0x2902) descriptor when `BLE_GATT_CHR_F_NOTIFY` is set. On subscribe, `ble_hid_notify_battery_level()` is called immediately to push the current value to the host.

### HID Service — UUID 0x1812

| Characteristic          | UUID   | Properties                    | Notes                                                        |
|-------------------------|--------|-------------------------------|--------------------------------------------------------------|
| HID Information         | 0x2A4A | READ+ENC                      | v1.11, normally connectable                                  |
| Report Map              | 0x2A4B | READ+ENC                      | Descriptor for 2-report layout                               |
| Protocol Mode           | 0x2A4E | READ+ENC, WRITE_NO_RSP        | Boot (0) vs Report (1) protocol                              |
| Boot Keyboard Input     | 0x2A22 | READ+ENC, NOTIFY              | Legacy; mirrors keyboard report                              |
| Boot Keyboard Output    | 0x2A32 | READ+ENC, WRITE, WRITE_NO_RSP | LED state (Caps Lock, etc.)                                  |
| HID Control Point       | 0x2A4C | WRITE_NO_RSP                  | Suspend/resume hint                                          |
| Input Report (Keyboard) | 0x2A4D | READ+ENC, NOTIFY              | Report ID=1, 8 bytes; + Report Reference descriptor          |
| Input Report (Consumer) | 0x2A4D | READ+ENC, NOTIFY              | Report ID=2, 2 bytes; + Report Reference descriptor          |
| Output Report           | 0x2A4D | READ+ENC, WRITE, WRITE_NO_RSP | Report ID=1 Output; LED state; + Report Reference descriptor |

**Report Reference descriptors** (UUID 0x2908) are manually attached to each Report characteristic. They carry `{report_id, report_type}` encoded via `REPORT_REF_ARG(id, type)`. The encoding packs both values into a `void*` arg: high byte = ID, low byte = type (1=Input, 2=Output).

### Report Map

The HID Report Map defines two logical reports:

**Report ID 1 — Keyboard (8 bytes):**
```
Byte 0:   Modifier bitmap  (Left/Right: Ctrl, Shift, Alt, GUI)
Byte 1:   Reserved (always 0)
Bytes 2–7: Key codes (6KRO — up to 6 simultaneous non-modifier keys)
```

**Report ID 2 — Consumer Control (2 bytes):**
```
Bytes 0–1: 16-bit HID Consumer Control usage ID
           0x0000 = release (key up)
           e.g. 0x00E9 = Volume Up, 0x00EA = Volume Down
           Valid range: 0x0000–0x03FF per HID Usage Tables spec
```

### Protocol Mode

The host can switch between **Report Protocol** (default, mode=1) and **Boot Protocol** (mode=0) via the Protocol Mode characteristic. In Boot Protocol, keyboard reports are sent on the Boot Keyboard Input characteristic (`s_hid_boot_in_handle`) without a Report ID byte — required for BIOS/pre-OS compatibility. The module handles this in `ble_hid_tx_keyboard_report()` by routing to the appropriate handle based on `s_hid_protocol_mode`.

---

## Public API Summary

All public functions are declared in `include/blemod.h`.

| Function                                    | Description                                                                                    |
|---------------------------------------------|------------------------------------------------------------------------------------------------|
| `ble_hid_init()`                            | Initialize NimBLE stack, configure security, register GATT services, start advertising         |
| `ble_hid_is_connected()`                    | Returns `true` if the currently selected profile has an active connection                      |
| `ble_hid_send_keyboard_report(report, len)` | Send 8-byte keyboard HID report to the selected profile's connection                           |
| `ble_hid_send_consumer_report(keycode)`     | Send 16-bit consumer control report to the selected profile's connection                       |
| `ble_hid_profile_pair(id)`                  | Erase credentials for profile `id`, increment nonce, begin discoverable pairing (60 s timeout) |
| `ble_hid_profile_connect_and_select(id)`    | Set `id` as selected profile; start reconnection advertising if not connected                  |
| `ble_hid_profile_toggle_connection(id)`     | Disconnect profile `id` if connected, or start reconnection advertising if disconnected        |
| `ble_hid_set_routing_active(bool)`          | Enable/disable BLE output; disabling terminates all connections and stops advertising          |
| `ble_hid_is_routing_active()`               | Returns `true` if BLE routing is currently enabled                                             |
| `ble_hid_get_connected_profiles_bitmap()`   | Returns a bitmask where bit N=1 means profile N is currently connected                         |
| `ble_hid_get_pairing_profile()`             | Returns the profile currently being paired, or -1 if not in pairing mode                       |

**Error handling:**
- `ble_hid_send_keyboard_report()` and `ble_hid_send_consumer_report()` return `ESP_ERR_INVALID_STATE` if no connection is active, `ESP_FAIL` on NimBLE TX error, `ESP_OK` on success (or if routing is disabled — reports are silently dropped).

---

## Initialization Sequence

`ble_hid_init()` performs these steps in order:

1. **Initialize connection handle array** — fill `s_conn_handles[]` with `BLE_HS_CONN_HANDLE_NONE`
2. **`nimble_port_init()`** — initialize the NimBLE HCI transport (VHCI to integrated BT controller)
3. **Register stack callbacks** — `bleprph_on_sync` (called when stack is ready) and `bleprph_on_reset` (called on fatal error)
4. **Configure Security Manager** — set IO capability, enable bonding, enable Secure Connections, configure key distribution
5. **Configure bond store** — register `ble_store_config_read/write` for NVS-backed bond persistence
6. **Register GATT services** — call `ble_svc_gap_init()`, `ble_svc_gatt_init()`, `ble_hid_svc_register()`
7. **Set GAP identity** — device name (`BLE_DEVICE_NAME`) and appearance (`BLE_APPEARANCE_HID_KEYBOARD = 0x03C1`)
8. **Start NimBLE FreeRTOS task** — `nimble_port_freertos_init(ble_host_task)` runs the NimBLE event loop on a dedicated task; `bleprph_on_sync` fires once the stack is ready and triggers the first `ble_hid_advertise()` call
9. **Create timers** — `s_adv_cooldown_timer` (10 s post-disconnect) and `s_pairing_timeout_timer` (60 s pairing guard)

---

## Dependency Graph

```
blemod.c
  ├── ble_hid_service.c   (low-level GATT TX, service registration)
  ├── cfg_ble.h           (profile state persistence via config_module)
  └── NimBLE stack        (GAP, GATT, SM, HCI)

ble_hid_service.c
  ├── kb_state.h          (LED output report: kb_state_get_leds / kb_state_update_leds)
  └── NimBLE stack        (GATTS, mbuf)
```

The BLE module depends on `config_module` for profile persistence but does **not** depend on any other keyboard module. The keyboard module calls into `blemod.h` (not the reverse), keeping the dependency direction clean.
