# Split Keyboard Module — Architecture & Plan

## Overview

The **split module** turns two independent ESP32-S3 keyboards into a single logical keyboard. The two halves communicate over **ESP-NOW** using a custom encrypted protocol. Either half can assume the **MASTER** or **SLAVE** role — roles are negotiated at runtime and can be swapped on the fly.

The MASTER half is the one that connects to the host (USB/BLE) and produces HID reports. The SLAVE half scans its own matrix and forwards key-state deltas to the MASTER, which merges them into a unified report.

> **Design constraint:** ESP-NOW will also be used for dongle functionality in the future. The transport layer is therefore kept generic and protocol-agnostic so that split-keyboard traffic and dongle traffic can coexist on the same radio.

---

## Core Concepts

### 1. Roles: MASTER & SLAVE

| Aspect | MASTER | SLAVE |
|--------|--------|-------|
| HID output | Yes — sends reports over USB/BLE | No — radio only |
| Matrix scan | Its own rows/cols only | Its own rows/cols only |
| Report generation | Merges own + remote key states | N/A |
| Config authority | Source of truth for shared config | Receives config sync from master |
| Role assignment | Negotiated (not hardcoded) | Negotiated (not hardcoded) |

**Role negotiation rules:**
1. If a user has configured a **preferred role** via the configurator, that preference wins.
2. If both sides have the same preference (or no preference), the device with the **higher MAC address** becomes MASTER.
3. Role can be **swapped at runtime** via a system action key or configurator command. The swap is coordinated: MASTER sends `ROLE_SWAP_REQUEST`, SLAVE ACKs, both transition simultaneously.

### 2. Pairing

Before two halves can communicate, they must be **paired**. Pairing establishes a shared encryption key and stores each other's MAC address in NVS.

**Pairing flow:**
1. User puts both halves into **pairing mode** (system action key or configurator).
2. Both halves broadcast `DISCOVERY` beacons on a known ESP-NOW channel.
3. Upon receiving a beacon, each side sends a `PAIR_REQUEST` containing its device ID and a public key (X25519).
4. The other side responds with `PAIR_RESPONSE` containing its own public key.
5. Both sides derive a **shared secret** via X25519 key exchange.
6. The shared secret is stored in NVS. Future communication uses this key for AES-128-CCM encryption.
7. Both sides store each other's MAC address and mark the link as paired.

**Security model:**
- All post-pairing traffic is encrypted with AES-128-CCM (authenticated encryption).
- Replay protection via monotonic sequence numbers.
- Pairing itself uses a brief plaintext window (the key exchange) — acceptable because it requires physical user action on both sides simultaneously.

### 3. Full Layout on Both Sides

Both halves store the **complete keyboard layout** (all layers, macros, custom keys). This is necessary because:
- Either half can become MASTER at any time.
- The MASTER needs the full layout to resolve action codes for both halves.
- Config changes on one side are synced to the other.

Each half also knows **which rows/cols it physically owns**. This is stored as a per-device bitmask:
```
split_own_keys[KB_MATRIX_BITMAP_BYTES]  — bitmask of (row,col) positions this half scans
```

The MASTER maintains two bitmaps:
- `local_matrix[]` — keys pressed on this half
- `remote_matrix[]` — keys pressed on the other half (received via ESP-NOW)

These are OR'd together before action resolution, producing a unified view as if it were a single keyboard.

### 4. Key State Synchronization

The SLAVE sends key-state updates to the MASTER whenever its matrix changes (not on a timer — only on delta). The protocol uses a compact bitmap format.

**Sync message types:**
- `KEY_STATE_FULL` — complete matrix bitmap (used on initial sync / reconnect).
- `KEY_STATE_DELTA` — only changed bytes + positions (used for incremental updates).

**Latency budget:** The entire radio round-trip must stay under **2 ms** to maintain the keyboard's 1200 Hz effective scan rate. ESP-NOW unicast typically achieves <1 ms on a clean channel.

### 5. Configuration Synchronization

When the MASTER's config changes (via USB configurator), it pushes the updated config kind to the SLAVE:
1. MASTER detects `CONFIG_EVENT_KIND_UPDATED` from the event bus.
2. MASTER serializes the changed config as a JSON blob.
3. MASTER sends `CONFIG_SYNC` message(s) to SLAVE (fragmented if >250 bytes).
4. SLAVE deserializes and applies the config locally.
5. SLAVE ACKs the sync.

**Conflict resolution:** Most recent TIMESTAMP always wins. If both sides are temporarily disconnected and config changes happen on both, the most recent version overwrites the older one on reconnect. TIMESTAMP changes after every configuration update.

### 6. Connection Lifecycle

```
    ┌─────────┐                          ┌─────────┐
    │ DEVICE A│                          │ DEVICE B│
    └────┬────┘                          └────┬────┘
         │                                     │
         │◄── Both unpaired ──►                │
         │                                     │
         │  ──── DISCOVERY (broadcast) ────►   │
         │  ◄─── DISCOVERY (broadcast) ────    │
         │                                     │
         │  ──── PAIR_REQUEST ─────────────►   │
         │  ◄─── PAIR_RESPONSE ────────────    │
         │                                     │
         │  [shared secret derived & stored]   │
         │                                     │
         │  ──── ROLE_NEGOTIATE ───────────►   │
         │  ◄─── ROLE_NEGOTIATE ───────────    │
         │                                     │
         │  [roles decided: A=MASTER, B=SLAVE] │
         │                                     │
         │  ◄─── KEY_STATE_FULL ───────────    │
         │  ◄─── KEY_STATE_DELTA ──────────    │
         │  ──── HEARTBEAT ────────────────►   │
         │  ◄─── HEARTBEAT ────────────────    │
         │                                     │
         │  [operating normally]               │
         │                                     │
         │  ──── CONFIG_SYNC ──────────────►   │
         │  ◄─── CONFIG_SYNC_ACK ──────────    │
         │                                     │
```

### 7. Heartbeat & Disconnect Detection

- Slave sends `HEARTBEAT` every **200 ms** to which Master responds with `ACK`.
- If no message (of any type) is received for **500 ms**, the link is considered **stale**.
- If no message for **2000 ms**, the link is considered **disconnected**.
- On disconnect, the MASTER clears the remote matrix (releases all remote keys) to prevent stuck keys.
- Both sides attempt automatic reconnection using stored pairing data.

### 8. ESP-NOW Transport Layer (shared with future dongle)

The transport layer is a thin wrapper around `esp_now`:

```c
// Register a protocol handler (SPLIT, DONGLE, etc.)
esp_err_t split_transport_init(void);
esp_err_t split_transport_register_protocol(uint8_t protocol_id, transport_recv_cb_t cb);
esp_err_t split_transport_send(const uint8_t *peer_mac, uint8_t protocol_id,
                               const uint8_t *data, size_t len);
```

**Frame format (fits in ESP-NOW's 250-byte payload):**
```
┌──────────┬─────────┬──────────┬─────────┬──────────────┬──────────┐
│ magic(2) │ proto(1)│ type(1)  │ seq(2)  │ payload(var) │ mic(4)   │
└──────────┴─────────┴──────────┴─────────┴──────────────┴──────────┘

magic:   0x4B53 ("KS" — keyboard split)
proto:   protocol ID (0x01 = split, 0x02 = dongle, etc.)
type:    message type (per protocol)
seq:     monotonic sequence number (replay protection)
payload: protocol-specific data (up to 240 bytes)
mic:     AES-128-CCM authentication tag (4 bytes, truncated)
```

### 9. Power Management

- SLAVE can enter **light sleep** between scan cycles when no keys are pressed.
- ESP-NOW remains active during light sleep on ESP32-S3 (WiFi modem sleep).
- MASTER stays fully awake while connected to USB, but can sleep when on battery + BLE.

---

## Module File Structure

```
components/split/
├── CMakeLists.txt           # Build: depends on esp_wifi, esp_now, event_bus, config_module, keyboard
├── SPLIT_PLAN.md            # This document
├── include/
│   └── splitmod.h           # Public API (init, deinit, status queries)
├── split_transport.h        # ESP-NOW transport layer (internal)
├── split_transport.c        # ESP-NOW init, send/recv, protocol dispatch
├── split_protocol.h         # Wire format: message types, frame structs
├── split_protocol.c         # Serialize/deserialize, fragmentation
├── split_crypto.h           # X25519 key exchange, AES-128-CCM encrypt/decrypt
├── split_crypto.c
├── split_pair.h             # Pairing state machine, NVS storage
├── split_pair.c
├── split_role.h             # Role negotiation & swap logic
├── split_role.c
├── split_sync.h             # Key-state merge, config sync
├── split_sync.c
└── splitmod.c               # Main task, lifecycle management, event bus integration
```

---

## Event Bus Integration

New event base: `SPLIT_EVENTS`

| Event ID | Payload | Description |
|----------|---------|-------------|
| `SPLIT_EVENT_CONNECTED` | `split_peer_info_t` | Link established with peer |
| `SPLIT_EVENT_DISCONNECTED` | `uint8_t reason` | Link lost |
| `SPLIT_EVENT_ROLE_CHANGED` | `split_role_t` | Local role changed (MASTER/SLAVE) |
| `SPLIT_EVENT_PAIR_STARTED` | — | Entered pairing mode |
| `SPLIT_EVENT_PAIR_COMPLETE` | `split_peer_info_t` | Pairing succeeded |
| `SPLIT_EVENT_PAIR_FAILED` | `uint8_t reason` | Pairing failed/timed out |
| `SPLIT_EVENT_REMOTE_MATRIX` | `uint8_t bitmap[]` | Remote matrix updated (for MASTER's merge) |

---

## Configuration (cfgmod integration)

New kind: `CFGMOD_KIND_SPLIT` (to be added to cfgmod_kind_t)

**Stored settings:**
```c
typedef struct {
    bool     enabled;                    // Split mode on/off
    uint8_t  preferred_role;             // 0=auto, 1=prefer master, 2=prefer slave
    uint8_t  peer_mac[6];               // Paired peer's MAC (zeroed if unpaired)
    uint8_t  encryption_key[16];         // Derived AES-128 key (zeroed if unpaired)
    uint8_t  own_key_mask[KB_MATRIX_BITMAP_BYTES]; // Which keys this half owns
    uint8_t  channel;                    // WiFi channel for ESP-NOW (default: 1)
} cfg_split_t;
```

---

## System Action Codes (to be added to kb_layout.h)

| Code | Name | Description |
|------|------|-------------|
| `SYS_ACTION_SPLIT_PAIR` | Split Pair | Enter/exit pairing mode |
| `SYS_ACTION_SPLIT_ROLE_SWAP` | Split Role Swap | Request role swap with peer |
| `SYS_ACTION_SPLIT_DISCONNECT` | Split Disconnect | Disconnect from peer (implemented) |

---

## Task List

### Phase 1 — Foundation (current)
- [x] Create module file structure and CMakeLists.txt
- [x] Define public API in `splitmod.h`
- [x] Define wire protocol structures in `split_protocol.h`
- [x] Implement ESP-NOW transport layer (`split_transport.c`)
- [x] Create main module task skeleton (`splitmod.c`)
- [x] Add `SPLIT_EVENTS` to `event_bus.h`
- [x] Write this plan document

### Phase 2 — Pairing & Discovery
- [x] Implement broadcast discovery beacons
- [x] Implement X25519 key exchange (`split_crypto.c`)
- [x] Implement AES-128-CCM encryption/decryption
- [x] Build pairing state machine (`split_pair.c`)
- [x] Store/load pairing data in NVS via cfgmod
- [x] Add `CFGMOD_KIND_SPLIT` to config module
- [x] Add `SYS_ACTION_SPLIT_PAIR` system action
- [x] Configurator support for entering pairing mode (MODULE_SPLIT USB callback: start/cancel/unpair)

### Phase 3 — Role Negotiation
- [x] Implement role negotiation protocol (`split_role.c`)
- [x] MAC-address-based tiebreaker
- [x] Preferred-role config support
- [x] Role swap request/ACK protocol
- [x] Add `SYS_ACTION_SPLIT_ROLE_SWAP` system action
- [x] Add `SYS_ACTION_SPLIT_DISCONNECT` system action (unpair + clear stored data)
- [x] MASTER activates USB/BLE output; SLAVE deactivates
- [x] Emit `SPLIT_EVENT_ROLE_CHANGED` on transitions

### Phase 4 — Key State Synchronization
- [x] SLAVE: detect matrix changes and send `KEY_STATE_DELTA`
- [x] SLAVE: send `KEY_STATE_FULL` on connect/reconnect
- [x] MASTER: receive and validate remote key states
- [x] MASTER: merge remote + local matrices
- [x] MASTER: feed merged matrix into `kb_macro_process_action`
- [x] Sequence-number validation and replay rejection
- [x] Latency measurement and logging (RTT via echoed sent_us in heartbeat)

### Phase 5 — Configuration Synchronization
- [x] MASTER: listen for `CONFIG_EVENT_KIND_UPDATED`
- [x] MASTER: serialize and fragment config blobs
- [x] MASTER: send `CONFIG_SYNC` messages to SLAVE
- [x] SLAVE: receive, reassemble, and apply config
- [x] SLAVE: ACK config sync
- [x] Handle partial sync failure (retry logic — up to 3 attempts per fragment, 10 ms delay)
- [x] Full config resync on reconnect

### Phase 6 — Connection Resilience
- [x] Heartbeat send/receive (200 ms interval)
- [x] Stale detection (500 ms) — SPLIT_EVENT_STALE posted, status module notified
- [x] Disconnect detection (2000 ms) — clear remote matrix
- [x] Automatic reconnection using stored pairing data
- [x] Graceful role fallback (SLAVE becomes standalone on disconnect)
- [x] Status module integration (split_state + split_role in USB status JSON)
- [x] RGB module integration (blue=pairing, green=paired, red=disconnected, yellow=stale)

### Phase 7 — Power Management
- [x] SLAVE light-sleep between scans when idle (`CONFIG_PM_ENABLE=y` + `CONFIG_FREERTOS_USE_TICKLESS_IDLE=1` in sdkconfig; `pm_apply_slave_idle()` sets `light_sleep_enable=true` + min 80 MHz on role assignment)
- [x] WiFi modem sleep tuning for ESP-NOW latency (`esp_wifi_set_ps(WIFI_PS_NONE)` in `split_transport.c` — prevents up to 100 ms DTIM-period receive delay)
- [x] Battery-aware scan rate adjustment (`battery_module` component with weak ADC stub; `kb_manager_set_scan_divisor()` called from heartbeat send path: 4× at <10%, 2× at <30%, 1× otherwise)
- [x] Power state reporting to MASTER (heartbeat `battery_pct` field filled from `battery_get_level_pct()` on each SLAVE heartbeat send)

### Phase 8 — Configurator Integration
- [x] Add split settings page to web configurator (`SplitDashboard.tsx` with status display, pairing controls, test mode)
- [x] Pairing timeout enforcement (splitmod_start_pairing timeout_ms now respected; tick_pairing cancels and emits SPLIT_EVENT_PAIR_FAILED on expiry)
- [ ] Key-ownership editor (visual half-assignment per key)
- [x] Pairing status display (colored dot + state/role text in `SplitDashboard`)
- [ ] Role preference selector
- [ ] Channel selector
- [x] Test mode (remote matrix polling at 50 ms when MASTER+connected; 6×18 key grid in `SplitDashboard`)

### Phase 9 — Hardening & Testing
- [ ] Stress test: rapid key presses on both halves simultaneously
- [ ] Latency benchmarking (end-to-end from physical press to HID report)
- [ ] Range testing at various distances
- [ ] Interference testing (busy 2.4 GHz environment)
- [ ] Role swap under load
- [ ] Reconnection reliability testing
- [x] Encryption correctness validation (`test/split/test_split_crypto.c` — 12 host tests covering AES-128-CCM round-trip, MIC integrity, ECDH key exchange, KDF known-answer)
- [x] Fuzzing protocol parser with malformed packets (`test/split/test_split_protocol.c` — 18 host tests including 256-mutation bit-flip fuzzer)
- [ ] Power consumption profiling

### Future — Dongle Mode (out of scope, but designed for)
- [ ] `protocol_id = 0x02` for dongle traffic
- [ ] Dongle receives HID reports from MASTER over ESP-NOW
- [ ] Dongle presents as USB HID device to host
- [ ] Shared transport layer, separate protocol handler
