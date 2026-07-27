# BLE COMM Channel — Implementation Plan

> **Goal:** Add a bidirectional, non-HID communication channel over Bluetooth Low Energy so the keyboard can be configured wirelessly from Android, iOS (via Bluefy/WebBLE browsers), and desktop — mirroring the existing USB COMM channel.

---

## Table of Contents

- [Step-by-Step Implementation Task List](#step-by-step-implementation-task-list)
- [Situation Analysis](#situation-analysis)
- [Architectural Strategy](#architectural-strategy)
- [Terminology](#terminology)
- [Key Design Decisions](#key-design-decisions)
- [Phased Rollout Summary](#phased-rollout-summary)
- [Phase 0 — `comm_module` Extraction](#phase-0--comm_module-extraction)
- [Phase 1 — BLE COMM GATT Service](#phase-1--ble-comm-gatt-service)
- [Phase 2 — Configurator Dual-Transport Support](#phase-2--configurator-dual-transport-support)
- [Phase 3 — Split Keyboard BLE COMM Verification](#phase-3--split-keyboard-ble-comm-verification)
- [Cross-Cutting Concerns](#cross-cutting-concerns)
- [Platform Compatibility Matrix](#platform-compatibility-matrix)
- [Risk Matrix](#risk-matrix)
- [File Change Manifest](#file-change-manifest)

---

## Step-by-Step Implementation Task List

### Phase 0: `comm_module` Extraction
- [ ] **Step 1:** Create `components/comm_module/` directory structure and `CMakeLists.txt`.
- [ ] **Step 2:** Migrate `usb_defs.h` to `comm_defs.h` (rename types and add protocol sizing constants).
- [ ] **Step 3:** Migrate `usb_crc.c/h` to `comm_crc.c/h` (update for dynamic length parameter).
- [ ] **Step 4:** Create `comm_transport.h` and `comm_transport.c` for the abstraction interface.
- [ ] **Step 5:** Create `comm_pool.h` and `comm_pool.c` to implement the shared memory pool and dynamic buffer lifecycle management.
- [ ] **Step 6:** Migrate `usb_send.c/h` to `comm_send.c/h` (remove USB hard dependency).
- [ ] **Step 7:** Migrate `usb_callbacks.c/h` to `comm_dispatch.c/h` (routing, queue, task creation, transport disconnect cleanup hooks).
- [ ] **Step 8:** Migrate `usb_callbacks_rx/tx.c` to `comm_rx/tx.c`, replacing static 21.5 KB buffers with dynamic session allocations from `comm_pool`.
- [ ] **Step 9:** Create public API header `comm_module.h`.
- [ ] **Step 10:** Update `usb_module` (`usbmod.c`, `CMakeLists.txt`, `usb_descriptors.h`) to strip old comm logic and register as a transport.
- [ ] **Step 11:** Update all consumers (`cfgmod.c`, `statusmod.c`, `splitmod.c`, `kb_manager.c`, etc.) to use `comm_module.h`.
- [ ] **Step 12:** Update `main.c` init order to call `comm_init()` before `usb_init()`.
- [ ] **Phase 0 Verification:** Clean build, test USB COMM, dynamic buffer allocation/freeing, Blast mode, and split commands without memory leaks.

### Phase 1: BLE COMM GATT Service
- [ ] **Step 1:** Create `components/ble_module/ble_comm_service.c` and `.h` with custom UUIDs.
- [ ] **Step 2:** Create `components/ble_module/ble_comm_transport.c` and `.h` to implement `comm_transport_ops_t`.
- [ ] **Step 3:** Update `blemod.c` to initialize the COMM service and manage connection state.
- [ ] **Step 4:** Update `ble_module/CMakeLists.txt` and `sdkconfig.defaults` (NimBLE resource tuning).
- [ ] **Phase 1 Verification:** Ensure COMM service is discoverable and handles RX writes/TX notifications.

### Phase 2: Configurator Dual-Transport Support
- [ ] **Step 1:** Create `configurator/src/services/ITransport.ts` abstraction.
- [ ] **Step 2:** Create `CommProtocol.ts` to share blast+reconcile protocol engine logic.
- [ ] **Step 3:** Create `BLETransport.ts` implementing Web Bluetooth.
- [ ] **Step 4:** Refactor `HIDTransport.ts` and `DeviceController.ts` to use `ITransport`.
- [ ] **Step 5:** Update `App.tsx` with a transport selector UI (USB vs Bluetooth).
- [ ] **Phase 2 Verification:** Test full configurator functionality via Web Bluetooth on desktop and Android.

### Phase 3: Split Keyboard Verification & Documentation
- [ ] **Step 1:** Verify slave suspension logic correctly disables COMM service.
- [ ] **Step 2:** Perform role swap and ensure new master's BLE COMM is functional.
- [ ] **Step 3:** Update `universe/` documentation, `COMM_PROTOCOL.md`, and local module `.md` files.


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

| Term                      | Meaning                                                                                                                  |
| ---------------------------| --------------------------------------------------------------------------------------------------------------------------|
| **COMM channel**          | The bidirectional data pipe used for configuration (not HID reports). Currently USB-only, will become transport-agnostic |
| **Transport**             | A physical medium carrying COMM packets (USB HID Interface 1, or BLE GATT custom service)                                |
| **BLE COMM Service**      | A new custom GATT service with RX/TX characteristics for configuration data                                              |
| **comm_channel**          | The new firmware abstraction layer that routes COMM traffic regardless of transport                                      |
| **Active COMM transport** | Which transport is currently servicing a COMM session. Can be USB, BLE, or both simultaneously                           |

---

## Key Design Decisions

### Decision 1: Transport Abstraction Layer via `comm_module` Extraction

**Approach:** Extract the entire COMM protocol engine (Blast+Reconcile, CRC, callback registry, processing queue/task, TX queue/task) out of `usb_module` into a new, independent `comm_module` component. Both `usb_module` and `ble_module` then register themselves as transports with `comm_module`, and all consumer modules (`cfgmod`, `statusmod`, `splitmod`, `keyboard`) depend on `comm_module` directly.

> [!IMPORTANT]
> See [Implementation: Decoupling COMM from USB](#implementation-decoupling-comm-from-usb-comm_module-extraction) for the exhaustive, step-by-step plan covering the `comm_module` extraction.



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
            Value: Up to 63 bytes
            Descriptors: CCCD (auto-created by NimBLE when NOTIFY flag is set)

Char 3:     COMM MTU (Read)
            UUID: 4D544546-0004-4B42-4254-455F434F4D4D
            Properties: READ
            Value: 1 byte (Current maximum packet size, e.g., 20 to 63)
```

**Why this design:**
- **63-byte max packets** — The absolute maximum packet size remains 63 bytes to mirror USB constraints cleanly, though BLE packets will be dynamically sized up to this limit to maximize throughput on smaller MTUs.
- **Write Without Response** — Faster than Write With Response for blast mode. The blast+reconcile protocol already handles reliability at the application layer.
- **Two characteristics instead of one** — Separating RX and TX avoids ambiguity about read-back semantics and keeps the CCCD subscription clean.
- **Custom 128-bit UUIDs** — Required to avoid collisions with standard Bluetooth SIG services. The UUIDs are derived from "TEF COMM" for readability in debugging tools.

### Decision 3: Packet Format Reuse

**The core COMM packet structure is reused over BLE.** No changes to:
- Flag byte definitions
- Blast+Reconcile state machines
- Module ID routing
- Application-level payload format

However, to support variable packet lengths (see Decision 4) gracefully, the monolithic `usb_packet_msg_t` struct will be decomposed. Instead of a hardcoded 63-byte struct with padding logic, an incoming packet of length `N` will be logically and physically divided into three parts:

1. **Header**: Defined as `comm_packet_header_t` (4 bytes: `flags`, `remaining_packets`, `payload_len`), located at `(0 .. sizeof(Header) - 1)`.
2. **Payload**: Located at `(sizeof(Header) .. sizeof(Header) + payload_len - 1)`.
3. **CRC**: Located immediately after the payload at index `sizeof(comm_packet_header_t) + header->payload_len`.

This means the minimum theoretical packet size becomes `sizeof(comm_packet_header_t) + 0 + 1` (which is 5 bytes). `comm_dispatch.c` and `comm_crc.c` will treat incoming data as a raw `uint8_t *` array of physical transport length `N`. Because transports like USB rigidly pad packets with zeroes up to 63 bytes, the protocol engine **must not** look at `N - 1` for the CRC. 

Instead, it will cast the first bytes to `comm_packet_header_t`, determine the expected logical length (`sizeof(header) + payload_len + 1`), verify that the physical length `N` is sufficient (`if (N < expected_logical_length) return error;`), validate the CRC at the logical end, and access the payload. Any padding bytes provided by the transport after the CRC are simply ignored. This makes dynamic MTU sizing fundamentally simpler. The configurator's `HIDTransport.ts` protocol logic can be reused with only the physical I/O layer swapped.

### Decision 4: Variable Packet Sizing (MTU Independence)

**Challenge:** BLE's default MTU is 23 bytes (20 bytes usable), but the USB COMM channel uses 63-byte packets. While most modern devices negotiate MTUs > 66 bytes, some older stacks or specific OS versions do not. We do not want to artificially constrain users.

**Solution:** The COMM protocol engine (`comm_module`) will be refactored to support **variable packet sizes**. The protocol structure inherently supports this because the framing overhead is fixed (5 bytes: 1b Flags, 2b Remaining, 1b Payload Len, 1b CRC) and the `Payload Len` defines the valid data within that specific packet.

- `COMM_REPORT_SIZE` (63) becomes `COMM_MAX_PACKET_SIZE` (63).
- Each transport defines its `max_packet_size`. USB always returns 63. BLE returns `min(63, ble_att_mtu(conn) - 3)`.
- The `comm_tx` task dynamically chunks large payloads based on the target transport's `max_packet_size` (Payload per packet = max_packet_size - 5).
- To avoid race conditions with asynchronous MTU negotiation, the `COMM_MTU` characteristic supports `READ` and `NOTIFY`. The firmware pushes a notification when `BLE_GAP_EVENT_MTU` completes. The configurator subscribes to notifications and reads the initial value to determine the negotiated size, chunking its outbound Web Bluetooth writes accordingly.

This allows the protocol to seamlessly scale down to 20-byte packets (15 bytes of payload) on legacy BLE connections, while running at 63 bytes on USB and modern BLE connections, with zero application-layer fragmentation hacks.

**sdkconfig change:** We will still request a large MTU to optimize throughput.
```
CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=256
```

### Decision 5: Configurator Client Strategy

**The Web Configurator will support both WebHID (existing) and Web Bluetooth (new).**

| API           | Used for | Browser Support                         |
| ---------------| ----------| -----------------------------------------|
| WebHID        | USB COMM | Chrome, Edge, Opera (desktop)           |
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

### Decision 8: Variable Buffers & Shared Memory Pool Architecture (`comm_pool`)

**Challenge:**
In the legacy USB-only implementation, `usb_callbacks_rx.c` and `usb_callbacks_tx.c` each allocated a static 21,500-byte BSS array (`rx_buf` and `tx_buf`). This locked up **43 KB of continuous SRAM permanently**, even when the keyboard was idle and disconnected from any configurator. Furthermore, having a single static buffer prevented concurrent communication across multiple transports (e.g., simultaneous USB and BLE configuration sessions or concurrent blast transfers).

**Solution:**
Replace static buffers with an **Exhaustive Shared Memory Pool (`comm_pool`) and Dynamic Per-Session Buffer Allocation**, governed by the following architectural specifications:

1. **Heap-Backed Budget Tracker Strategy (`comm_pool.c/.h`):**
   - **Why Heap-Backed over Static Arena:** When any communication transfer is initiated (whether a layout blast or single-packet exchange), the protocol header of the initial (`FIRST`) packet explicitly specifies `msg.remaining_packets`. Consequently, the exact total buffer requirement is known instantaneously upon session initiation: $\text{total\_required\_size} = (\text{remaining\_packets} + 1) \times \text{max\_payload\_per\_packet}$.
   - Leveraging this deterministic sizing, `comm_pool_alloc(size)` dynamically allocates session buffers from internal RAM via `heap_caps_malloc(size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)` while atomically tracking total active allocations in `s_pool_used_bytes`.
   - **Zero Idle Overhead:** When no configurator is connected or actively transferring data, `s_pool_used_bytes == 0` and **0 bytes of RAM are consumed**, releasing up to 32 KB of internal SRAM for general keyboard operations, complex macro processing, and NimBLE stack buffers.

2. **Memory Pool Ceiling (`CONFIG_COMM_POOL_SIZE`):**
   - The shared budget ceiling is defined by `CONFIG_COMM_POOL_SIZE` in `menuconfig` (defaulting to **32,768 bytes / 32 KB**). This direct Kconfig integration allows custom firmware builds to easily tune pool capacity based on available hardware RAM without adding unnecessary Kconfig boilerplate.
   - This 32 KB ceiling provides an immediate **~11 KB net savings of continuous internal RAM** compared to the legacy 43 KB static arrays, while allowing any combination of dynamic buffer allocations lower than or equal to 32 KB (e.g., two concurrent 16 KB sessions across USB and BLE, or one massive 24 KB full-layout transfer on a single transport).

3. **Non-Preemptive Concurrent Blasts & Out-of-Memory (OOM) Protection:**
   - When multiple communication channels operate concurrently, each transport channel maintains its own independent session struct and dynamically allocated buffer pointer.
   - **First-Come, First-Served Resolution:** If Channel A (e.g., USB) initiates a transfer requiring 24 KB, and Channel B (e.g., BLE) subsequently attempts to initiate a transfer requiring 12 KB (exceeding the remaining 8 KB capacity of the 32 KB pool), `comm_pool_alloc(12288)` returns `NULL`.
   - To preserve ongoing operations without preemption, Channel B's incoming `FIRST` packet is immediately cleanly rejected with an explicit error response (`PAYLOAD_FLAG_ERR` with an OOM reason code), leaving Channel A's active transfer completely undisturbed. The configurator client on Channel B detects the busy/OOM response and automatically retries after a brief delay.

4. **Guaranteed Buffer Release & Unified Watchdog Timeout:**
   - As soon as communication concludes—whether the `LAST` packet is received and executed, an error occurs, or an outgoing transfer completes—the buffer is immediately returned to the pool (`comm_pool_free()`) and the session pointer is nulled out.
   - **Unified Inactivity Timeout Constant (`COMM_TIMEOUT_MS`):** To eliminate timing discrepancies and simplify configuration across all channels, the legacy individual timeouts (`RX_TIMEOUT_MS` in `usb_callbacks_rx.h` and `TX_TIMEOUT_MS` in `usb_callbacks_tx.h`) are unified into a single global protocol constant: `#define COMM_TIMEOUT_MS 1000` (1,000 ms / 1 second) defined in `comm_defs.h`.
   - **Watchdog Inactivity Safeguard (`comm_timeouts_task`):** Every active session buffer records an atomic timestamp on packet arrival. If an over-the-air BLE link drops, a configurator client crashes, or a transfer stalls mid-stream for $> \text{COMM\_TIMEOUT\_MS}$ (1000 ms), the watchdog task automatically aborts the stale session, resets the transport state machine, and calls `comm_pool_free()`, guaranteeing 100% immunity against memory leaks.

---

## Phased Rollout Summary

| Phase | Scope | Risk | Duration Estimate |
|-------|-------|------|-------------------|
| **Phase 0** | `comm_module` extraction + USB transport adapter wiring | Low — pure refactor, zero behavioral change | 2–3 days |
| **Phase 1** | BLE COMM GATT service + BLE transport adapter | Medium — new GATT service, NimBLE integration | 2–3 days |
| **Phase 2** | Configurator Web Bluetooth support + transport abstraction | Medium — new browser API, UI changes | 2–3 days |
| **Phase 3** | Split keyboard BLE COMM verification | Low — mostly verification | 1 day |

**After each phase, the keyboard must still work identically to today.** Phase 0 is a pure refactor with zero behavioral change. Phase 1 adds the BLE GATT service. Phase 2 adds the configurator client-side BLE support. Phase 3 validates split keyboard scenarios.

---

## Phase 0 — `comm_module` Extraction

### Motivation

The COMM protocol engine — packet framing (flags, remaining_packets), Blast+Reconcile state machines, CRC-8 integrity, the processing queue/task, the TX queue/task, timeout watchdog, module callback registry, and `send_payload()` — currently lives inside `components/usb_module/`. This is purely a historical accident: USB was the first (and only) transport.

Adding BLE as a second transport creates an unacceptable dependency:

```
ble_module ──depends-on──► usb_module   ← WRONG
```

The BLE module would need to `#include "usb_callbacks_rx.h"`, `#include "usb_send.h"`, etc., and its `CMakeLists.txt` would `REQUIRES usb_module`. This couples two independent hardware drivers through an implementation detail.

**The correct architecture:**

```
usb_module ──depends-on──► comm_module  ← USB is a transport
ble_module ──depends-on──► comm_module  ← BLE is a transport

cfgmod     ──depends-on──► comm_module  ← registers MODULE_CONFIG callback
statusmod  ──depends-on──► comm_module  ← registers MODULE_STATUS callback
splitmod   ──depends-on──► comm_module  ← registers MODULE_SPLIT / MODULE_BLE callbacks
keyboard   ──depends-on──► comm_module  ← registers MODULE_SYSTEM callback
```

---

### Scope: What Moves, What Stays

The guiding principle is: **anything that doesn't need TinyUSB or NimBLE moves to `comm_module`.** Anything that touches a specific hardware stack stays in its transport module.

#### Files That MOVE to `comm_module` (renamed)

| Current location (`usb_module/`) | New location (`comm_module/`) | Reason |
|---|---|---|
| `usb_defs.h` | `comm_defs.h` | Protocol constants, flags, `comm_packet_msg_t` (renamed from `usb_packet_msg_t`), module IDs, callback typedef — none of this is USB-specific |
| `usb_crc.c` / `usb_crc.h` | `comm_crc.c` / `comm_crc.h` | CRC-8 computation. Currently depends on `usb_descriptors.h` only for `COMM_REPORT_SIZE` — that constant moves to `comm_defs.h` |
| `usb_callbacks.c` / `usb_callbacks.h` | `comm_dispatch.c` / `comm_dispatch.h` | The processing queue, `process_incoming_packet()`, callback registry (`s_module_callbacks[]`, `register_callback()`, `execute_callback()`), the `usb_processing_task`, and the `timeouts_task`. This is the protocol engine core |
| `usb_callbacks_rx.c` / `usb_callbacks_rx.h` | `comm_rx.c` / `comm_rx.h` | RX buffer, blast mode RX state machine, `process_rx_request()`, `erase_rx_buffer()`. Zero USB dependency |
| `usb_callbacks_tx.c` / `usb_callbacks_tx.h` | `comm_tx.c` / `comm_tx.h` | TX buffer, blast mode TX state machine, `send_payload()`, TX queue, TX task. Zero USB dependency |
| `usb_send.c` / `usb_send.h` | `comm_send.c` / `comm_send.h` | `build_send_single_msg_packet()` and `send_single_packet()`. Currently, `send_single_packet()` calls `tud_hid_n_report()` directly — this **hard USB dependency must be replaced** with the transport abstraction (see below) |
| *(New Component File)* | `comm_pool.c` / `comm_pool.h` | Shared memory pool (`comm_pool`) for dynamic RX/TX session buffer allocation, replacing static 21.5 KB BSS arrays |

#### Files That STAY in `usb_module`

| File | Reason |
|---|---|
| `usbmod.c` / `usbmod.h` | TinyUSB driver init, USB task, HID keyboard send functions (`usb_send_keyboard_6kro`, `usb_send_keyboard_nkro`, `usb_send_consumer_report`), USB string descriptor overrides. All TinyUSB-specific |
| `usb_descriptors.h` | USB HID report descriptors, interface enums, device descriptor. Pure USB. The only shared constant (`COMM_REPORT_SIZE`) moves to `comm_defs.h`; `usb_descriptors.h` will `#include "comm_defs.h"` to get it |

---

### Detailed Implementation Steps

#### Step 1 — Create the `comm_module` Component Skeleton

Create `components/comm_module/` with this structure:

```
components/comm_module/
├── CMakeLists.txt
├── include/
│   ├── comm_defs.h          ← protocol types, flags, module IDs
│   ├── comm_crc.h           ← CRC API
│   ├── comm_dispatch.h      ← callback registry + processing queue
│   ├── comm_rx.h            ← RX state machine API
│   ├── comm_tx.h            ← TX state machine API
│   ├── comm_send.h          ← packet send abstraction
│   ├── comm_transport.h     ← transport interface (register/receive)
│   └── comm_pool.h          ← shared memory pool + dynamic buffer allocation
├── comm_crc.c
├── comm_dispatch.c
├── comm_rx.c
├── comm_tx.c
├── comm_send.c
├── comm_transport.c
└── comm_pool.c
```

**`CMakeLists.txt`:**
```cmake
idf_component_register(
    SRCS "comm_crc.c" "comm_dispatch.c" "comm_rx.c" "comm_tx.c"
         "comm_send.c" "comm_transport.c" "comm_pool.c"
    INCLUDE_DIRS "." "include" "../../components/utils"
    REQUIRES freertos esp_timer
)
```

> [!NOTE]
> **No dependency on `esp_tinyusb`, `bt`, or any hardware driver.** The only RTOS-level requirements are FreeRTOS (queues, tasks, semaphores) and `esp_timer` (for timestamp functions). The `utils` include is for `basic_utils.h`.

---

#### Step 2 — Migrate `comm_defs.h` (from `usb_defs.h`)

This is the most critical header — it defines the entire protocol vocabulary.

**Renames:**

| Old name | New name | Notes |
|---|---|---|
| `usb_packet_msg_t` | `comm_packet_header_t` | Decomposed from a 63-byte buffer to just the 4-byte header |
| `usb_msg_module_t` | `comm_module_id_t` | The module ID enum |
| `USB_MODULE_COUNT` | `COMM_MODULE_COUNT` | End sentinel for the enum |
| `usb_data_callback_t` | `comm_data_callback_t` | The callback function pointer typedef. **Must be updated to accept `comm_transport_t source` as its first parameter.** |

**Additions to `comm_defs.h`:**

```c
// Protocol sizing constants
#define COMM_MAX_PACKET_SIZE 63

// Packet header (4 bytes)
typedef struct __attribute__ ((packed)) {
    uint8_t flags;
    uint16_t remaining_packets;
    uint8_t payload_len;
} comm_packet_header_t;
```

All flag `#define`s (`PAYLOAD_FLAG_FIRST`, etc.) move unchanged.

---

#### Step 3 — Migrate `comm_crc.c` (from `usb_crc.c`)

1. Copy `usb_crc.c` → `comm_crc.c`
2. Replace `#include "usb_descriptors.h"` with `#include "comm_defs.h"` (for `COMM_REPORT_SIZE`)
3. Rename functions: `usb_crc_prepare_packet()` → `comm_crc_prepare_packet()`, `usb_crc_verify_packet()` → `comm_crc_verify_packet()`
4. **Update Signatures**: Both CRC functions must now accept a `uint16_t len` parameter alongside the `uint8_t *packet` to compute the CRC at the dynamically correct `len - 1` position, breaking the hardcoded reliance on a 63-byte macro.
5. Header `comm_crc.h` exports the new names

---

#### Step 4 — Create `comm_transport.h/.c` (Transport Interface)

This is the new abstraction that doesn't exist today. It enables plugging in USB and BLE without either knowing about the other.

**`comm_transport.h`:**

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    COMM_TRANSPORT_USB = 0,
    COMM_TRANSPORT_BLE,
    COMM_TRANSPORT_COUNT
} comm_transport_t;

typedef struct {
    /** Send a single packet up to max_packet_size.
     *  The packet is already CRC-stamped. Returns true on success. */
    bool (*send_packet)(const uint8_t *packet, uint16_t len);

    /** Returns true if this transport is physically connected and ready. */
    bool (*is_ready)(void);

    /** Returns the maximum packet size (including 5-byte protocol overhead) 
     *  supported by this transport currently. */
    uint16_t (*get_max_packet_size)(void);
} comm_transport_ops_t;

/** Register a transport's operations. Called once during init. */
void comm_transport_register(comm_transport_t id,
                             const comm_transport_ops_t *ops);

/** Get the operations for a specific transport. Returns NULL if not registered. */
const comm_transport_ops_t *comm_transport_get(comm_transport_t id);

/** Called by a transport driver when it receives a raw packet.
 *  Enqueues the packet along with the source transport ID. */
void comm_transport_receive_packet(comm_transport_t source,
                                   const uint8_t *packet, uint16_t len);

// Note: comm_transport_get_reply_target is completely removed.
// comm_send_payload now requires explicit targeting to eliminate global state.
```

**`comm_transport.c`:**

- Stores `comm_transport_ops_t` in a static array indexed by `comm_transport_t`
- `comm_transport_receive_packet()` creates a `comm_queue_item_t` (defined in `comm_dispatch.h`) by value, guaranteeing safe memory boundaries across tasks:
  ```c
  typedef struct {
      comm_transport_t source;
      uint16_t len;
      uint8_t data[COMM_MAX_PACKET_SIZE]; 
  } comm_queue_item_t;
  ```
  It then enqueues this struct into `comm_dispatch`'s processing queue.
- No global `s_reply_transport` state is maintained. Callbacks are completely stateless.

---

#### Step 5 — Create `comm_pool.h/.c` (Shared Memory Pool & Dynamic Buffer Allocation)

To eliminate the 43 KB static BSS RAM footprint of the legacy implementation and enable concurrent multi-channel operations, implement an exhaustive shared memory pool:

**`comm_pool.h`:**
```c
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifndef CONFIG_COMM_POOL_SIZE
#define CONFIG_COMM_POOL_SIZE 32768 // 32 KB shared memory pool ceiling (configurable via menuconfig)
#endif

/** Initialize the memory pool manager. */
void comm_pool_init(void);

/** Dynamically allocate a buffer from the shared pool. 
 *  Returns NULL if requested size exceeds remaining available pool capacity. */
uint8_t *comm_pool_alloc(size_t size);

/** Dynamically reallocate an existing buffer in the pool to a new size. */
uint8_t *comm_pool_realloc(uint8_t *ptr, size_t new_size);

/** Free a dynamically allocated buffer back to the shared pool. */
void comm_pool_free(uint8_t *ptr);

/** Get current allocated pool usage in bytes. */
size_t comm_pool_get_used_bytes(void);
```

**`comm_pool.c`:**
- Implements a budget-tracked heap allocator over internal SRAM (`heap_caps_malloc(size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)`), maintaining an atomic counter `s_pool_used_bytes`.
- Rejects allocation if `s_pool_used_bytes + size > CONFIG_COMM_POOL_SIZE`, preventing OOM crashes in the general system heap or starvation of Wi-Fi/BLE mbufs.
- When all channels are idle, `s_pool_used_bytes == 0`, making the entire 32 KB available for general keyboard use.

---

#### Step 6 — Migrate `comm_send.c` (from `usb_send.c`)

This is where the **hard USB coupling is broken.**

Current `usb_send.c` has two functions:
1. `build_send_single_msg_packet()` — Builds a `comm_packet_msg_t`, CRC-stamps it, calls `send_single_packet()`. **Transport-agnostic** apart from calling the function below.
2. `send_single_packet()` — Calls `tud_hid_n_report(ITF_NUM_HID_COMM, ...)`. **This is the USB hard dependency.**

**Migration:**

`comm_send.c` replaces the hard-coded TinyUSB call with the transport abstraction:

```c
// comm_send.c

bool comm_send_single_packet(comm_transport_t target, uint8_t *packet, uint16_t packet_len) {
    comm_crc_prepare_packet(packet, packet_len);

    const comm_transport_ops_t *ops = comm_transport_get(target);

    if (!ops || !ops->is_ready || !ops->is_ready()) {
        ESP_LOGW(TAG, "Transport %d not ready", target);
        return false;
    }

    return ops->send_packet(packet, packet_len);
}
```

**Thread-Safety Rationale:**
`comm_send_single_packet` explicitly accepts the `target` transport as a parameter. We **do not** use an implicit `comm_transport_get_reply_target()` getter inside the single-packet send function because protocol engine timeouts (`comm_timeouts_task`) run asynchronously. If a blast times out, `comm_rx` must explicitly pass its cached `active_rx_transport` to ensure the asynchronous `STATUS_REQ` or `ERR` packet routes to the correct transport, bypassing the state of the synchronous processing task.

`comm_build_send_single_packet()` must also be updated to accept the `target` parameter and pass it down.

---

#### Step 7 — Migrate `comm_dispatch.c` (from `usb_callbacks.c`)

**What moves:**
- `usb_processing_queue` → `s_comm_processing_queue` (Now holds `comm_queue_item_t` instead of raw packets)
- `process_incoming_packet()` — The flag router (RX vs TX, blast reconcile, etc.)
- `usb_processing_task()` → `comm_processing_task()` (Extracts the `source` transport from the dequeued `comm_queue_item_t` and passes it explicitly into the callback router)
- `timeouts_task()` → `comm_timeouts_task()`
- `s_module_callbacks[]` array, `register_callback()`, `execute_callback()`
- `usb_callbacks_init()` → `comm_dispatch_init()` — creates the queue and spawns both tasks. (Also, move the misplaced `#include <freertos/...>` directives from inside the function body to the top of the file).

**What does NOT move:**
- `usbmod_tud_hid_set_report_cb()` — This is the TinyUSB entry point for receiving USB packets. It stays in `usb_module`, but instead of directly enqueuing to the processing queue, it calls `comm_transport_receive_packet(COMM_TRANSPORT_USB, packet, len)`.
- `usbmod_tud_hid_get_report_cb()` — Stays in `usb_module`.
- `tud_hid_descriptor_report_cb()` — Stays in `usb_module`.

**The TinyUSB-specific packet ingestion in `usbmod_tud_hid_set_report_cb()`** (currently in `usb_callbacks.c`, lines 45–116) stays in `usb_module` but is drastically simplified to act purely as a transport adapter. It handles:
1. Keyboard LED reports → `kb_state_update_leds()` (keyboard-specific, stays)
2. COMM interface packets → strips report ID byte, validates length, then directly calls `comm_transport_receive_packet(COMM_TRANSPORT_USB, payload, payload_len)`.

**Protocol Logic Migration & Parameter Chaining:**
Because `comm_send_single_packet` now requires a `target` transport, synchronous error responses must be explicitly chained. When `process_incoming_packet()` handles a packet, it must pass the `target` transport down to `process_rx_request(target, msg)` in `comm_rx.c`. This ensures that if the RX buffer appends fail, `process_rx_request` can correctly call `comm_build_send_single_packet(target, PAYLOAD_FLAG_ABORT, ...)` rather than relying on a global variable. This explicit parameter chaining keeps the asynchronous timeout paths robust and bug-free.

The CRC validation is moved entirely into `comm_dispatch.c` (`process_incoming_packet()`). This ensures the integrity check is centralized. To preserve Blast+Reconcile state synchronization, if `process_incoming_packet()` detects a CRC failure on a packet during blast mode, it must simply drop the packet silently (returning immediately). Because the packet is dropped, its bit will never be set in the RX bitmap (`comm_rx.c`), and the configurator will naturally resend it during reconciliation. This perfectly mimics the existing robust behavior found in `usb_callbacks.c`.

**Cleanup:** `usb_callbacks.h` will be stripped of everything except the TinyUSB-specific callbacks:

```c
#pragma once
// TinyUSB callbacks remain here — they're USB-specific
uint16_t usbmod_tud_hid_get_report_cb(...);
void usbmod_tud_hid_set_report_cb(...);
```

---

#### Step 8 — Migrate `comm_rx.c` and `comm_tx.c` with Dynamic Buffer Management

In addition to updating internal includes (`comm_defs.h`, `comm_send.h`, `comm_crc.h`, `comm_pool.h`), **static BSS buffers are completely replaced with dynamic per-session pool allocations**:

**`comm_rx.c`** (from `usb_callbacks_rx.c`):
- **Remove static buffer:** Delete `static uint8_t rx_buf[21500];`.
- **Session state structure:** Create `comm_rx_session_t` per transport channel containing a dynamic pointer `uint8_t *buf` (allocated via `comm_pool_alloc`), `buf_len`, `buf_capacity`, `last_activity_us`, and blast mode bitmap state.
- **Dynamic buffer lifecycle:**
  - When an RX transaction starts (`FIRST` packet or single packet), calculate required capacity (`(msg.remaining_packets + 1) * msg.payload_len`) and allocate `session->buf = comm_pool_alloc(required_cap)`. If `comm_pool_alloc` returns NULL (pool exhausted), respond immediately with `PAYLOAD_FLAG_ERR` (OOM) and abort without touching other active channels.
  - When `LAST` packet arrives, assemble payload and execute callback.
  - **Zero-leak guaranteed cleanup:** In `process_rx_buffer()` (and on any abort, CRC error, or watchdog timeout), execute `comm_pool_free(session->buf); session->buf = NULL; session->buf_len = 0;` in a guaranteed cleanup path.

**`comm_tx.c`** (from `usb_callbacks_tx.c`):
- **Remove static buffer:** Delete `static uint8_t tx_buf[21500];`.
- **Dynamic TX staging:** In `comm_send_payload(target, payload, len)`, dynamically allocate `uint8_t *tx_data = comm_pool_alloc(len)` and copy the payload into it. Attach `tx_data` to `tx_queue_item_t` and push to the FreeRTOS TX queue.
- **Queue ownership & cleanup:** Once the TX task completes transmission of the item (or upon transmission timeout/disconnect), it calls `comm_pool_free(item.data)`. If queue push fails (`xQueueSend` error), `comm_send_payload()` immediately frees the buffer to prevent memory leaks.
- **Unified inactivity timeout:** Both `comm_rx.c` and `comm_tx.c` replace their legacy 1000 ms timeout constants with `COMM_TIMEOUT_MS` (defined in `comm_defs.h`).

---

#### Step 9 — Public API Header (`comm_module.h`)

A clean, single-entry-point header that consumers include:

```c
// comm_module.h — public API for the comm protocol engine
#pragma once

#include "comm_defs.h"
#include "comm_transport.h"

/** Initialize the comm protocol engine (queues, tasks, timeouts). */
void comm_init(void);

/** Register a module callback for incoming COMM data. */
void comm_register_callback(comm_module_id_t module, comm_data_callback_t cb);

/** Send a payload to the configurator via a specific transport.
 *  This is the primary API for module callbacks to respond to requests.
 *  Callbacks receive the 'source' transport and must pass it back here as the 'target'. */
bool comm_send_payload(comm_transport_t target, const uint8_t *payload, uint16_t payload_len);
```

---

#### Step 10 — Update `usb_module` (Slim Down)

After extraction, `usb_module` contains only:

| File | Contents |
|---|---|
| `usbmod.c` | TinyUSB init, USB task, HID keyboard functions, TinyUSB callback trampolines, USB event handler (mounted/unmounted), USB-specific COMM packet ingestion |
| `usbmod.h` | Public API: `usb_init()`, `usb_send_keyboard_6kro()`, etc. No more `usbmod_register_callback()` — replaced by `comm_register_callback()` |
| `usb_descriptors.h` | Device/config/report descriptors. `#include "comm_defs.h"` for `COMM_REPORT_SIZE` |
| `usb_callbacks.h` | TinyUSB callback declarations only |
*(All other COMM-related headers in usb_module are deleted)*

**Updated `usb_module/CMakeLists.txt`:**
```cmake
idf_component_register(
    SRCS "usbmod.c"
    INCLUDE_DIRS "." "include" "../../components/utils"
    REQUIRES esp_tinyusb driver freertos keyboard comm_module
)
```

Key change: `REQUIRES comm_module` (instead of building the COMM code itself).

**USB transport registration** (added to `usb_init()` in `usbmod.c`):

```c
#include "comm_transport.h"

static bool usb_comm_send_packet(const uint8_t *packet, uint16_t len) {
    // Wait until COMM HID endpoint is ready (max 100ms timeout) to prevent random drops
    uint32_t wait_timeout_ticks = pdMS_TO_TICKS(100);
    if (wait_timeout_ticks == 0) wait_timeout_ticks = 1;
    TickType_t start_tick = xTaskGetTickCount();
    
    while (!tud_hid_n_ready(ITF_NUM_HID_COMM)) {
        if (xTaskGetTickCount() - start_tick > wait_timeout_ticks) {
            ESP_LOGW("USB", "COMM HID not ready (timeout)");
            return false;
        }
        vTaskDelay(1);
    }

    // USB HID requires exactly 63 bytes (COMM_MAX_PACKET_SIZE) per report.
    // If the transport engine generated a dynamically shorter packet, pad it.
    if (len < 63) {
        uint8_t padded_packet[63] = {0};
        memcpy(padded_packet, packet, len);
        return tud_hid_n_report(ITF_NUM_HID_COMM, REPORT_ID_COMM, padded_packet, 63);
    }

    return tud_hid_n_report(ITF_NUM_HID_COMM, REPORT_ID_COMM, packet, len);
}

static bool usb_comm_is_ready(void) {
    return tud_mounted() && tud_hid_n_ready(ITF_NUM_HID_COMM);
}

static const comm_transport_ops_t s_usb_transport_ops = {
    .send_packet = usb_comm_send_packet,
    .is_ready    = usb_comm_is_ready,
};

void usb_init() {
    // ... existing TinyUSB init ...

    // Register USB as a COMM transport
    comm_transport_register(COMM_TRANSPORT_USB, &s_usb_transport_ops);

    // ... existing task creation ...
}
```

---

#### Step 11 — Update All Consumers

Each consumer needs two changes:
1. **Include:** `usbmod.h` → `comm_module.h` (for callback registration and `send_payload`)
2. **CMake:** `REQUIRES usb_module` → `REQUIRES comm_module` (only if they were depending on it for COMM, not for HID keyboard sends)

**Consumer-by-consumer migration:**

| Module | Currently uses from `usb_module` | Change |
|---|---|---|
| `config_module/cfgmod.c` | `usbmod_register_callback(MODULE_CONFIG, ...)` + `send_payload()` via `usb_send.h` | `#include "comm_module.h"` → `comm_register_callback()` + `comm_send_payload()`. CMake: swap `usb_module` → `comm_module` |
| `status_module/statusmod.c` | `usbmod_register_callback(MODULE_STATUS, ...)` + `send_payload()` via `usb_send.h` / `usb_callbacks_tx.h` | Same pattern. CMake: swap `usb_module` → `comm_module` |
| `split/splitmod.c` | `usbmod_register_callback(MODULE_SPLIT, ...)` + `usbmod_register_callback(MODULE_BLE, ...)` | Same pattern. CMake: add `comm_module` alongside existing |
| `split/split_usb.c` | `send_payload()` via `usb_send.h` + `usbmod.h` | `#include "comm_module.h"` → `comm_send_payload()` |
| `keyboard/kb_manager.c` | `usbmod_register_callback(MODULE_SYSTEM, ...)` + HID keyboard functions | Split: `comm_module.h` for callback registration, keep `usbmod.h` for `usb_send_keyboard_*()` and `usb_keyboard_use_boot_protocol()`. CMake: add `comm_module` |
| `keyboard/kb_report.c` | `usb_send_keyboard_6kro()`, `usb_send_keyboard_nkro()`, `usb_send_consumer_report()`, `usb_keyboard_use_boot_protocol()` | **No change** — these are USB HID functions, not COMM. Stays on `usbmod.h` |
| `split/split_task.c` | Includes `usbmod.h` | Check what it uses — likely HID or mounted state. May need no change |
| `split/split_dispatch.c` | Includes `usbmod.h` | Same investigation needed |

> [!IMPORTANT]
> **Clean Refactor**: As there are no backward compatibility constraints, the includes in all consumer files (`cfgmod.c`, `statusmod.c`, `splitmod.c`, `split_usb.c`, `kb_manager.c`, etc.) must be directly updated to `#include "comm_module.h"` during Phase 0. No legacy shim headers will be retained in `usb_module`. Note that unsolicited pushes (e.g., in `statusmod.c`) are being removed; the configurator will explicitly poll for status updates instead.

---

#### Step 12 — Update `main.c` Init Order

```c
// main/main.c
#include "comm_module.h"  // NEW

static void init_procedure(void) {
    event_bus_init();
    button_init(*single_press_test, *double_press_test);
    cfg_init();
    rgb_init(GPIO_NUM_48);

    comm_init();      // NEW: start protocol engine (queues, tasks)
    usb_init();       // Existing: TinyUSB driver + registers USB transport
    ble_hid_init();   // Existing: NimBLE + (later) registers BLE transport

    ble_controller_init();
    status_module_init();
    splitmod_init();
    kb_manager_start();
}
```

`comm_init()` must run **before** `usb_init()` because `usb_init()` calls `comm_transport_register()` and the transport registry must already exist. The processing queue/task and TX queue/task are also started here.

> [!NOTE]
> `cfg_init()` and other modules may safely call `comm_register_callback()` **before** `comm_init()` executes. The callback registry is backed by a static array (guaranteed zero-initialized by the C runtime), so assigning function pointers is 100% thread-safe before any FreeRTOS queues or tasks are created. This ensures modules can load configs that `comm_init()` might need.

---

### Naming Convention Summary

| Old (USB-centric) | New (transport-agnostic) |
|---|---|
| `usb_packet_msg_t` | `comm_packet_msg_t` |
| `usb_msg_module_t` | `comm_module_id_t` |
| `usb_data_callback_t` | `comm_data_callback_t` |
| `USB_MODULE_COUNT` | `COMM_MODULE_COUNT` |
| `RX_TIMEOUT_MS` / `TX_TIMEOUT_MS` (`1000`) | `COMM_TIMEOUT_MS` (`1000`) |
| `usb_crc_prepare_packet()` | `comm_crc_prepare_packet()` |
| `usb_crc_verify_packet()` | `comm_crc_verify_packet()` |
| `usbmod_register_callback()` | `comm_register_callback()` |
| `usbmod_execute_callback()` | `comm_execute_callback()` |
| `usb_callbacks_init()` | `comm_dispatch_init()` |
| `usb_tx_init()` | `comm_tx_init()` |
| `usb_processing_task` | `comm_processing_task` |
| `usb_cb_timeouts_task` | `comm_timeouts_task` |
| `send_payload()` | `comm_send_payload()` |
| `send_single_packet()` | `comm_send_single_packet()` |
| `build_send_single_msg_packet()` | `comm_build_send_single_msg_packet()` |
| `static uint8_t rx_buf[21500]` | `comm_pool_alloc(size)` / `comm_pool_free(ptr)` |
| `static uint8_t tx_buf[21500]` | `comm_pool_alloc(size)` / `comm_pool_free(ptr)` |

---

### Verification Checklist

| # | Check | Method |
|---|-------|--------|
| 1 | Clean build | `idf.py build` with zero warnings or errors |
| 2 | USB COMM still works | Connect configurator, read/write all config types |
| 3 | Blast mode (USB) | Transfer a full layout (~20KB), verify bitmap reconciliation in logs |
| 4 | Status push (USB) | Change BLE profile, verify push arrives in configurator |
| 5 | Split commands (USB) | Pair/unpair split, read remote matrix, role swap |
| 6 | BLE HID unaffected | Connect BLE keyboard, type, switch profiles |
| 7 | Split keyboard | Full split test (pair, type, sync, swap roles) |
| 8 | No USB regression | Hot-plug USB cable, verify mount/unmount events and buffer cleanup |
| 9 | Shim headers work | Temporarily revert one consumer to old `#include`s — must still compile |

---

### Risk Assessment

| Risk | Impact | Probability | Mitigation |
|---|---|---|---|
| Include path breakage after move | Build failure | Medium | Backward-compat shims ensure old includes still resolve. Run `idf.py build` after every file move |
| Thread safety during transport switch | Race condition between reply-target and TX task | Low | `s_reply_transport` is only written by the processing task (single consumer of the queue), and only read by the TX task. The queue serializes all access |
| Circular dependency between `comm_module` and consumers | Build failure | Very Low | `comm_module` has zero knowledge of its consumers. It only exposes registration APIs. Consumers depend on `comm_module`, never the reverse |
| TX task references stale transport ops | Crash | Very Low | Transport ops are registered once during init and never change. The static array is immutable after boot |

---

### File Change Manifest (comm_module Extraction)

#### New Files

| File | Description |
|---|---|
| `components/comm_module/CMakeLists.txt` | Build config — REQUIRES only freertos, esp_timer |
| `components/comm_module/include/comm_defs.h` | Protocol types, flags, module IDs, packet struct |
| `components/comm_module/include/comm_crc.h` | CRC-8 API |
| `components/comm_module/include/comm_dispatch.h` | Processing queue, callback registry API |
| `components/comm_module/include/comm_rx.h` | RX state machine API |
| `components/comm_module/include/comm_tx.h` | TX state machine API |
| `components/comm_module/include/comm_send.h` | Packet send abstraction |
| `components/comm_module/include/comm_transport.h` | Transport interface |
| `components/comm_module/include/comm_module.h` | Unified public API header |
| `components/comm_module/comm_crc.c` | CRC-8 implementation |
| `components/comm_module/comm_dispatch.c` | Processing task, callback dispatch, timeout task |
| `components/comm_module/comm_rx.c` | RX buffer, blast RX state machine |
| `components/comm_module/comm_tx.c` | TX buffer, blast TX state machine, send_payload |
| `components/comm_module/comm_send.c` | Transport-routed packet send |
| `components/comm_module/comm_transport.c` | Transport registry, reply-target tracking |
| `components/comm_module/comm_pool.c` | Shared memory pool implementation with budget tracking |

#### Modified Files

| File | Change |
|---|---|
| `components/usb_module/CMakeLists.txt` | Remove COMM source files, add `REQUIRES comm_module` |
| `components/usb_module/usbmod.c` | Remove COMM init, add USB transport registration. Keep TinyUSB-specific COMM packet ingestion |
| `components/usb_module/include/usb_defs.h` | DELETED |
| `components/usb_module/include/usb_crc.h` | DELETED |
| `components/usb_module/include/usb_send.h` | DELETED |
| `components/usb_module/include/usb_callbacks.h` | Keep only TinyUSB callback declarations |
| `components/usb_module/include/usb_callbacks_rx.h` | DELETED |
| `components/usb_module/include/usb_callbacks_tx.h` | DELETED |
| `components/config_module/cfgmod.c` | `usbmod_register_callback` → `comm_register_callback` |
| `components/config_module/CMakeLists.txt` | `usb_module` → `comm_module` in REQUIRES |
| `components/status_module/statusmod.c` | `usbmod_register_callback` → `comm_register_callback` |
| `components/status_module/CMakeLists.txt` | `usb_module` → `comm_module` in REQUIRES |
| `components/split/splitmod.c` | `usbmod_register_callback` → `comm_register_callback` |
| `components/split/split_usb.c` | `usbmod.h` → `comm_module.h`, `usb_send.h` → `comm_send.h` |
| `components/split/CMakeLists.txt` | Add `comm_module` to REQUIRES |
| `components/keyboard/kb_manager.c` | `usbmod_register_callback` → `comm_register_callback` |
| `components/keyboard/CMakeLists.txt` | Add `comm_module` to REQUIRES |
| `main/main.c` | Add `comm_init()` call before `usb_init()` |

#### Deleted Files (after consumer migration is complete)

| File | Notes |
|---|---|
| `components/usb_module/usb_callbacks.c` | Logic moved to `comm_dispatch.c` + USB-specific packet ingestion inlined into `usbmod.c` |
| `components/usb_module/usb_callbacks_rx.c` | Logic moved to `comm_rx.c` |
| `components/usb_module/usb_callbacks_tx.c` | Logic moved to `comm_tx.c` |
| `components/usb_module/usb_crc.c` | Logic moved to `comm_crc.c` |
| `components/usb_module/usb_send.c` | Logic moved to `comm_send.c` |

---

### Phase 0 Completion Checklist

Once all steps above are completed, the USB transport adapter is wired (via `comm_transport_register()` in `usb_init()`), `main.c` calls `comm_init()` before `usb_init()`, and the old `.c` files are deleted from `usb_module/` (only shim headers remain).

| # | Check | Method |
|---|-------|--------|
| 1 | Clean build | `idf.py build` with zero warnings or errors |
| 2 | USB COMM still works | Connect configurator, read/write all config types |
| 3 | Blast mode (USB) | Transfer a full layout (~20KB), verify bitmap reconciliation in logs |
| 4 | Status push (USB) | Change BLE profile, verify push arrives in configurator |
| 5 | Split commands (USB) | Pair/unpair split, read remote matrix, role swap |
| 6 | BLE HID unaffected | Connect BLE keyboard, type, switch profiles |
| 7 | Split keyboard | Full split test (pair, type, sync, swap roles) |
| 8 | No USB regression | Hot-plug USB cable, verify mount/unmount events and buffer cleanup |
| 9 | Shim headers work | Temporarily revert one consumer to old `#include`s — must still compile |

---

## Phase 1 — BLE COMM GATT Service

### Goal
Add a custom GATT service to the BLE stack that provides a bidirectional 63-byte data channel, and wire it into the `comm_module` transport abstraction.

---

### 1.1 Custom GATT Service Definition

#### [NEW] `components/ble_module/ble_comm_service.c`

This file defines the GATT service table for the COMM channel. It follows the same pattern as `ble_hid_service.c`:

```c
#include "comm_defs.h"

/* TEF COMM Service — Custom vendor service for keyboard configuration.
 *
 * Service UUID:  4D544546-0001-4B42-4254-455F434F4D4D
 * RX Char UUID:  4D544546-0002-4B42-4254-455F434F4D4D  (WRITE | WRITE_NO_RSP)
 * TX Char UUID:  4D544546-0003-4B42-4254-455F434F4D4D  (READ | NOTIFY)
 */

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

static const ble_uuid128_t comm_mtu_uuid = BLE_UUID128_INIT(
    0x4D, 0x4D, 0x4F, 0x43, 0x5F, 0x45, 0x54, 0x42,
    0x42, 0x4B, 0x04, 0x00, 0x46, 0x45, 0x54, 0x4D);
```

**Service characteristics:**

| Characteristic | Direction | Properties | Security |
|---------------|-----------|------------|----------|
| COMM RX | Client → Device | WRITE, WRITE_NO_RSP | Encrypted |
| COMM TX | Device → Client | READ, NOTIFY | Encrypted |
| COMM MTU | Device → Client | READ, NOTIFY | Encrypted |

The RX access callback receives written data and passes it to `comm_channel_receive_packet(COMM_TRANSPORT_BLE, ...)`.

The MTU read access callback returns `ble_comm_get_max_packet_size()`. When `blemod.c` processes a `BLE_GAP_EVENT_MTU` event, it must trigger the transport adapter to notify subscribed clients of the new MTU value via this characteristic.

The TX characteristic stores the latest outgoing packet. When the firmware needs to send data, it calls `ble_gatts_notify_custom()` on the TX handle.

---

### 1.2 BLE COMM RX Path

When the configurator writes to the COMM RX characteristic:

```
[Configurator App]
       │  writeValue(63 bytes) via Web Bluetooth
       ▼
[NimBLE GATT Server]
       │  comm_rx_access_cb()
       ▼
[comm_transport_receive_packet(COMM_TRANSPORT_BLE, packet, 63)]
       │  enqueues item with source=COMM_TRANSPORT_BLE to comm_processing_queue
       ▼
[comm_processing_task]
       │  dequeues item, sets s_current_reply_target = item.source
       │  process_incoming_packet() — same as USB
       ▼
[Callback Router]
       │  execute_callback(module, data, len)
       ▼
[Module callback — cfg_usb_callback / status_module_callback / etc.]
```

**Critical:** The NimBLE GATT access callback runs in the NimBLE host task context. We must **not** do heavy processing there. The callback copies the 63-byte packet and enqueues it to the existing `usb_processing_queue` (which, despite its name, is transport-agnostic — it processes `usb_packet_msg_t` structs that are identical for both transports).

---

### 1.3 BLE COMM TX Path

When a module callback calls `send_payload()` and the reply target is BLE:

```
[Module callback]
       │  send_payload(response, len)
       ▼
[comm_send_payload_to_target(COMM_TRANSPORT_BLE, ...)] /* conceptual routing */
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

### 1.4 BLE Transport Adapter

#### [NEW] `components/ble_module/ble_comm_transport.c`

```c
static uint16_t s_comm_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_comm_tx_handle = 0;
static bool     s_comm_subscribed = false;  // CCCD state

void ble_comm_set_tx_handle(uint16_t handle) {
    s_comm_tx_handle = handle;
}

static bool ble_comm_send_packet(const uint8_t *packet, uint16_t len) {
    if (s_comm_conn_handle == BLE_HS_CONN_HANDLE_NONE) return false;
    if (!s_comm_subscribed) return false;

    // To prevent COMM from starving HID during blast TX, attempt allocation
    // and wait/retry if the mbuf pool is exhausted.
    uint32_t wait_timeout_ticks = pdMS_TO_TICKS(250);
    if (wait_timeout_ticks == 0) wait_timeout_ticks = 1;
    TickType_t start_tick = xTaskGetTickCount();

    struct os_mbuf *om = NULL;
    while (om == NULL) {
        om = ble_hs_mbuf_from_flat(packet, len);
        if (om) break;
        
        if (xTaskGetTickCount() - start_tick > wait_timeout_ticks) {
            ESP_LOGW("BLE_COMM", "mbuf pool starved, timeout");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(5)); // Safe to block here (called from comm_tx task, not NimBLE host)
    }

    // Note: ble_gatts_notify_custom inherently consumes the mbuf regardless
    // of success or failure. Do NOT call os_mbuf_free_chain() here to prevent double-free corruption.
    int rc = ble_gatts_notify_custom(s_comm_conn_handle, s_comm_tx_handle, om);
    return rc == 0;
}

static bool ble_comm_is_ready(void) {
    return s_comm_conn_handle != BLE_HS_CONN_HANDLE_NONE
        && s_comm_subscribed
        && !ble_hid_is_suspended();
}

static uint16_t ble_comm_get_max_packet_size(void) {
    // Only verify we have an active connection; do not require CCCD 
    // subscription, because the Configurator reads MTU *before* subscribing.
    if (s_comm_conn_handle == BLE_HS_CONN_HANDLE_NONE) return 63; // Default fallback
    // MTU minus 3 bytes for ATT write/notify overhead
    uint16_t max_size = ble_att_mtu(s_comm_conn_handle) - 3;
    return max_size > 63 ? 63 : max_size;
}
```

**Connection handle tracking:** When a BLE central connects, `s_comm_conn_handle` is stored. When the client subscribes to COMM TX notifications, `s_comm_subscribed` is set to true. If multiple BLE connections exist (the keyboard supports up to 3 simultaneous), only the one that subscribed to COMM TX notifications is the "configurator connection". This naturally prevents conflicts.

---

### 1.5 Integration with blemod.c

**Minimal changes to `blemod.c`:**

1. **In `ble_hid_init()`:** Call `ble_comm_svc_register()` alongside `ble_hid_svc_register()`. The new COMM service is registered in the same GATT server — NimBLE handles multiple services cleanly.

2. **In `ble_hid_gap_event()` → `BLE_GAP_EVENT_CONNECT`:** Notify the COMM transport adapter of the new connection handle.

3. **In `ble_hid_gap_event()` → `BLE_GAP_EVENT_DISCONNECT`:** Notify the COMM transport adapter to clear its connection state.

4. **In `ble_hid_set_suspended()`:** When BLE is suspended (slave role), `blemod` must explicitly call `ble_comm_reset_state()` (exposed by `ble_comm_transport.h`). This safely clears `s_comm_conn_handle` and subscription flags, ensuring the adapter correctly reports `is_ready() = false` without leaking state.

**No changes to:**
- Advertising logic (the COMM service UUID is automatically included in the GATT database, discoverable via service discovery after connection)
- Pairing/bonding flow
- Profile management
- HID report delivery path

---

### 1.6 NimBLE Resource Tuning

The COMM service adds GATT attributes that consume NimBLE resources:

| Resource | Current | Required Change | Why |
|----------|---------|-----------------|-----|
| `CONFIG_BT_NIMBLE_MAX_CCCDS` | 15 | → 18 | +3 CCCDs for COMM TX notifications (1 per possible connection, as `MAX_CONNECTIONS` = 3) |
| `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU` | default (256) | → 256 (explicit) | Ensure 63-byte packets fit in a single ATT notification |
| `CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT` | 24 | → 28 | Additional mbufs for COMM traffic alongside HID |

**Memory impact:** ~1 KB additional internal SRAM. Well within budget.

---

### 1.7 Phase 1 Verification

| Check | Method |
|-------|--------|
| COMM service is discoverable | Use nRF Connect app to scan and discover the TEF COMM service |
| COMM RX write works | Write 63-byte test packet from nRF Connect, verify CRC validation and callback routing in logs |
| COMM TX notification works | Trigger a status push, verify notification appears in nRF Connect |
| Full config read over BLE | Connect configurator (Phase 2 preview using manual Web Bluetooth test page), read layout |
| Blast mode over BLE | Read a full layout (multi-packet) over BLE, verify bitmap reconciliation |
| USB COMM still works | Connect USB configurator alongside BLE, verify both paths work |
| HID keyboard over BLE | Verify no regression in keypress delivery |
| Split keyboard | Verify slave suspension disables COMM service correctly |

---

## Phase 2 — Configurator Dual-Transport Support

### Goal
Add Web Bluetooth support to the configurator so it can connect to the keyboard via BLE, using the same UI and protocol as USB.

---

### 2.1 BLETransport.ts (Web Bluetooth Client)

#### [NEW] `configurator/src/services/BLETransport.ts`

This is the Web Bluetooth mirror of `HIDTransport.ts`. It implements the same transport interface but uses `navigator.bluetooth` instead of `navigator.hid`:

```typescript
// Service and characteristic UUIDs (matching firmware)
const COMM_SERVICE_UUID    = '4d544546-0001-4b42-4254-455f434f4d4d';
const COMM_RX_CHAR_UUID    = '4d544546-0002-4b42-4254-455f434f4d4d';
const COMM_TX_CHAR_UUID    = '4d544546-0003-4b42-4254-455f434f4d4d';
const COMM_MTU_CHAR_UUID   = '4d544546-0004-4b42-4254-455f434f4d4d';

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
        this.txChar.addEventListener('characteristicvaluechanged', this.onNotification.bind(this));

        // Subscribe to MTU updates (Handle async BLE_GAP_EVENT_MTU race condition)
        const mtuChar = await service.getCharacteristic(COMM_MTU_CHAR_UUID);
        await mtuChar.startNotifications();
        mtuChar.addEventListener('characteristicvaluechanged', (event) => {
            const val = (event.target as BluetoothRemoteGATTCharacteristic).value!;
            this.protocol.setMaxPacketSize(val.getUint8(0));
        });
        
        // Read the initial MTU *after* subscribing to guarantee no updates are missed
        const mtuVal = await mtuChar.readValue();
        this.protocol.setMaxPacketSize(mtuVal.getUint8(0));
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

### 2.2 Transport Abstraction Layer (TypeScript)

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

Refactor to implement `ITransport`. **Crucially, the protocol engine must be extracted into a shared `CommProtocol.ts` class that both transports delegate to.** Duplicating the blast+reconcile state machine into both `HIDTransport` and `BLETransport` is error-prone.

```typescript
// CommProtocol.ts — shared protocol state machine
export class CommProtocol {
    private maxPacketSize = 63; // Default

    constructor(
        private io: {
            sendRaw: (data: Uint8Array) => Promise<void>;
            onRawReceived: (callback: (data: Uint8Array) => void) => void;
        }
    ) {}

    setMaxPacketSize(size: number) {
        this.maxPacketSize = size;
    }

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

### 2.3 UI Connection Selector

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

### 2.4 Phase 2 Verification

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

## Phase 3 — Split Keyboard BLE COMM Verification

### Goal
Ensure the BLE COMM channel works correctly in split keyboard configurations.

---

### 3.1 Slave-Side BLE COMM Forwarding

**As established in [Decision 7](#decision-7-split-keyboard-implications), no proxy logic is needed.** The BLE COMM service is only active on the master half (the slave's BLE is suspended). This means:

- A BLE configurator always talks directly to the master
- Config writes to the master automatically sync to the slave via the existing `SPLIT_MSG_CONFIG_SYNC` mechanism
- The slave can still be configured via USB (the USB COMM channel is always active on whichever half has the cable)

**What needs verification:**
- When a role swap occurs, the new master's BLE stack reinitializes with `ble_hid_reinit_bonds()`. The COMM service must survive this reinit (NimBLE re-registers all GATT services during `ble_gatts_reset()`).
- **Explicit Reset (Architectural Purity):** As detailed in Phase 1.5, `blemod.c` internally handles the reset of the COMM adapter during `ble_hid_set_suspended(true)`. `split_bridge.c` remains completely oblivious to the COMM channel, maintaining the strict module boundaries (where all external modules talk only to the opaque `blemod` API). The side effect is completely safe: when the slave radio suspends, the COMM connection state is wiped cleanly along with the GATT server stop.

---

### 3.2 Phase 3 Verification

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
| TX buffer/queue | FreeRTOS queue + semaphore | Dynamically allocate buffer from `comm_pool` per TX item |
| Module callbacks array | Written once at init, read-only after | None |
| `s_reply_transport` | N/A | Removed completely. Target is explicitly passed down the call chain |
| Blast RX State | None (assumes one active transport) | Each active transport maintains independent session state and dynamic pool buffer |

**Dynamic Pool Allocation for TX/RX Sessions:** The legacy TX/RX system used a single monolithic set of static buffers (`tx_buf`, `rx_buf`, blast state, etc.).

**Solution:** By transitioning to `comm_pool`, each incoming or outgoing session dynamically allocates its own buffer from the shared 32 KB memory pool upon transfer initiation and releases it upon completion. The TX queue serializes sends. `comm_send_payload(target, ...)` allocates an outbound buffer from `comm_pool`, copies the payload, and injects the dynamic pointer and `target` into `tx_queue_item_t`. The TX task dequeues one-at-a-time, reading the target transport directly from the item, and calls `comm_pool_free()` once transmission finishes.
**Note on Broadcasting:** Unsolicited broadcasting is explicitly removed. The configurator will request updates (e.g., polling) and those requests will be fulfilled. No unsolicited data will ever be sent, which vastly simplifies the queueing architecture.

**Enhancement:** Extend `tx_queue_item_t` to include the target transport:

```c
typedef struct {
    uint8_t *data;            // Dynamically allocated from comm_pool
    uint16_t len;
    comm_transport_t target;  // NEW: which transport to send on
} tx_queue_item_t;
```

`comm_send_payload(target, payload, len)` allocates `data = comm_pool_alloc(len)`, copies `payload` into `data`, and assigns `target` to the newly minted `tx_queue_item_t.target` before pushing it to the FreeRTOS queue. If `xQueueSend` fails, it immediately calls `comm_pool_free(data)` to prevent a leak.

### Memory Budget & Pool Savings (`comm_pool`)

| Component | Legacy Static SRAM | New Dynamic Pool SRAM | Net Impact | Notes |
|-----------|:------------------:|:---------------------:|:----------:|-------|
| RX Reassembly Buffer | 21,500 B | 0 B (idle) / up to 32 KB (shared) | -21.5 KB static | Allocated dynamically from `comm_pool` |
| TX Staging Buffer | 21,500 B | 0 B (idle) / up to 32 KB (shared) | -21.5 KB static | Allocated dynamically from `comm_pool` |
| COMM GATT Service & CCCDs | 0 B | ~300 B | +300 B | NimBLE attribute table |
| MSYS mbufs (4 extra blocks) | 0 B | ~1,024 B | +1 KB | For COMM notifications |
| `comm_pool` & Session State | 0 B | ~100 B | +100 B | Pool tracking counters & pointers |
| **Total Firmware Footprint** | **43,000 B** | **~1,424 B static (+ dynamic pool)** | **-41.5 KB static SRAM** | **Enables 32 KB dynamic pool while saving 11 KB net RAM!** |

By eliminating the hardcoded 43 KB static BSS footprint (`rx_buf` and `tx_buf`) and replacing it with a 32 KB shared memory pool (`comm_pool`), the firmware achieves a **net saving of ~11 KB of continuous internal SRAM**, even when the pool is at maximum capacity!

### Concurrent USB + BLE COMM Sessions

**Fully Supported via Shared Memory Pool.** Multiple configurator instances (e.g., USB and BLE) can connect and operate simultaneously. Each incoming packet is enqueuing into `s_comm_processing_queue` along with its source transport tag and processed sequentially by `comm_processing_task`.

**Concurrent Blasts & Dynamic Buffer Allocation:**
Because `comm_rx.c` and `comm_tx.c` allocate session buffers dynamically from `comm_pool` rather than relying on a single monolithic static buffer, **concurrent blast transfers across multiple transports are natively supported**, provided their combined buffer requirements do not exceed the 32 KB pool ceiling:
- **RX Blasts:** Each active transport maintains its own independent `comm_rx_session_t` and dynamically allocated buffer pointer. For example, USB can conduct a 16 KB layout transfer while BLE simultaneously executes single-packet status queries or a secondary 12 KB transfer. Neither session interferes with the other's reassembly state or memory space.
- **TX Blasts:** `comm_send_payload()` allocates an outbound buffer from `comm_pool` and pushes the pointer to the FreeRTOS TX queue. The TX task serializes packet transmission across target transports. Once a payload completes transmission (or times out), its buffer is immediately freed back to the pool.
- **Out-of-Memory (OOM) Protection:** If concurrent sessions attempt to allocate more total memory than `CONFIG_COMM_POOL_SIZE` (defaulting to 32 KB), `comm_pool_alloc()` returns NULL. The requesting channel cleanly rejects the transfer by replying with `PAYLOAD_FLAG_ERR` (OOM reason code). Existing active transfers continue undisturbed without preemption, and the rejected client automatically retries once pool memory is released.

**Caveat:** If both configurators try to SET the same config key simultaneously via single packets, the last write wins (no locking). This is acceptable because:
1. It's an unlikely scenario (who has two configurators open at once?)
2. The firmware's NVS writes are atomic per key
3. The configurator always re-reads after a SET to confirm

### Error Handling

| Error | USB Behavior (unchanged) | BLE Behavior (new) |
|-------|-------------------------|---------------------|
| CRC mismatch | Silently drop | Silently drop |
| Transport disconnect mid-transfer | `comm_transport_on_disconnect(USB)` sweeps & frees buffer via `comm_pool_free()` | `comm_transport_on_disconnect(BLE)` sweeps & frees buffer via `comm_pool_free()` + clears conn_handle |
| Module callback failure | Send ACK\|ERR | Send ACK\|ERR (via notification) |
| BLE MTU too small | N/A | Handled gracefully: dynamically chunks max packet size down |
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
| BLE MTU negotiation fails below 66 bytes | COMM throughput decreases due to smaller packets | Low | Transparently handled by dynamic packet sizing down to 20 bytes |
| NimBLE mbuf exhaustion under heavy COMM + HID traffic | Stack crash or dropped packets | Low | Increased MSYS block counts + throttle COMM during rapid typing |
| Web Bluetooth browser support narrows | Fewer platforms supported | Very Low | WebHID USB path remains primary; BLE is additive |
| GATT service discovery slow on some hosts | Connection setup takes 5–10 seconds | Medium | Acceptable for a configuration tool (not real-time) |
| Two concurrent configurators cause config conflicts | Inconsistent state | Very Low | Last-write-wins is acceptable; add warning in UI |
| NimBLE reinit during role swap loses COMM service state | Configurator must reconnect | Medium | Clear adapter state, configurator auto-reconnects |
| Blast mode over BLE slower than USB | Config transfers take longer | High (expected) | BLE notification interval is ~7.5ms min vs USB's 1ms; blast mode handles this gracefully. Expected throughput: ~4–8 KB/s over BLE vs ~30 KB/s over USB |

---

## File Change Manifest

> [!NOTE]
> Phase 0 has its own detailed file manifest in the [comm_module Extraction section](#file-change-manifest-comm_module-extraction). The manifest below covers Phases 1–3.

### Phase 1 — BLE COMM Service

| File                                         | Action | Description                                                  |
| ----------------------------------------------| --------| --------------------------------------------------------------|
| `components/ble_module/ble_comm_service.c`   | NEW    | GATT service definition, RX/TX access callbacks              |
| `components/ble_module/ble_comm_service.h`   | NEW    | Public API: register, set conn handle, TX handle getter      |
| `components/ble_module/ble_comm_transport.c` | NEW    | BLE transport adapter for comm_module                        |
| `components/ble_module/ble_comm_transport.h` | NEW    | Public API: init, conn handle set/clear, tx handle setter    |
| `components/ble_module/blemod.c`             | MODIFY | Call COMM service init, notify adapter on connect/disconnect |
| `components/ble_module/CMakeLists.txt`       | MODIFY | Add new source files, add `comm_module` dependency           |
| `sdkconfig.defaults`                         | MODIFY | Bump CCCD count, set explicit MTU, bump MSYS blocks          |

### Phase 2 — Configurator

| File | Action | Description |
|------|--------|-------------|
| `configurator/src/services/ITransport.ts` | NEW | Transport interface definition |
| `configurator/src/services/CommProtocol.ts` | NEW | Shared blast+reconcile protocol engine |
| `configurator/src/services/BLETransport.ts` | NEW | Web Bluetooth transport implementation |
| `configurator/src/services/HIDTransport.ts` | MODIFY | Refactor to implement ITransport, delegate protocol to CommProtocol |
| `configurator/src/services/DeviceController.ts` | MODIFY | Accept ITransport instead of HIDTransport |
| `configurator/src/App.tsx` | MODIFY | Add transport selector UI |
| `configurator/src/types/protocol.ts` | MODIFY | Add BLE COMM UUIDs |

### Phase 3 — Split Verification

| File         | Action | Description                                                       |
| --------------| --------| -------------------------------------------------------------------|
| No new files | —      | Phase 3 is verification-only; existing split mechanisms handle it |

### Documentation Updates (all phases)

| File                                    | Action                                          |
| -----------------------------------------| -------------------------------------------------|
| `universe/modules/BLE_MODULE.md`        | Update with COMM service documentation          |
| `universe/modules/USB_MODULE.md`        | Update with transport abstraction documentation |
| `universe/modules/CONFIGURATOR.md`      | Update with dual-transport support              |
| `COMM_PROTOCOL.md`                      | Add BLE transport section                       |
| `components/ble_module/BLE_MODULE.md`   | Update local module docs                        |
| `components/usb_module/USB_MODULE.md`   | Update local module docs                        |
| `components/comm_module/COMM_MODULE.md` | NEW — Document the new comm_module and comm_pool |
| `components/comm_module/comm_pool.c/.h` | NEW — Shared memory pool for dynamic buffer allocation |
