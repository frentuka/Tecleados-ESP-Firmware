# BLE COMM Channel — Implementation Plan

> **Goal:** Add a bidirectional, non-HID communication channel over Bluetooth Low Energy so the keyboard can be configured wirelessly from Android, iOS (via Bluefy/WebBLE browsers), and desktop — mirroring the existing USB COMM channel.

---

## Table of Contents

- [Situation Analysis](#situation-analysis)
- [Architectural Strategy](#architectural-strategy)
- [Terminology](#terminology)
- [Key Design Decisions](#key-design-decisions)
  - [Decision 1: Transport Abstraction Layer](#decision-1-transport-abstraction-layer)
  - [Decision 2: BLE GATT Service Design](#decision-2-ble-gatt-service-design)
  - [Decision 3: Packet Format Reuse](#decision-3-packet-format-reuse)
  - [Decision 4: MTU and Fragmentation](#decision-4-mtu-and-fragmentation)
  - [Decision 5: Configurator Client Strategy](#decision-5-configurator-client-strategy)
  - [Decision 6: Security Model](#decision-6-security-model)
  - [Decision 7: Split Keyboard Implications](#decision-7-split-keyboard-implications)
- [Phased Rollout Summary](#phased-rollout-summary)
- [Phase 1 — Transport Abstraction (comm_channel)](#phase-1--transport-abstraction-comm_channel)
  - [1.1 Core Type System](#11-core-type-system)
  - [1.2 Transport Interface](#12-transport-interface)
  - [1.3 USB Transport Adapter](#13-usb-transport-adapter)
  - [1.4 Migrate All Consumers to comm_channel](#14-migrate-all-consumers-to-comm_channel)
  - [1.5 Phase 1 Verification](#15-phase-1-verification)
- [Phase 2 — BLE COMM GATT Service](#phase-2--ble-comm-gatt-service)
  - [2.1 Custom GATT Service Definition](#21-custom-gatt-service-definition)
  - [2.2 BLE COMM RX Path](#22-ble-comm-rx-path)
  - [2.3 BLE COMM TX Path](#23-ble-comm-tx-path)
  - [2.4 BLE Transport Adapter](#24-ble-transport-adapter)
  - [2.5 Integration with blemod.c](#25-integration-with-blemodc)
  - [2.6 NimBLE Resource Tuning](#26-nimble-resource-tuning)
  - [2.7 Phase 2 Verification](#27-phase-2-verification)
- [Phase 3 — Configurator Dual-Transport Support](#phase-3--configurator-dual-transport-support)
  - [3.1 BLETransport.ts (Web Bluetooth Client)](#31-bletransportts-web-bluetooth-client)
  - [3.2 Transport Abstraction Layer (TypeScript)](#32-transport-abstraction-layer-typescript)
  - [3.3 UI Connection Selector](#33-ui-connection-selector)
  - [3.4 Phase 3 Verification](#34-phase-3-verification)
- [Phase 4 — Split Keyboard BLE COMM Proxy](#phase-4--split-keyboard-ble-comm-proxy)
  - [4.1 Slave-Side BLE COMM Forwarding](#41-slave-side-ble-comm-forwarding)
  - [4.2 Phase 4 Verification](#42-phase-4-verification)
- [Cross-Cutting Concerns](#cross-cutting-concerns)
  - [Thread Safety](#thread-safety)
  - [Memory Budget](#memory-budget)
  - [Concurrent USB + BLE COMM Sessions](#concurrent-usb--ble-comm-sessions)
  - [Error Handling](#error-handling)
- [Platform Compatibility Matrix](#platform-compatibility-matrix)
- [Risk Matrix](#risk-matrix)
- [File Change Manifest](#file-change-manifest)

---

## Situation Analysis

### What We Have Today

The firmware has **two completely independent USB interfaces** presented to the host:

| Interface                  | Role                                      | Direction | Status    |
| ----------------------------| -------------------------------------------| -----------| -----------|
| Interface 0 — HID Keyboard | Keyboard/NKRO/Consumer reports            | IN only   | ✅ Working |
| Interface 1 — HID COMM     | 63-byte vendor-defined bidirectional pipe | IN + OUT  | ✅ Working |

The COMM channel carries all configurator traffic: config read/write, status polling, split management, BLE profile control, and key injection. It uses the [Blast+Reconcile protocol](file:///home/srleg/Projects/Tecleados-ESP-Firmware/COMM_PROTOCOL.md) with CRC-8 integrity.

Over Bluetooth, the firmware only offers the **HID Keyboard** service (HOGP). There is no equivalent of Interface 1 over BLE. This means:

- ❌ Configuration is impossible without a USB cable
- ❌ Android/iOS devices cannot configure the keyboard at all
- ❌ Desktop BLE-only connections cannot access the configurator

### What We Want

```
                         ┌─────────────────────────────────────────────────┐
                         │           Keyboard Firmware                     │
                         │                                                 │
   USB Host              │  ┌────────────┐  ┌──────────────┐               │
      │  HID KBD ───────►│  │ Interface 0│  │ Interface 1  │               │
      │  COMM    ◄──────►│  │   (KBD)    │  │   (USB COMM) │               │
      │                  │  └────────────┘  └──────┬───────┘               │
      │                  │                         │                       │
   BLE Host              │  ┌────────────┐  ┌──────┴───────┐               │
      │  HID KBD ───────►│  │ HID Service│  │ comm_channel │ ◄── NEW!      │
      │  COMM    ◄──────►│  │   (HOGP)   │  │ (transport   │               │
      │                  │  └────────────┘  │  abstraction)│               │
      │                  │                  └──────┬───────┘               │
      │                  │                  ┌──────┴───────┐               │
      │                  │                  │ BLE COMM Svc │ ◄── NEW!      │
      │                  │                  │ (Custom GATT)│               │
      │                  │                  └──────────────┘               │
                         └─────────────────────────────────────────────────┘
```

Both USB and BLE COMM channels feed into the **same** callback routing system. The modules (`cfg_usb_callback`, `status_module_callback`, `split_usb_callback`, `ble_usb_callback`, `kb_system_usb_callback`) never know which transport delivered the data.

---

## Architectural Strategy

> [!IMPORTANT]
> **The Core Insight:** The existing module callbacks ([`execute_callback()`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/usb_module/usb_callbacks.c#L240-L253)) and the TX function ([`send_payload()`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/usb_module/usb_callbacks_tx.c#L366-L383)) are already transport-agnostic in spirit — they work on byte arrays and module IDs. The only thing coupling them to USB is the physical send/receive path.
>
> We do **not** rewrite the protocol. We do **not** duplicate the callback system. We introduce a thin **transport abstraction** that lets the same protocol, same callbacks, same blast+reconcile state machines work over either USB or BLE.

### The Strategy in One Sentence

**Extract the protocol engine from the USB module, wrap USB and BLE as interchangeable transports behind a common interface, and let the configurator choose which transport to use.**

---

## Terminology

| Term | Meaning |
|------|---------|
| **COMM channel** | The bidirectional data pipe used for configuration (not HID reports). Currently USB-only, will become transport-agnostic |
| **Transport** | A physical medium carrying COMM packets (USB HID Interface 1, or BLE GATT custom service) |
| **BLE COMM Service** | A new custom GATT service with RX/TX characteristics for configuration data |
| **comm_channel** | The new firmware abstraction layer that routes COMM traffic regardless of transport |
| **Active COMM transport** | Which transport is currently servicing a COMM session. Can be USB, BLE, or both simultaneously |

---

## Key Design Decisions

### Decision 1: Transport Abstraction Layer

**Approach:** Create a new `comm_channel` abstraction inside the existing `usb_module` component (not a new component). This keeps the blast+reconcile protocol code, CRC, callbacks, and TX/RX buffers exactly where they are — we only abstract the physical I/O at the bottom of the stack.

**Why inside `usb_module`:**
- The protocol code (blast mode, CRC, packet parsing) is already there
- Moving it to a new component would require massive refactoring of include paths and CMakeLists across every consumer
- The USB module is already the "COMM home" — adding BLE as a second physical transport is conceptually clean
- The rename from `usb_module` to `comm_module` can happen later if desired — it's cosmetic

**The abstraction is minimal:**
```c
// comm_channel.h (new header)
typedef enum {
    COMM_TRANSPORT_USB = 0,
    COMM_TRANSPORT_BLE,
    COMM_TRANSPORT_COUNT
} comm_transport_t;

// The transport must implement these two operations
typedef struct {
    bool (*send_packet)(const uint8_t *data, uint16_t len);  // Send a 63-byte packet
    bool (*is_ready)(void);                                   // Can we send right now?
} comm_transport_ops_t;

// Registration API
void comm_channel_register_transport(comm_transport_t id, const comm_transport_ops_t *ops);
```

### Decision 2: BLE GATT Service Design

**Approach:** A single custom GATT service with two characteristics: one for RX (client→device writes), one for TX (device→client notifications).

```
Service:    TEF COMM Service  (128-bit custom UUID)
            UUID: 4D544546-0001-4B42-4254-455F434F4D4D
                  ("MTEF" + 0001 + "KB" + "BT" + "E_COMM")

Char 1:     COMM RX (Write Without Response + Write)
            UUID: 4D544546-0002-4B42-4254-455F434F4D4D
            Properties: WRITE | WRITE_NO_RSP
            Max size: 63 bytes (mirrors USB COMM_REPORT_SIZE)

Char 2:     COMM TX (Notify + Read)
            UUID: 4D544546-0003-4B42-4254-455F434F4D4D
            Properties: READ | NOTIFY
            Value: 63 bytes (mirrors USB COMM_REPORT_SIZE)
            Descriptors: CCCD (auto-created by NimBLE when NOTIFY flag is set)
```

**Why this design:**
- **63-byte packets** — Exactly mirrors USB. The same `usb_packet_msg_t` structure is reused byte-for-byte. No protocol changes needed.
- **Write Without Response** — Faster than Write With Response for blast mode. The blast+reconcile protocol already handles reliability at the application layer.
- **Two characteristics instead of one** — Separating RX and TX avoids ambiguity about read-back semantics and keeps the CCCD subscription clean.
- **Custom 128-bit UUIDs** — Required to avoid collisions with standard Bluetooth SIG services. The UUIDs are derived from "TEF COMM" for readability in debugging tools.

### Decision 3: Packet Format Reuse

**The existing 63-byte COMM packet format is reused identically over BLE.** No changes to:
- `usb_packet_msg_t` structure
- Flag byte definitions
- Blast+Reconcile state machines
- CRC-8 validation
- Module ID routing
- Application-level payload format

The configurator's `HIDTransport.ts` protocol logic can be reused with only the physical I/O layer swapped.

### Decision 4: MTU and Fragmentation

**Challenge:** BLE's default MTU is 23 bytes (20 bytes usable). Our packets are 63 bytes.

**Solution:** MTU negotiation to ≥ 66 bytes (63 bytes + 3 bytes ATT header). This is a very modest request — most modern BLE stacks negotiate 185+ bytes by default.

**sdkconfig change:**
```
CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=256
```

Why 256 instead of just 66? Because the MTU exchange is one-shot per connection, and setting it higher gives headroom at near-zero cost. 256 is well within the ESP32-S3's capabilities and is supported by virtually all modern centrals (Android 5.0+, iOS 10+, Chrome on desktop).

**Fallback:** If MTU negotiation fails to reach 66 (extremely unlikely on any 2020+ device), we perform application-level fragmentation within each 63-byte COMM packet. This is a safety net, not a primary path. The BLE transport adapter will chunk at the ATT layer if needed.

### Decision 5: Configurator Client Strategy

**The Web Configurator will support both WebHID (existing) and Web Bluetooth (new).**

| API | Used for | Browser Support |
|-----|----------|----------------|
| WebHID | USB COMM | Chrome, Edge, Opera (desktop) |
| Web Bluetooth | BLE COMM | Chrome (desktop + Android), Edge, Opera |

**iOS situation:**
- Safari does not support Web Bluetooth
- **Bluefy** (iOS browser) provides Web Bluetooth support — users install this free browser and open the same configurator URL
- This is a well-established pattern in the BLE IoT ecosystem

**Architecture:**

```typescript
// New transport abstraction in the configurator
interface ITransport {
    connect(): Promise<void>;
    disconnect(): Promise<void>;
    isConnected(): boolean;
    sendPacket(data: Uint8Array): Promise<void>;
    onPacketReceived(callback: (data: Uint8Array) => void): void;
    onConnectionChange(callback: (connected: boolean) => void): void;
}

// HIDTransport implements ITransport (refactored from current code)
// BLETransport implements ITransport (new)
// DeviceController takes an ITransport instead of HIDTransport directly
```

### Decision 6: Security Model

The BLE COMM service **requires encryption** (same as the existing HID service). All COMM characteristics use `BLE_GATT_CHR_F_READ_ENC` / `BLE_GATT_CHR_F_WRITE_ENC` flags, meaning:

- A device must be **bonded** (paired) before it can read/write COMM data
- This uses the existing "Just Works" pairing model already in `blemod.c`
- No additional pairing flow is needed — if you can type on the keyboard via BLE, you can also configure it

**Why this is sufficient:**
- The configuration data is not sensitive enough to warrant PIN/passkey entry
- The keyboard is already physically in the user's hands
- MITM attacks on keyboard configuration are impractical (the attacker would need to be within BLE range and bonded)

### Decision 7: Split Keyboard Implications

**Current behavior:**
- The USB COMM channel is physically on whichever half has the USB cable
- BLE commands sent to the slave via USB are proxied over ESP-NOW to the master
- Config writes are synced from master to slave after the write

**With BLE COMM:**
- The BLE COMM service runs on the **master half only** (since the slave's BLE is suspended)
- A configurator connected via BLE is always talking to the master — no proxying needed for BLE commands
- Config writes still sync to the slave via ESP-NOW (existing mechanism)
- If the configurator connects to the slave via USB while the master has BLE COMM active, both channels can coexist independently

**No proxy logic needed for BLE COMM.** This is simpler than USB, where the slave must proxy BLE commands. The BLE COMM is inherently on the master, which is the BLE authority.

---

## Phased Rollout Summary

| Phase | Scope | Risk | Duration Estimate |
|-------|-------|------|-------------------|
| **Phase 1** | Transport abstraction on firmware side | Low — refactor only, no new functionality | 1–2 days |
| **Phase 2** | BLE COMM GATT service + BLE transport adapter | Medium — new GATT service, NimBLE integration | 2–3 days |
| **Phase 3** | Configurator Web Bluetooth support + transport abstraction | Medium — new browser API, UI changes | 2–3 days |
| **Phase 4** | Split keyboard BLE COMM integration | Low — mostly verification | 1 day |

**After each phase, the keyboard must still work identically to today.** Phase 1 is a pure refactor with zero behavioral change. Phase 2 adds the BLE service but the USB path remains primary. Phase 3 adds the client-side support.

---

## Phase 1 — Transport Abstraction (comm_channel)

### Goal
Decouple the COMM protocol engine from USB physical I/O so that a second transport (BLE) can be plugged in without touching the protocol code.

---

### 1.1 Core Type System

#### [NEW] `components/usb_module/include/comm_channel.h`

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "usb_defs.h"

/* ── Transport identifiers ──────────────────────────────────────── */

typedef enum {
    COMM_TRANSPORT_USB = 0,
    COMM_TRANSPORT_BLE,
    COMM_TRANSPORT_COUNT
} comm_transport_t;

/* ── Transport operations ───────────────────────────────────────── */

typedef struct {
    /** Send a single 63-byte COMM packet over this transport.
     *  The packet is already CRC-stamped. Returns true on success. */
    bool (*send_packet)(const uint8_t *packet, uint16_t len);

    /** Returns true if this transport is physically ready to send. */
    bool (*is_ready)(void);
} comm_transport_ops_t;

/* ── Public API ─────────────────────────────────────────────────── */

/** Register a transport's send/ready operations. Called during init. */
void comm_channel_register_transport(comm_transport_t id,
                                     const comm_transport_ops_t *ops);

/** Called by a transport when it receives a raw 63-byte packet.
 *  Routes it into the shared processing queue. */
void comm_channel_receive_packet(comm_transport_t source,
                                 const uint8_t *packet, uint16_t len);

/** Send a fully assembled payload back through a specific transport.
 *  This is the transport-aware version of send_payload(). */
bool comm_channel_send_payload(comm_transport_t target,
                               const uint8_t *payload, uint16_t len);

/** Legacy send_payload() — sends through whichever transport is
 *  currently "active" (the one that sent the last received packet).
 *  Maintains backward compatibility for existing module callbacks. */
bool send_payload(const uint8_t *payload, uint16_t payload_len);
```

**Key insight:** `send_payload()` keeps its exact current signature. Existing module callbacks (`cfgmod.c`, `statusmod.c`, `split_usb.c`) continue calling it without any changes. Internally, it now routes to whichever transport delivered the current request.

---

### 1.2 Transport Interface

#### [MODIFY] `components/usb_module/usb_callbacks_tx.c`

Add a **reply-target** variable that tracks which transport should receive the response:

```c
// New: tracks which transport to reply on
static comm_transport_t s_reply_transport = COMM_TRANSPORT_USB;

// send_payload() now routes through comm_channel
bool send_payload(const uint8_t *payload, uint16_t payload_len) {
    return comm_channel_send_payload(s_reply_transport, payload, payload_len);
}
```

The TX task, blast mode logic, and buffer management stay exactly as they are. Only the final `send_single_packet()` call at the bottom of the stack is redirected through the transport ops.

#### [MODIFY] `components/usb_module/usb_send.c`

`send_single_packet()` currently calls `tud_hid_n_report(ITF_NUM_HID_COMM, ...)`. This becomes the USB transport's `send_packet` implementation. The function itself becomes the USB adapter.

---

### 1.3 USB Transport Adapter

#### [NEW] `components/usb_module/comm_transport_usb.c`

A thin file (~30 lines) that wraps the existing USB send path:

```c
#include "comm_channel.h"
#include "usb_descriptors.h"
#include "usb_crc.h"
#include "tinyusb.h"

static bool usb_comm_send_packet(const uint8_t *packet, uint16_t len) {
    // Existing send_single_packet logic, extracted
    if (!tud_hid_n_ready(ITF_NUM_HID_COMM)) return false;
    return tud_hid_n_report(ITF_NUM_HID_COMM, REPORT_ID_COMM, packet, len);
}

static bool usb_comm_is_ready(void) {
    return tud_mounted() && tud_hid_n_ready(ITF_NUM_HID_COMM);
}

static const comm_transport_ops_t s_usb_ops = {
    .send_packet = usb_comm_send_packet,
    .is_ready    = usb_comm_is_ready,
};

void comm_transport_usb_init(void) {
    comm_channel_register_transport(COMM_TRANSPORT_USB, &s_usb_ops);
}
```

---

### 1.4 Migrate All Consumers to comm_channel

**The beauty of this design:** No consumer migration is needed! All existing callbacks (`cfg_usb_callback`, `status_module_callback`, etc.) call `send_payload()`, which now internally routes through `comm_channel`. The callbacks remain registered via `usbmod_register_callback()` — this function name is a historical artifact but its behavior is transport-agnostic (it just stores function pointers in an array indexed by module ID).

**Optional rename:** `usbmod_register_callback()` → `comm_register_callback()` can be done later with a `#define` alias for backward compatibility. Not required for functionality.

**One subtle change:** `usb_callbacks.c`'s `usbmod_tud_hid_set_report_cb()` — where raw USB packets arrive from TinyUSB — must call `comm_channel_receive_packet(COMM_TRANSPORT_USB, ...)` instead of directly enqueuing to the processing queue. This sets the reply-target to USB.

---

### 1.5 Phase 1 Verification

| Check | Method |
|-------|--------|
| All existing USB COMM functions work | Connect configurator, read all configs, write a layer, save macros |
| `send_payload()` backward compat | Verify status pushes arrive, config responses work |
| No regression in BLE HID | Connect BLE keyboard, type, switch profiles |
| No regression in split | Test split pairing, config sync, role swap |
| Build with no warnings | `idf.py build` clean |

---

## Phase 2 — BLE COMM GATT Service

### Goal
Add a custom GATT service to the BLE stack that provides a bidirectional 63-byte data channel, and wire it into the `comm_channel` abstraction.

---

### 2.1 Custom GATT Service Definition

#### [NEW] `components/ble_module/ble_comm_service.c`

This file defines the GATT service table for the COMM channel. It follows the same pattern as `ble_hid_service.c`:

```c
/* TEF COMM Service — Custom vendor service for keyboard configuration.
 *
 * Service UUID:  4D544546-0001-4B42-4254-455F434F4D4D
 * RX Char UUID:  4D544546-0002-4B42-4254-455F434F4D4D  (WRITE | WRITE_NO_RSP)
 * TX Char UUID:  4D544546-0003-4B42-4254-455F434F4D4D  (READ | NOTIFY)
 */

#define COMM_PACKET_SIZE 63  // Mirror USB COMM_REPORT_SIZE

// UUIDs (128-bit, little-endian byte arrays for NimBLE)
static const ble_uuid128_t comm_svc_uuid = BLE_UUID128_INIT(
    0x4D, 0x4D, 0x4F, 0x43, 0x5F, 0x45, 0x54, 0x42,
    0x42, 0x4B, 0x01, 0x00, 0x46, 0x45, 0x54, 0x4D);

static const ble_uuid128_t comm_rx_uuid = BLE_UUID128_INIT(
    0x4D, 0x4D, 0x4F, 0x43, 0x5F, 0x45, 0x54, 0x42,
    0x42, 0x4B, 0x02, 0x00, 0x46, 0x45, 0x54, 0x4D);

static const ble_uuid128_t comm_tx_uuid = BLE_UUID128_INIT(
    0x4D, 0x4D, 0x4F, 0x43, 0x5F, 0x45, 0x54, 0x42,
    0x42, 0x4B, 0x03, 0x00, 0x46, 0x45, 0x54, 0x4D);
```

**Service characteristics:**

| Characteristic | Direction | Properties | Security |
|---------------|-----------|------------|----------|
| COMM RX | Client → Device | WRITE, WRITE_NO_RSP | Encrypted |
| COMM TX | Device → Client | READ, NOTIFY | Encrypted |

The RX access callback receives written data and passes it to `comm_channel_receive_packet(COMM_TRANSPORT_BLE, ...)`.

The TX characteristic stores the latest outgoing packet. When the firmware needs to send data, it calls `ble_gatts_notify_custom()` on the TX handle.

---

### 2.2 BLE COMM RX Path

When the configurator writes to the COMM RX characteristic:

```
[Configurator App]
       │  writeValue(63 bytes) via Web Bluetooth
       ▼
[NimBLE GATT Server]
       │  comm_rx_access_cb()
       ▼
[comm_channel_receive_packet(COMM_TRANSPORT_BLE, packet, 63)]
       │  sets s_reply_transport = COMM_TRANSPORT_BLE
       │  enqueues to usb_processing_queue (shared)
       ▼
[usb_processing_task]
       │  process_incoming_packet() — same as USB
       ▼
[Callback Router]
       │  execute_callback(module, data, len)
       ▼
[Module callback — cfg_usb_callback / status_module_callback / etc.]
```

**Critical:** The NimBLE GATT access callback runs in the NimBLE host task context. We must **not** do heavy processing there. The callback copies the 63-byte packet and enqueues it to the existing `usb_processing_queue` (which, despite its name, is transport-agnostic — it processes `usb_packet_msg_t` structs that are identical for both transports).

---

### 2.3 BLE COMM TX Path

When a module callback calls `send_payload()` and the reply target is BLE:

```
[Module callback]
       │  send_payload(response, len)
       ▼
[comm_channel_send_payload(COMM_TRANSPORT_BLE, ...)]
       │  enqueues to TX queue (shared)
       ▼
[TX task]
       │  builds packet(s), CRC stamps
       │  calls transport_ops->send_packet()
       ▼
[ble_comm_send_packet()]
       │  ble_gatts_notify_custom(conn_handle, tx_handle, om)
       ▼
[NimBLE → Configurator notification]
```

**Key considerations:**
- `ble_gatts_notify_custom()` is non-blocking and copies the data into an mbuf
- The TX task must check that the BLE connection is still active and that the client has subscribed to notifications (CCCD enabled)
- Blast mode (multi-packet) works identically: the TX task fires multiple notifications in sequence, then sends STATUS_REQ and waits for the BITMAP response (which arrives as a WRITE to the RX characteristic)

---

### 2.4 BLE Transport Adapter

#### [NEW] `components/ble_module/ble_comm_transport.c`

```c
static uint16_t s_comm_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_comm_tx_handle = 0;  // Set during GATT registration
static bool     s_comm_subscribed = false;  // CCCD state

static bool ble_comm_send_packet(const uint8_t *packet, uint16_t len) {
    if (s_comm_conn_handle == BLE_HS_CONN_HANDLE_NONE) return false;
    if (!s_comm_subscribed) return false;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(packet, len);
    if (!om) return false;

    int rc = ble_gatts_notify_custom(s_comm_conn_handle, s_comm_tx_handle, om);
    return rc == 0;
}

static bool ble_comm_is_ready(void) {
    return s_comm_conn_handle != BLE_HS_CONN_HANDLE_NONE
        && s_comm_subscribed
        && !ble_hid_is_suspended();
}
```

**Connection handle tracking:** When a BLE central connects and subscribes to COMM TX notifications, the adapter stores its `conn_handle`. If multiple BLE connections exist (the keyboard supports up to 3 simultaneous), only the one that subscribed to COMM TX notifications is the "configurator connection". This naturally prevents conflicts.

---

### 2.5 Integration with blemod.c

**Minimal changes to `blemod.c`:**

1. **In `ble_hid_init()`:** Call `ble_comm_svc_register()` alongside `ble_hid_svc_register()`. The new COMM service is registered in the same GATT server — NimBLE handles multiple services cleanly.

2. **In `ble_hid_gap_event()` → `BLE_GAP_EVENT_CONNECT`:** Notify the COMM transport adapter of the new connection handle.

3. **In `ble_hid_gap_event()` → `BLE_GAP_EVENT_DISCONNECT`:** Notify the COMM transport adapter to clear its connection state.

4. **In `ble_hid_set_suspended()`:** When BLE is suspended (slave role), the COMM transport adapter automatically becomes unavailable via `is_ready() = false`.

**No changes to:**
- Advertising logic (the COMM service UUID is automatically included in the GATT database, discoverable via service discovery after connection)
- Pairing/bonding flow
- Profile management
- HID report delivery path

---

### 2.6 NimBLE Resource Tuning

The COMM service adds GATT attributes that consume NimBLE resources:

| Resource | Current | Required Change | Why |
|----------|---------|-----------------|-----|
| `CONFIG_BT_NIMBLE_MAX_CCCDS` | 15 | → 18 | +1 CCCD for COMM TX notifications, +2 safety margin |
| `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU` | default (256) | → 256 (explicit) | Ensure 63-byte packets fit in a single ATT notification |
| `CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT` | 24 | → 28 | Additional mbufs for COMM traffic alongside HID |

**Memory impact:** ~1 KB additional internal SRAM. Well within budget.

---

### 2.7 Phase 2 Verification

| Check | Method |
|-------|--------|
| COMM service is discoverable | Use nRF Connect app to scan and discover the TEF COMM service |
| COMM RX write works | Write 63-byte test packet from nRF Connect, verify CRC validation and callback routing in logs |
| COMM TX notification works | Trigger a status push, verify notification appears in nRF Connect |
| Full config read over BLE | Connect configurator (Phase 3 preview using manual Web Bluetooth test page), read layout |
| Blast mode over BLE | Read a full layout (multi-packet) over BLE, verify bitmap reconciliation |
| USB COMM still works | Connect USB configurator alongside BLE, verify both paths work |
| HID keyboard over BLE | Verify no regression in keypress delivery |
| Split keyboard | Verify slave suspension disables COMM service correctly |

---

## Phase 3 — Configurator Dual-Transport Support

### Goal
Add Web Bluetooth support to the configurator so it can connect to the keyboard via BLE, using the same UI and protocol as USB.

---

### 3.1 BLETransport.ts (Web Bluetooth Client)

#### [NEW] `configurator/src/services/BLETransport.ts`

This is the Web Bluetooth mirror of `HIDTransport.ts`. It implements the same transport interface but uses `navigator.bluetooth` instead of `navigator.hid`:

```typescript
// Service and characteristic UUIDs (matching firmware)
const COMM_SERVICE_UUID    = '4d544546-0001-4b42-4254-455f434f4d4d';
const COMM_RX_CHAR_UUID    = '4d544546-0002-4b42-4254-455f434f4d4d';
const COMM_TX_CHAR_UUID    = '4d544546-0003-4b42-4254-455f434f4d4d';

export class BLETransport implements ITransport {
    private device: BluetoothDevice | null = null;
    private server: BluetoothRemoteGATTServer | null = null;
    private rxChar: BluetoothRemoteGATTCharacteristic | null = null;
    private txChar: BluetoothRemoteGATTCharacteristic | null = null;

    async connect(): Promise<void> {
        this.device = await navigator.bluetooth.requestDevice({
            filters: [{ services: [COMM_SERVICE_UUID] }],
            // Also show devices advertising HID, in case the COMM service
            // is not in the advertisement (it's discovered via GATT)
            optionalServices: [COMM_SERVICE_UUID],
        });
        this.server = await this.device.gatt!.connect();
        const service = await this.server.getPrimaryService(COMM_SERVICE_UUID);
        this.rxChar = await service.getCharacteristic(COMM_RX_CHAR_UUID);
        this.txChar = await service.getCharacteristic(COMM_TX_CHAR_UUID);

        // Subscribe to notifications (TX: device → configurator)
        await this.txChar.startNotifications();
        this.txChar.addEventListener('characteristicvaluechanged',
            this.onNotification.bind(this));
    }

    async sendPacket(data: Uint8Array): Promise<void> {
        // Use writeValueWithoutResponse for blast mode performance
        await this.rxChar!.writeValueWithoutResponse(data);
    }

    private onNotification(event: Event): void {
        const value = (event.target as BluetoothRemoteGATTCharacteristic).value!;
        const packet = new Uint8Array(value.buffer);
        // Feed into the shared protocol state machine
        this.packetCallback?.(packet);
    }
}
```

**Reconnection:** Web Bluetooth's `device.gatt.connect()` can auto-reconnect. The transport monitors the `gattserverdisconnected` event and attempts reconnection with exponential backoff.

---

### 3.2 Transport Abstraction Layer (TypeScript)

#### [NEW] `configurator/src/services/ITransport.ts`

```typescript
export interface ITransport {
    connect(): Promise<void>;
    disconnect(forceReset?: boolean): Promise<void>;
    isConnected(): boolean;
    getTransportName(): string;  // "USB" or "Bluetooth"

    // Protocol operations (shared between HID and BLE)
    sendCommand(payload: Uint8Array): Promise<CommandResponse | null>;
    onStatusUpdate(callback: StatusUpdateCallback): void;
    offStatusUpdate(callback: StatusUpdateCallback): void;
    onConnectionChange(callback: ConnectionCallback): void;
    offConnectionChange(callback: ConnectionCallback): void;
}
```

#### [MODIFY] `configurator/src/services/HIDTransport.ts`

Refactor to implement `ITransport`. The blast+reconcile protocol code stays in `HIDTransport.ts`. `BLETransport.ts` reimplements the same protocol state machine (it's the same protocol, just different physical I/O).

**Alternative approach (preferred):** Extract the protocol engine into a shared `CommProtocol.ts` class that both transports delegate to. This avoids duplicating the blast+reconcile logic:

```typescript
// CommProtocol.ts — shared protocol state machine
export class CommProtocol {
    constructor(
        private io: {
            sendRaw: (data: Uint8Array) => Promise<void>;
            onRawReceived: (callback: (data: Uint8Array) => void) => void;
        }
    ) {}

    // All blast+reconcile, CRC, task queue logic lives here
    async sendCommand(payload: Uint8Array): Promise<CommandResponse | null> { ... }
}

// HIDTransport.ts
export class HIDTransport implements ITransport {
    private protocol: CommProtocol;
    constructor() {
        this.protocol = new CommProtocol({
            sendRaw: (data) => this.device.sendReport(COMM_REPORT_ID, data),
            onRawReceived: (cb) => this.device.addEventListener('inputreport', ...),
        });
    }
}

// BLETransport.ts
export class BLETransport implements ITransport {
    private protocol: CommProtocol;
    constructor() {
        this.protocol = new CommProtocol({
            sendRaw: (data) => this.rxChar.writeValueWithoutResponse(data),
            onRawReceived: (cb) => this.txChar.addEventListener('characteristicvaluechanged', ...),
        });
    }
}
```

#### [MODIFY] `configurator/src/services/DeviceController.ts`

Change constructor to accept `ITransport` instead of `HIDTransport`:

```typescript
export class DeviceController {
    public readonly transport: ITransport;

    constructor(transport?: ITransport) {
        this.transport = transport || new HIDTransport();
    }
    // ... all command methods unchanged
}
```

---

### 3.3 UI Connection Selector

#### [MODIFY] `configurator/src/App.tsx`

Add a transport selector in the connection flow:

```
┌─────────────────────────────────┐
│  Connect to Keyboard            │
│                                 │
│  ┌───────────┐ ┌──────────────┐ │
│  │   🔌 USB  │ │  📶 Bluetooth │ │
│  └───────────┘ └──────────────┘ │
│                                 │
│  USB requires Chrome desktop.   │
│  Bluetooth works on Android,    │
│  iOS (Bluefy), and desktop.     │
└─────────────────────────────────┘
```

**Behavior:**
- USB button: Creates `HIDTransport`, calls `requestDevice()` (existing flow)
- Bluetooth button: Creates `BLETransport`, calls `connect()` → triggers `navigator.bluetooth.requestDevice()` with COMM service filter
- The selected transport is passed to `DeviceController`
- Once connected, the rest of the UI is identical — it doesn't know or care which transport is active

**Feature detection:**
```typescript
const hasWebHID = 'hid' in navigator;
const hasWebBluetooth = 'bluetooth' in navigator;
```
Only show buttons for available transports.

---

### 3.4 Phase 3 Verification

| Check | Method |
|-------|--------|
| WebHID still works | Connect via USB, full configurator test |
| Web Bluetooth connects | Connect via BLE from Chrome on Android |
| Config read over BLE | Read all layouts, macros, custom keys |
| Config write over BLE | Save a layer change, verify it persists on reboot |
| Blast mode over BLE | Transfer a full layout (~20KB), verify bitmap reconciliation |
| Status push over BLE | Change BLE profile, verify StatusWidget updates |
| iOS via Bluefy | Open configurator in Bluefy browser, connect, read config |
| Disconnect handling | Disconnect BLE mid-transfer, verify clean recovery |
| Concurrent USB + BLE | Connect USB configurator AND BLE configurator simultaneously, verify both work |

---

## Phase 4 — Split Keyboard BLE COMM Proxy

### Goal
Ensure the BLE COMM channel works correctly in split keyboard configurations.

---

### 4.1 Slave-Side BLE COMM Forwarding

**As established in [Decision 7](#decision-7-split-keyboard-implications), no proxy logic is needed.** The BLE COMM service is only active on the master half (the slave's BLE is suspended). This means:

- A BLE configurator always talks directly to the master
- Config writes to the master automatically sync to the slave via the existing `SPLIT_MSG_CONFIG_SYNC` mechanism
- The slave can still be configured via USB (the USB COMM channel is always active on whichever half has the cable)

**What needs verification:**
- When a role swap occurs, the new master's BLE stack reinitializes with `ble_hid_reinit_bonds()`. The COMM service must survive this reinit (NimBLE re-registers all GATT services during `ble_gatts_reset()`).
- The COMM transport adapter must clear its `s_comm_conn_handle` when BLE is suspended (slave becoming master after a swap will start with no COMM connection).

---

### 4.2 Phase 4 Verification

| Check | Method |
|-------|--------|
| BLE COMM on master | Connect BLE configurator to master half, full config test |
| BLE COMM unavailable on slave | Verify slave does not advertise COMM service (BLE suspended) |
| Role swap | Perform role swap, verify new master's BLE COMM is functional |
| Config sync after BLE write | Write a layout via BLE COMM on master, verify slave receives sync |
| USB on slave + BLE on master | Simultaneous configurator sessions via different transports |

---

## Cross-Cutting Concerns

### Thread Safety

| Resource | Current Protection | Change Needed |
|----------|-------------------|---------------|
| `usb_processing_queue` | FreeRTOS queue (thread-safe) | None — both USB and BLE enqueue here |
| TX buffer/queue | FreeRTOS queue + semaphore | Add per-transport TX state (see below) |
| Module callbacks array | Written once at init, read-only after | None |
| `s_reply_transport` | Single-threaded (processing task) | None — set before callback, read by callback |

**Per-transport TX state:** The current TX system uses a single set of buffers (`tx_buf`, blast state, etc.) and a semaphore. With two transports, we need **per-transport TX state** so a USB response doesn't collide with a BLE response in flight.

**Solution:** The TX queue already serializes sends. Since `send_payload()` enqueues and the TX task dequeues one-at-a-time, the existing serialization handles concurrent USB and BLE responses naturally. The `s_reply_transport` variable tells the TX task which transport ops to use for each queued item.

**Enhancement:** Extend `tx_queue_item_t` to include the target transport:

```c
typedef struct {
    uint8_t *data;
    uint16_t len;
    comm_transport_t target;  // NEW: which transport to send on
} tx_queue_item_t;
```

### Memory Budget

| Component | Internal SRAM | PSRAM | Notes |
|-----------|:------------:|:-----:|-------|
| COMM GATT service attributes | ~200 B | — | NimBLE attribute table |
| Additional CCCDs | ~100 B | — | 3 extra CCCD entries |
| MSYS mbufs (4 extra blocks) | ~1 KB | — | For COMM notifications |
| `ble_comm_transport.c` state | ~20 B | — | conn_handle, flags |
| `comm_channel.c` state | ~50 B | — | Transport registry |
| **Total firmware** | **~1.4 KB** | **0** | Well within budget |

The configurator-side changes have no memory impact on the firmware.

### Concurrent USB + BLE COMM Sessions

**Supported.** Two configurator instances (one USB, one BLE) can connect simultaneously. Each request is processed sequentially through the shared `usb_processing_queue`. Responses are routed back to the correct transport via `s_reply_transport`.

**Caveat:** If both configurators try to SET the same config key simultaneously, the last write wins (no locking). This is acceptable because:
1. It's an unlikely scenario (who has two configurators open at once?)
2. The firmware's NVS writes are atomic per key
3. The configurator always re-reads after a SET to confirm

### Error Handling

| Error | USB Behavior (unchanged) | BLE Behavior (new) |
|-------|-------------------------|---------------------|
| CRC mismatch | Silently drop | Silently drop |
| Transport disconnect mid-transfer | TX timeout → erase buffer | TX timeout → erase buffer + clear conn_handle |
| Module callback failure | Send ACK\|ERR | Send ACK\|ERR (via notification) |
| BLE MTU too small | N/A | Application-level fragmentation fallback |
| BLE COMM not subscribed | N/A | `send_packet()` returns false, TX task retries or times out |

---

## Platform Compatibility Matrix

| Platform | Transport | API | Status |
|----------|-----------|-----|--------|
| Windows / macOS / Linux + Chrome | USB | WebHID | ✅ Existing |
| Windows / macOS / Linux + Chrome | BLE | Web Bluetooth | 🆕 Phase 3 |
| Android + Chrome | BLE | Web Bluetooth | 🆕 Phase 3 |
| iOS + Bluefy browser | BLE | Web Bluetooth (bridged) | 🆕 Phase 3 |
| iOS + Safari | — | — | ❌ Not supported (Safari blocks Web Bluetooth) |
| Firefox (any platform) | USB | — | ❌ Not supported (Firefox blocks WebHID and Web Bluetooth) |

> [!NOTE]
> **iOS users** must install the free **Bluefy** browser (or similar WebBLE browser) from the App Store. This is well-documented in the BLE IoT community and is the standard approach for Web Bluetooth on iOS. The configurator URL works identically in Bluefy as in Chrome.

---

## Risk Matrix

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| BLE MTU negotiation fails below 66 bytes | COMM packets don't fit in one notification | Very Low | Implement ATT-level fragmentation fallback |
| NimBLE mbuf exhaustion under heavy COMM + HID traffic | Stack crash or dropped packets | Low | Increased MSYS block counts + throttle COMM during rapid typing |
| Web Bluetooth browser support narrows | Fewer platforms supported | Very Low | WebHID USB path remains primary; BLE is additive |
| GATT service discovery slow on some hosts | Connection setup takes 5–10 seconds | Medium | Acceptable for a configuration tool (not real-time) |
| Two concurrent configurators cause config conflicts | Inconsistent state | Very Low | Last-write-wins is acceptable; add warning in UI |
| NimBLE reinit during role swap loses COMM service state | Configurator must reconnect | Medium | Clear adapter state, configurator auto-reconnects |
| Blast mode over BLE slower than USB | Config transfers take longer | High (expected) | BLE notification interval is ~7.5ms min vs USB's 1ms; blast mode handles this gracefully. Expected throughput: ~4–8 KB/s over BLE vs ~30 KB/s over USB |

---

## File Change Manifest

### Phase 1 — Transport Abstraction

| File | Action | Description |
|------|--------|-------------|
| `components/usb_module/include/comm_channel.h` | NEW | Transport abstraction types and API |
| `components/usb_module/comm_channel.c` | NEW | Transport registry, reply-target tracking |
| `components/usb_module/comm_transport_usb.c` | NEW | USB transport adapter |
| `components/usb_module/usb_send.c` | MODIFY | Extract USB-specific send into adapter |
| `components/usb_module/usb_callbacks.c` | MODIFY | Route incoming packets through `comm_channel_receive_packet()` |
| `components/usb_module/usb_callbacks_tx.c` | MODIFY | Use transport ops for send, add target to queue items |
| `components/usb_module/CMakeLists.txt` | MODIFY | Add new source files |

### Phase 2 — BLE COMM Service

| File | Action | Description |
|------|--------|-------------|
| `components/ble_module/ble_comm_service.c` | NEW | GATT service definition, RX/TX access callbacks |
| `components/ble_module/ble_comm_service.h` | NEW | Public API: register, set conn handle, TX handle getter |
| `components/ble_module/ble_comm_transport.c` | NEW | BLE transport adapter for comm_channel |
| `components/ble_module/ble_comm_transport.h` | NEW | Public API: init, conn handle set/clear |
| `components/ble_module/blemod.c` | MODIFY | Call COMM service init, notify adapter on connect/disconnect |
| `components/ble_module/CMakeLists.txt` | MODIFY | Add new source files, add usb_module dependency |
| `sdkconfig.defaults` | MODIFY | Bump CCCD count, set explicit MTU, bump MSYS blocks |

### Phase 3 — Configurator

| File | Action | Description |
|------|--------|-------------|
| `configurator/src/services/ITransport.ts` | NEW | Transport interface definition |
| `configurator/src/services/CommProtocol.ts` | NEW | Shared blast+reconcile protocol engine |
| `configurator/src/services/BLETransport.ts` | NEW | Web Bluetooth transport implementation |
| `configurator/src/services/HIDTransport.ts` | MODIFY | Refactor to implement ITransport, delegate protocol to CommProtocol |
| `configurator/src/services/DeviceController.ts` | MODIFY | Accept ITransport instead of HIDTransport |
| `configurator/src/App.tsx` | MODIFY | Add transport selector UI |
| `configurator/src/types/protocol.ts` | MODIFY | Add BLE COMM UUIDs |

### Phase 4 — Split Integration

| File | Action | Description |
|------|--------|-------------|
| No new files | — | Phase 4 is verification-only; existing split mechanisms handle it |

### Documentation Updates

| File | Action |
|------|--------|
| `universe/modules/BLE_MODULE.md` | Update with COMM service documentation |
| `universe/modules/USB_MODULE.md` | Update with transport abstraction documentation |
| `universe/modules/CONFIGURATOR.md` | Update with dual-transport support |
| `COMM_PROTOCOL.md` | Add BLE transport section |
| `components/ble_module/BLE_MODULE.md` | Update local module docs |
| `components/usb_module/USB_MODULE.md` | Update local module docs |
