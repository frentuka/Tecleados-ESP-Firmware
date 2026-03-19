# USB Module

The `usb_module` component implements the full USB stack for the DF-ONE keyboard firmware. It exposes the device to the host as a **dual-interface HID device**: one interface for keyboard input (6KRO, NKRO, consumer control) and one bidirectional vendor-defined interface used as a reliable configuration and command channel.

Built on top of **TinyUSB** via `esp_tinyusb`, the module adds its own multi-packet reliability protocol on the comm channel, with two operating modes: **sequential** (small payloads, one ACK per packet) and **blast** (large payloads sent in bulk, with bitmap-based reconciliation for any missed packets).

---

## Table of Contents

1. [Directory Structure](#directory-structure)
2. [USB Interfaces](#usb-interfaces)
3. [Communication Protocol](#communication-protocol)
   - [Packet Format](#packet-format)
   - [Flag Bits](#flag-bits)
   - [Sequential Mode](#sequential-mode)
   - [Blast Mode](#blast-mode)
   - [CRC-8 Integrity](#crc-8-integrity)
4. [File-by-File Reference](#file-by-file-reference)
   - [usbmod.h / usbmod.c](#usbmodh--usbmodc)
   - [usb_defs.h](#usb_defsh)
   - [usb_descriptors.h](#usb_descriptorsh)
   - [usb_crc.h / usb_crc.c](#usb_crch--usb_crcc)
   - [usb_send.h / usb_send.c](#usb_sendh--usb_sendc)
   - [usb_callbacks_rx.h / usb_callbacks_rx.c](#usb_callbacks_rxh--usb_callbacks_rxc)
   - [usb_callbacks_tx.h / usb_callbacks_tx.c](#usb_callbacks_txh--usb_callbacks_txc)
   - [usb_callbacks.h / usb_callbacks.c](#usb_callbacksh--usb_callbacksc)
   - [CMakeLists.txt](#cmakeliststxt)
5. [Task Architecture](#task-architecture)
6. [Data Flow](#data-flow)
   - [Reception Path (Host → Device)](#reception-path-host--device)
   - [Transmission Path (Device → Host)](#transmission-path-device--host)
7. [Blast Mode Deep Dive](#blast-mode-deep-dive)
8. [Callback System](#callback-system)
9. [Initialization Sequence](#initialization-sequence)
10. [Constants & Limits Reference](#constants--limits-reference)

---

## Directory Structure

```
components/usb_module/
├── CMakeLists.txt           # Build config, source list, dependencies
├── USB_MODULE.md            # This file
├── usbmod.c                 # Main init, keyboard HID API, TinyUSB glue
├── usb_callbacks.c          # HID report callbacks, packet router, task spawner
├── usb_callbacks_rx.c       # RX buffer management, blast receive, bitmap
├── usb_callbacks_tx.c       # TX queue, blast transmit, reconciliation task
├── usb_send.c               # Low-level single-packet sender
├── usb_crc.c                # CRC-8 lookup table implementation
└── include/
    ├── usbmod.h             # Public API (keyboard HID, callbacks, init)
    ├── usb_defs.h           # Protocol types: flags, packet struct, module IDs
    ├── usb_descriptors.h    # USB device/config descriptors, report descriptors
    ├── usb_crc.h            # CRC API
    ├── usb_send.h           # Low-level send API
    ├── usb_callbacks.h      # Callback registration/execution API
    ├── usb_callbacks_rx.h   # RX internal API
    └── usb_callbacks_tx.h   # TX internal API
```

---

## USB Interfaces

The device presents itself to the host with the following identity:

| Field        | Value       |
|--------------|-------------|
| Vendor ID    | `0x303A`    |
| Product ID   | `0x1324`    |
| Manufacturer | `Tecleados` |
| Product      | `DF-ONE`    |
| Serial       | `13548`     |

There are **two HID interfaces**:

### Interface 0 — Keyboard (`ITF_NUM_HID_KBD`)

Unidirectional: device sends keyboard reports to the host. The host can send LED status reports back (Caps Lock, Num Lock, etc.).

| Report ID | Name              | Size      | Purpose                                    |
|-----------|-------------------|-----------|--------------------------------------------|
| `1`       | `REPORT_ID_KEYBOARD` | 8 bytes | 6KRO boot keyboard (1 modifier + 6 keycodes) |
| `2`       | `REPORT_ID_NKRO`     | ~64 bytes | N-Key Rollover bitmap (231 keys = 29 bytes + modifier) |
| `4`       | `REPORT_ID_CONSUMER` | 2 bytes | Media/consumer control (volume, play, etc.) |

- **Endpoint IN**: `0x81` (`EPNUM_HID_KBD_IN`)
- Supports both boot protocol (6KRO) and report protocol (NKRO) — `usb_keyboard_use_boot_protocol()` indicates which one the host requested.

### Interface 1 — Comm Channel (`ITF_NUM_HID_COMM`)

Bidirectional vendor-defined channel used to send configuration, commands, and data between the host software and the firmware.

| Report ID | Name           | Size     | Direction       |
|-----------|----------------|----------|-----------------|
| `3`       | `REPORT_ID_COMM` | 63 bytes | Bidirectional   |

- **Endpoint IN**: `0x82` (`EPNUM_HID_COMM_IN`) — device → host
- **Endpoint OUT**: `0x02` (`EPNUM_HID_COMM_OUT`) — host → device

All custom data exchange (reading/writing config, firmware updates, macro management, etc.) goes through this interface using the reliability protocol described below.

---

## Communication Protocol

### Packet Format

Every comm-channel packet is exactly `COMM_REPORT_SIZE` (64) bytes, structured as:

```c
typedef struct __attribute__((packed)) {
    uint8_t  flags;              // Control flags (see below)
    uint16_t remaining_packets;  // Packets left after this one
    uint8_t  payload_len;        // Actual data bytes in payload[] (0–58)
    uint8_t  payload[58];        // Data payload (may be partially used)
    uint8_t  crc;                // CRC-8 checksum of the entire packet
} usb_packet_msg_t;              // Total: 63 bytes content + 1 byte report ID = 64
```

The `remaining_packets` field carries a dual meaning:
- When the packet is **data** (sent by the transmitter): it is the count of packets still to come after this one. `0` means this is the last packet.
- When the packet is a **response** (e.g., bitmap): it is unused or zero.

### Flag Bits

The `flags` byte drives the protocol state machines on both sides. Individual bits have defined meanings, and some combinations form named combined flags:

| Constant                  | Value  | Description                                              |
|---------------------------|--------|----------------------------------------------------------|
| `PAYLOAD_FLAG_FIRST`      | `0x80` | First packet in a transfer sequence                      |
| `PAYLOAD_FLAG_MID`        | `0x40` | Middle packet (blast mode only)                          |
| `PAYLOAD_FLAG_LAST`       | `0x20` | Last packet in a transfer sequence                       |
| `PAYLOAD_FLAG_ACK`        | `0x10` | Acknowledge receipt                                      |
| `PAYLOAD_FLAG_NAK`        | `0x08` | Negative acknowledge (CRC error or missing)              |
| `PAYLOAD_FLAG_STATUS_REQ` | `0x50` | `MID\|ACK` — Blast TX asks RX for its bitmap             |
| `PAYLOAD_FLAG_BITMAP`     | `0x48` | `MID\|NAK` — RX responds to STATUS_REQ with a bitmap     |
| `PAYLOAD_FLAG_OK`         | `0x04` | Transfer completed successfully                          |
| `PAYLOAD_FLAG_ERR`        | `0x02` | Transfer failed                                          |
| `PAYLOAD_FLAG_ABORT`      | `0x01` | Abort in-progress transfer                               |

Direction convention:
- `FIRST`, `MID`, `LAST` flags belong to **data packets** going from transmitter to receiver.
- `ACK`, `NAK`, `OK`, `ERR`, `ABORT` flags belong to **response packets** going back.
- `STATUS_REQ` and `BITMAP` are special blast-reconciliation packets that flow in opposite directions on each use.

### Sequential Mode

Used automatically when the payload fits in a **single packet** (≤ 58 bytes).

```
Transmitter                    Receiver
    |                              |
    |--- FIRST|LAST (data) ------> |  (remaining_packets = 0)
    |                              |  [CRC check]
    |<-- ACK (or NAK) ------------ |
    |                              |
    | (if NAK: retry up to 3x)     |
    | (if ACK: done)               |
```

- On **NAK**: retransmits the same packet. Up to `TX_NAK_RESEND_MAX_ATTEMPTS` (3) retries before aborting.
- On **timeout** (1000 ms with no ACK): transfer aborts and buffers are erased.

### Blast Mode

Used automatically when the payload requires **more than one packet** (> 58 bytes). Designed for high-throughput transfers (e.g., full config dumps, layout data).

The host and device can play either role (transmitter or receiver) on the comm channel.

```
Transmitter                             Receiver
    |                                       |
    |--- FIRST (remaining=total-1) -------> |  Store packet[0]
    |<-- ACK -------------------------------|
    |                                       |
    |--- MID (remaining=total-2) ---------> |  Store packet[1]
    |--- MID (remaining=total-3) ---------> |  Store packet[2]
    |--- MID ...                            |  ...  (no per-packet ACK)
    |--- MID (remaining=1) ------------->   |  Store packet[total-2]
    |                                       |
    |--- STATUS_REQ (MID|ACK) -----------> |  "What did you receive?"
    |<-- BITMAP (MID|NAK) ---------------  |  RX sends received-packet bitmap
    |                                       |
    | (check bitmap for missing packets)    |
    |--- MID (retransmit missing) ------->  |  (up to 5 reconcile rounds)
    |--- STATUS_REQ again if needed ...     |
    |                                       |
    |--- LAST (remaining=0) ------------->  |  Commit full payload
    |<-- ACK|OK (or ACK|ERR) ------------- |
```

Key properties:
- The **FIRST** packet establishes the total packet count via `remaining_packets`.
- All **MID** packets are sent fire-and-forget (no per-packet ACK), maximizing throughput.
- The **STATUS_REQ / BITMAP** exchange uses a 48-byte bitmap (up to 384 bits) to identify which of the MID packets the receiver missed.
- The transmitter retransmits only the missing packets, then sends STATUS_REQ again (up to `TX_BLAST_MAX_RECONCILE_ROUNDS` = 5 times).
- Once the bitmap confirms all MID packets received, the LAST packet is sent to finalize and trigger processing.

Packet ordering on the **receive side**: Each incoming packet's position in the buffer is derived from:
```c
index = total_packets - 1 - remaining_packets
offset = index * 58   // byte offset into rx_buf
```
This means packets can arrive and be stored out of order without issue.

### CRC-8 Integrity

Every packet carries a CRC-8 checksum in its last byte (`crc` field of `usb_packet_msg_t`). The CRC is computed over all preceding bytes of the packet using **polynomial 0x07** with a precomputed 256-entry lookup table.

- `usb_crc_prepare_packet(packet)` — compute and write the CRC into `packet[COMM_REPORT_SIZE - 1]`.
- `usb_crc_verify_packet(packet)` — verify integrity; returns `true` if valid.

On the receive side, any packet that fails CRC is responded to with `PAYLOAD_FLAG_NAK` (in sequential mode) or simply discarded (in blast mode — the bitmap will expose the gap).

---

## File-by-File Reference

### usbmod.h / usbmod.c

**Role:** Public interface of the entire module. Contains USB initialization, keyboard HID send functions, and the public wrappers for the callback system.

**Public API:**

```c
// Initialize USB (call once at startup)
void usb_init(void);

// Query current HID protocol mode
bool usb_keyboard_use_boot_protocol(void);

// Send keyboard reports
bool usb_send_keyboard_6kro(uint8_t modifier, const uint8_t keycodes[6]);
bool usb_send_keyboard_nkro(uint8_t modifier, const uint8_t *bitmap, uint16_t len);
bool usb_send_consumer_report(uint16_t keycode);

// Callback registration for the comm channel
void usbmod_register_callback(usb_msg_module_t module, usb_data_callback_t callback);
bool usbmod_execute_callback(usb_msg_module_t module, uint8_t const *data, uint16_t len);

// Test helpers (send ASCII text/keystrokes)
void usb_send_char(char c);
void usb_send_keystroke(uint8_t hid_keycode);
```

**Internal:**
- `usb_task(void *arg)` — Runs on **CPU core 1**, continuously calls `tud_task()` to handle USB I/O. Priority 5, 4 KB stack.
- `usb_event_cb(tinyusb_event_t *event, void *arg)` — Handles `TINYUSB_EVENT_ATTACHED` (host connected) and `TINYUSB_EVENT_DETACHED` (host disconnected, clears TX/RX buffers).
- `tud_hid_get_report_cb` / `tud_hid_set_report_cb` — Thin stubs that forward to `usbmod_tud_hid_get_report_cb` / `usbmod_tud_hid_set_report_cb` in `usb_callbacks.c`.

---

### usb_defs.h

**Role:** Central definition file for the comm-channel protocol. Included by most other files in this module.

**Contents:**

```c
// Module IDs — identify which subsystem a comm payload belongs to
typedef enum usb_msg_module : uint8_t {
    MODULE_CONFIG = 0,   // Configuration read/write
    MODULE_SYSTEM,       // System-level commands
    MODULE_ACTION,       // Action/macro execution
    MODULE_STATUS,       // Status queries
    USB_MODULE_COUNT     // Sentinel
} usb_msg_module_t;

// Payload flags
#define PAYLOAD_FLAG_FIRST       0x80
#define PAYLOAD_FLAG_MID         0x40
#define PAYLOAD_FLAG_LAST        0x20
#define PAYLOAD_FLAG_ACK         0x10
#define PAYLOAD_FLAG_NAK         0x08
#define PAYLOAD_FLAG_STATUS_REQ  0x50  // MID|ACK
#define PAYLOAD_FLAG_BITMAP      0x48  // MID|NAK
#define PAYLOAD_FLAG_OK          0x04
#define PAYLOAD_FLAG_ERR         0x02
#define PAYLOAD_FLAG_ABORT       0x01

// Maximum bytes per packet payload
#define MAX_PAYLOAD_LENGTH  58

// Packet structure (63 bytes, packed)
typedef struct __attribute__((packed)) {
    uint8_t  flags;
    uint16_t remaining_packets;
    uint8_t  payload_len;
    uint8_t  payload[58];
    uint8_t  crc;
} usb_packet_msg_t;

// Callback type for comm channel modules
typedef bool (*usb_data_callback_t)(uint8_t *data, uint16_t data_len);
```

The first byte of every payload (after multi-packet reassembly) is the **module ID** (`usb_msg_module_t`), followed by the module-specific data. The callback system uses this byte to dispatch to the correct handler.

---

### usb_descriptors.h

**Role:** Defines all USB descriptors presented to the host at enumeration time.

**Key definitions:**

| Symbol                 | Value    | Description                          |
|------------------------|----------|--------------------------------------|
| `ITF_NUM_HID_KBD`      | `0`      | Keyboard HID interface index         |
| `ITF_NUM_HID_COMM`     | `1`      | Comm HID interface index             |
| `EPNUM_HID_KBD_IN`     | `0x81`   | Keyboard IN endpoint                 |
| `EPNUM_HID_COMM_IN`    | `0x82`   | Comm IN endpoint (device → host)     |
| `EPNUM_HID_COMM_OUT`   | `0x02`   | Comm OUT endpoint (host → device)    |
| `REPORT_ID_KEYBOARD`   | `1`      | 6KRO boot keyboard report            |
| `REPORT_ID_NKRO`       | `2`      | NKRO bitmap report                   |
| `REPORT_ID_COMM`       | `3`      | Vendor comm report                   |
| `REPORT_ID_CONSUMER`   | `4`      | Consumer/media control report        |
| `COMM_REPORT_SIZE`     | `64`     | Total comm HID report size (bytes)   |
| `NKRO_BYTES`           | `29`     | Bytes in NKRO bitmap (231 keys)      |

Contains:
- `desc_device` — USB device descriptor (VID, PID, etc.)
- `desc_fs_configuration` — Full-speed configuration descriptor (2 interfaces, endpoints)
- `desc_hid_report_kbd` — HID report descriptor for the keyboard interface
- `desc_hid_report_comm` — HID report descriptor for the comm interface
- `string_desc_arr` — Manufacturer, product, and serial strings

---

### usb_crc.h / usb_crc.c

**Role:** CRC-8 checksum for packet integrity verification.

**API:**

```c
// Compute CRC over the packet and write it into the last byte
void usb_crc_prepare_packet(uint8_t *packet);

// Verify the CRC of a received packet; returns true if valid
bool usb_crc_verify_packet(const uint8_t *packet);
```

**Implementation:** Uses a precomputed 256-entry lookup table with polynomial `0x07`, initial value `0x00`. The CRC is computed over `COMM_REPORT_SIZE - 1` bytes (62 bytes), and the result is stored in byte 63. Verification runs the same CRC over all 63 bytes and checks the result is 0.

---

### usb_send.h / usb_send.c

**Role:** The lowest layer of the TX path. Handles the actual submission of a 63-byte comm report to TinyUSB.

**API:**

```c
// Build a control packet from components and send it
bool build_send_single_msg_packet(uint8_t flags, uint16_t remaining,
                                   uint8_t payload_len, uint8_t *payload);

// Send a pre-built raw packet buffer
bool send_single_packet(uint8_t *packet, uint16_t packet_len);
```

**`send_single_packet` behavior:**
1. Polls `tud_hid_n_ready(ITF_NUM_HID_COMM)` with 1-tick delays.
2. Times out after **100 ms** if the endpoint never becomes ready.
3. Calls `tud_hid_n_report(ITF_NUM_HID_COMM, REPORT_ID_COMM, packet, len)`.
4. Returns `false` if the send fails or times out.

All higher-level TX code ultimately calls `send_single_packet`.

---

### usb_callbacks_rx.h / usb_callbacks_rx.c

**Role:** Manages the receive-side buffer and implements the blast-mode receive state machine.

**State (module-level statics):**

| Variable                   | Type / Size    | Purpose                                       |
|----------------------------|----------------|-----------------------------------------------|
| `rx_buf`                   | `uint8_t[21500]` | Reassembly buffer for incoming payloads      |
| `rx_buf_len`               | `uint16_t`     | Total valid bytes accumulated in `rx_buf`     |
| `rx_last_packet_timestamp_us` | `uint64_t` | Timestamp of the last received packet (µs)   |
| `rx_blast_mode_flag`       | `bool`         | True when in blast receive mode               |
| `rx_blast_total_packets`   | `uint16_t`     | Total packets expected in this blast transfer |
| `rx_blast_start_time_us`   | `uint64_t`     | Timestamp when blast started (for metrics)   |
| `rx_blast_bitmap[48]`      | `uint8_t`      | Bit `i` set when packet `i` received (384 max)|
| `rx_blast_payload_lens[384]` | `uint8_t`   | Actual payload_len for each received packet   |

**Public API:**

```c
// Sequential mode: process one incoming packet (FIRST/MID/LAST)
void process_rx_request(const usb_packet_msg_t msg);

// Reset RX state and clear buffer
void erase_rx_buffer(void);

// Get microsecond timestamp of last received packet (for timeout monitoring)
uint64_t rx_get_last_packet_timestamp_us(void);

// Blast mode: query/update/receive/commit
bool rx_blast_active(void);
void rx_blast_update_activity(void);
void rx_blast_receive_packet(const usb_packet_msg_t *msg);
void rx_blast_build_bitmap_response(usb_packet_msg_t *out_msg);
bool rx_blast_commit(const usb_packet_msg_t *last_msg);
```

**`rx_blast_receive_packet` logic:**
- Computes `index = total - 1 - remaining_packets`.
- Skips duplicates (bitmap bit already set).
- Writes `payload` to `rx_buf[index * 58]`.
- Records `payload_len` in `rx_blast_payload_lens[index]`.
- Sets the corresponding bit in `rx_blast_bitmap`.

**`rx_blast_commit` logic:**
- Writes the LAST packet's payload.
- Calculates `rx_buf_len = sum(rx_blast_payload_lens[0..total-1])`.
- Calls `process_rx_buffer()` to dispatch to the appropriate module callback.
- Logs transfer metrics (KB/s, receive time, process time).
- Resets all blast state.

**`process_rx_buffer`** (internal): reads `rx_buf[0]` as the `usb_msg_module_t`, then calls `execute_callback(module, rx_buf + 1, rx_buf_len - 1)`.

---

### usb_callbacks_tx.h / usb_callbacks_tx.c

**Role:** Manages the transmit-side queue and implements both sequential and blast-mode transmission state machines.

**State (module-level statics):**

| Variable                      | Type / Size    | Purpose                                            |
|-------------------------------|----------------|----------------------------------------------------|
| `tx_buf`                      | `uint8_t[21500]` | Staging buffer for outgoing payload              |
| `tx_buf_len`                  | `uint16_t`     | Total bytes in `tx_buf`                            |
| `tx_buf_idx`                  | `uint16_t`     | Current byte position (sequential mode)           |
| `tx_last_packet_timestamp_us` | `uint64_t`     | Timestamp of last sent packet (µs)                |
| `tx_awaiting_response`        | `bool`         | True when waiting for ACK in sequential mode      |
| `tx_nak_resend_attempts`      | `uint8_t`      | NAK retry counter                                 |
| `tx_blast_mode_flag`          | `bool`         | True when in blast TX mode                        |
| `tx_blast_total_packets`      | `uint16_t`     | Total packets in this blast transfer              |
| `tx_blast_start_time_us`      | `uint64_t`     | Blast start timestamp (for metrics)               |
| `tx_blast_reconcile_attempts` | `uint8_t`      | Number of bitmap reconcile rounds so far          |
| `tx_queue`                    | `QueueHandle_t`  | 16-item queue of `(data*, len)` pairs            |
| `tx_done_sem`                 | `SemaphoreHandle_t` | Binary semaphore: signaled when TX completes  |

**Public API:**

```c
// Queue a payload for transmission (non-blocking)
// Returns false if the queue is full
bool send_payload(const uint8_t *payload, uint16_t payload_len);

// Create TX queue, semaphore, and spawn usb_tx_task — called by usb_callbacks_init()
void usb_tx_init(void);

// Query blast TX mode
bool tx_blast_active(void);

// Called by usb_callbacks.c when a BITMAP response is received
void tx_blast_handle_bitmap(const usb_packet_msg_t *msg);

// Called by usb_callbacks.c when ACK/NAK/OK/ERR/ABORT is received
void process_tx_response(const usb_packet_msg_t msg);

// Get microsecond timestamp of last sent packet (for timeout monitoring)
uint64_t tx_get_last_packet_timestamp_us(void);

// Reset TX state and signal tx_done_sem (unblocks usb_tx_task)
void erase_tx_buffer(void);
```

**`usb_tx_task` (internal, spawned by `usb_tx_init`):**
- Waits on `tx_queue` for a `(data*, len)` pair.
- Copies data into `tx_buf`.
- Calculates `total_packets = ceil(len / 58)`.
- If `total_packets > 1`: enters blast mode, sends FIRST, waits for ACK.
- If `total_packets == 1`: sends FIRST|LAST, waits for ACK.
- On ACK (blast): calls `tx_blast_send_all_mid_packets()`, then sends `STATUS_REQ`.
- Waits on `tx_done_sem` (with `TX_TIMEOUT_MS` timeout) for the transfer to complete.
- `erase_tx_buffer()` signals the semaphore when a terminal response (`ACK|OK`, `ACK|ERR`, `ABORT`) is received.

**`tx_blast_handle_bitmap` (internal, called from `usb_callbacks.c`):**
- Reads the bitmap from the BITMAP response packet.
- Identifies which MID packets are missing (bit not set).
- Retransmits missing packets via `tx_send_packet_by_index()`.
- If more than 0 missing packets remain and `reconcile_attempts < 5`: sends another STATUS_REQ.
- If all received: sends LAST packet.
- On max reconcile failures: sends PAYLOAD_FLAG_ABORT and signals done.

---

### usb_callbacks.h / usb_callbacks.c

**Role:** The central hub of the module. Implements TinyUSB HID callbacks, routes incoming packets to the correct handler, manages the module callback registry, spawns background tasks, and owns the processing queue.

**TinyUSB Callbacks (called by TinyUSB from `usb_task`):**

```c
// Returns the HID report descriptor for a given interface
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance);

// Called when host requests a report (not actively used for comm)
uint16_t usbmod_tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                        hid_report_type_t report_type,
                                        uint8_t *buffer, uint16_t reqlen);

// Called when host sends a report — main RX entry point
void usbmod_tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                                   hid_report_type_t report_type,
                                   uint8_t const *buffer, uint16_t bufsize);
```

**`usbmod_tud_hid_set_report_cb` behavior:**

For the **keyboard interface** (`instance == ITF_NUM_HID_KBD`):
- Only handles `HID_REPORT_TYPE_OUTPUT` (LED status).
- Calls `kb_state_update_leds(led_status)`.

For the **comm interface** (`instance == ITF_NUM_HID_COMM`):
1. Strips the leading report ID byte to get raw packet bytes.
2. Validates length (1–64 bytes).
3. Calls `usb_crc_verify_packet()`.
4. On CRC failure (sequential mode): sends NAK and returns.
5. On success: pushes packet to `usb_processing_queue`.
6. Immediate ACK decision:
   - ACK `PAYLOAD_FLAG_FIRST` packets immediately.
   - ACK `PAYLOAD_FLAG_LAST` packets from the blast receive side after `rx_blast_commit()`.
   - **Do not** ACK `STATUS_REQ` or `BITMAP` packets (they are blast-reconciliation exchanges).
   - **Do not** ACK MID packets (blast fire-and-forget).

**Callback Registry:**

```c
// Register a callback for a module (CONFIG/SYSTEM/ACTION/STATUS)
void register_callback(usb_msg_module_t module, usb_data_callback_t callback);

// Execute the registered callback for a module
bool execute_callback(usb_msg_module_t module, uint8_t const *data, uint16_t len);
```

Public wrappers: `usbmod_register_callback()` and `usbmod_execute_callback()` in `usbmod.h`.

**`process_incoming_packet` (internal):**
Routes a dequeued packet based on its flags:

| Flags match          | Action                                                   |
|----------------------|----------------------------------------------------------|
| `STATUS_REQ`         | `rx_blast_build_bitmap_response()` → send BITMAP back    |
| `BITMAP`             | `process_tx_response(msg)` → TX side handles reconcile   |
| `FIRST` or `MID` or `LAST` | `process_rx_request(msg)` — incoming data packet   |
| `ACK`, `NAK`, `OK`, `ERR`, `ABORT` | `process_tx_response(msg)` — response to TX |

**Background Tasks spawned here:**

| Task                   | Priority | Stack  | Core     | Purpose                                    |
|------------------------|----------|--------|----------|--------------------------------------------|
| `usb_processing_task`  | 5        | 8 KB   | Any      | Dequeues and routes packets from `usb_processing_queue` |
| `timeouts_task`        | 5        | 4 KB   | Any      | Polls RX/TX timestamps; clears stale buffers |

**`timeouts_task` behavior:**
- Runs at ~200 Hz (5 ms delay loop).
- If `now - rx_last_packet_timestamp_us > RX_TIMEOUT_MS * 1000` and RX buffer is non-empty: calls `erase_rx_buffer()`.
- If `now - tx_last_packet_timestamp_us > TX_TIMEOUT_MS * 1000` and TX is active: calls `erase_tx_buffer()`.

**Initialization:**

```c
void usb_callbacks_init(void);
```
- Calls `usb_tx_init()`.
- Creates `usb_processing_queue` (128 items of `usb_packet_msg_t`).
- Spawns `usb_processing_task` (internal RAM).
- Spawns `timeouts_task` (internal RAM).

---

### CMakeLists.txt

**Role:** ESP-IDF component build configuration.

```cmake
idf_component_register(
    SRCS "usbmod.c" "usb_send.c" "usb_callbacks.c"
         "usb_callbacks_rx.c" "usb_callbacks_tx.c" "usb_crc.c"
    INCLUDE_DIRS "." "include" "../../components/utils"
    REQUIRES esp_tinyusb driver freertos keyboard
)
```

Dependencies:
- **`esp_tinyusb`** — TinyUSB integration for ESP-IDF.
- **`driver`** — ESP peripheral drivers (GPIO, timers).
- **`freertos`** — RTOS primitives (tasks, queues, semaphores).
- **`keyboard`** — Provides `kb_state_update_leds()` for keyboard LED feedback.
- **`../../components/utils`** — Provides debug utilities (`print_bytes_as_chars`, `BYTE_TO_BINARY`).

---

## Task Architecture

The module runs four concurrent FreeRTOS tasks:

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Core 0                              Core 1                             │
│  ──────────────────────────          ────────────────────────────────   │
│                                                                         │
│  usb_processing_task                 usb_task                           │
│  Priority: 5 | Stack: 8 KB           Priority: 5 | Stack: 4 KB          │
│  Reads usb_processing_queue          Calls tud_task() in a tight loop   │
│  Routes packets to RX/TX handlers    Drives all USB I/O                 │
│                                                                         │
│  timeouts_task                                                          │
│  Priority: 5 | Stack: 4 KB                                              │
│  Polls every 5 ms                                                       │
│  Clears stale RX/TX buffers                                             │
│                                                                         │
│  usb_tx_task                                                            │
│  Priority: 10 | Stack: 4 KB                                             │
│  Reads tx_queue                                                         │
│  Drives TX state machine                                                │
└─────────────────────────────────────────────────────────────────────────┘
```

**`usb_task`** is deliberately pinned to **Core 1** to isolate USB I/O from application logic running on Core 0. The three other tasks run on whichever core FreeRTOS schedules them.

**`usb_tx_task`** has the highest priority (10) among the four tasks so that outgoing data is sent promptly without being delayed by the processing or timeout tasks.

---

## Data Flow

### Reception Path (Host → Device)

```
Host (PC software)
    │
    │  HID SET_REPORT (64 bytes via USB OUT endpoint 0x02)
    ▼
tud_hid_set_report_cb()          [usb_callbacks.c — called from usb_task on Core 1]
    │
    ├─ Keyboard interface:
    │      kb_state_update_leds()
    │
    └─ Comm interface:
           │
           ├─ CRC check ──► FAIL → send NAK (sequential mode) → done
           │
           ├─ Push to usb_processing_queue (128 slots)
           │
           └─ Send immediate ACK/no-ACK (see ACK decision rules above)
                │
                ▼
        usb_processing_task  [dequeues from usb_processing_queue]
                │
                ▼
        process_incoming_packet()
                │
                ├─ STATUS_REQ ──► rx_blast_build_bitmap_response() → send BITMAP
                │
                ├─ BITMAP     ──► tx_blast_handle_bitmap() [TX reconciliation]
                │
                ├─ FIRST/MID  ──► rx_blast_receive_packet()  [blast mode]
                │
                ├─ FIRST|LAST ──► append_payload_to_rx_buffer() [sequential mode]
                │                       │
                │                       └─► process_rx_buffer()
                │                               └─► execute_callback(module, data, len)
                │
                └─ LAST (blast) ──► rx_blast_commit()
                                        └─► process_rx_buffer()
                                                └─► execute_callback(module, data, len)
```

### Transmission Path (Device → Host)

```
Application code
    │
    ▼
send_payload(data, len)          [usb_callbacks_tx.c]
    │  Copies data, pushes to tx_queue (16 slots)
    ▼
usb_tx_task                      [dequeues from tx_queue]
    │
    ├─ total_packets == 1  ──► Send FIRST|LAST
    │                               └─► Wait ACK (tx_done_sem)
    │
    └─ total_packets > 1   ──► Blast mode:
            │
            ├─ Send FIRST (remaining = total - 1)
            ├─ Wait ACK (from usb_processing_task via process_tx_response)
            ├─ Send all MID packets (fire and forget)
            ├─ Send STATUS_REQ
            ├─ Wait for BITMAP → tx_blast_handle_bitmap()
            │       └─ Retransmit missing MIDs (up to 5 rounds)
            ├─ Send LAST (remaining = 0)
            └─ Wait ACK|OK (tx_done_sem)
                    │
                    ▼
            erase_tx_buffer() signals tx_done_sem
                    │
                    ▼
            usb_tx_task resumes, waits for next item on tx_queue
    │
    ▼
send_single_packet()             [usb_send.c]
    │  Polls endpoint ready (100 ms timeout)
    ▼
tud_hid_n_report(ITF_NUM_HID_COMM, REPORT_ID_COMM, packet, 63)
    │
    ▼
Host receives HID IN report (USB IN endpoint 0x82)
```

---

## Blast Mode Deep Dive

Blast mode is the protocol's fast path. It is triggered automatically whenever a payload requires more than one packet (i.e., `payload_len > 58`).

### Transmit State Machine

```
State: IDLE
    │ send_payload() called with multi-packet payload
    ▼
State: BLAST_FIRST_SENT
    │ FIRST packet sent (flags=FIRST, remaining=total-1)
    │ Waiting for ACK from receiver
    ▼ (ACK received via process_tx_response)
State: BLAST_MIDS_SENT
    │ All MID packets sent sequentially (flags=MID, remaining=total-2..1)
    │ No per-packet acknowledgment — fire and forget
    │ STATUS_REQ sent (flags=MID|ACK)
    │ Waiting for BITMAP
    ▼ (BITMAP received via tx_blast_handle_bitmap)
State: BLAST_RECONCILING  (may repeat up to 5 times)
    │ Missing packets identified from bitmap
    │ Missing MID packets retransmitted
    │ If still missing: send STATUS_REQ again
    │ If all received: send LAST (flags=LAST, remaining=0)
    ▼ (ACK|OK received)
State: IDLE
    │ erase_tx_buffer() called, tx_done_sem signaled
```

### Receive State Machine

```
State: IDLE
    │ FIRST packet arrives (remaining > 0)
    ▼
State: BLAST_RECEIVING
    │ rx_blast_total_packets = remaining + 1
    │ For each packet (FIRST or MID):
    │   rx_blast_receive_packet() stores at index=(total-1-remaining)
    │   Sets bit in rx_blast_bitmap
    │ STATUS_REQ arrives → send BITMAP (rx_blast_build_bitmap_response)
    ▼ (LAST packet arrives)
State: COMMITTING
    │ rx_blast_commit() called
    │ Calculates true rx_buf_len from per-packet payload_lens[]
    │ Calls process_rx_buffer() → execute_callback()
    │ Logs metrics
    ▼
State: IDLE
```

### Bitmap Encoding

The 48-byte `rx_blast_bitmap` (384 bits) tracks which packets have been received. Bit `i` corresponds to the packet stored at index `i`:

```
bitmap[i / 8] |= (1 << (i % 8))   // set on receive
```

When building a BITMAP response, the entire 48-byte bitmap is copied into the packet payload so the transmitter can identify gaps.

When the transmitter reads the BITMAP, it iterates over all MID packet indices (1 through total-2) and retransmits any where the corresponding bit is `0`.

### Performance Notes

- Max payload: 21,500 bytes = 370.7 packets × 58 bytes, rounded to 370 packets max.
- Bitmap supports up to 384 packets (48 bytes × 8 bits).
- At USB full-speed (12 Mbps) with 64-byte reports, theoretical throughput ~900 KB/s. The blast reconciliation overhead is typically small when the link is clean.
- Transfer speed metrics are logged at the end of each blast commit.

---

## Callback System

The comm channel routes completed payloads to registered callbacks based on the module ID in the first byte of the payload.

### Module IDs

| ID               | Value | Purpose                                          |
|------------------|-------|--------------------------------------------------|
| `MODULE_CONFIG`  | `0`   | Read/write keyboard configuration (layouts, macros, settings) |
| `MODULE_SYSTEM`  | `1`   | System-level commands (reboot, firmware info, etc.) |
| `MODULE_ACTION`  | `2`   | Trigger actions/macros remotely                  |
| `MODULE_STATUS`  | `3`   | Query device status (connected devices, battery, etc.) |

### Registration

```c
// Register once, typically during application init
usbmod_register_callback(MODULE_CONFIG, my_config_handler);

// Callback signature
bool my_config_handler(uint8_t *data, uint16_t data_len) {
    // data[0] is the module-specific command byte
    // data[1..data_len-1] is the payload
    // return true on success, false on error
}
```

Callbacks are called from `usb_processing_task` context (priority 5), not from any USB interrupt. Long operations are safe but should be considered with respect to the 1000 ms RX timeout.

### Execution

`execute_callback(module, data, len)` calls the registered function if one exists, or returns `false` if no callback is registered for that module.

---

## Initialization Sequence

```
app_main() or equivalent
    │
    └─ usb_init()                              [usbmod.c]
            │
            ├─ Configure tinyusb_config_t with custom descriptors/strings
            ├─ tinyusb_driver_install(&config)
            ├─ Register usb_event_cb for ATTACHED/DETACHED events
            ├─ xTaskCreatePinnedToCore(usb_task, core=1, pri=5, 4KB)
            │
            └─ usb_callbacks_init()            [usb_callbacks.c]
                    │
                    ├─ usb_tx_init()           [usb_callbacks_tx.c]
                    │       ├─ xQueueCreate(tx_queue, 16 items)
                    │       ├─ xSemaphoreCreateBinary(tx_done_sem)
                    │       └─ xTaskCreate(usb_tx_task, pri=10, 4KB)
                    │
                    ├─ xQueueCreate(usb_processing_queue, 128 items)
                    ├─ xTaskCreateWithCaps(usb_processing_task, pri=5, 8KB, INTERNAL)
                    └─ xTaskCreateWithCaps(timeouts_task, pri=5, 4KB, INTERNAL)
```

After initialization:
- `usb_task` runs on Core 1, polling `tud_task()` continuously.
- `usb_processing_task` and `timeouts_task` block on their queue/timer.
- `usb_tx_task` blocks on `tx_queue`.

When the host connects (`TINYUSB_EVENT_ATTACHED`): logged, no buffer action.
When the host disconnects (`TINYUSB_EVENT_DETACHED`): `erase_rx_buffer()` and `erase_tx_buffer()` are called to reset any in-progress transfers.

---

## Constants & Limits Reference

| Constant                       | Value   | Location              | Description                                       |
|--------------------------------|---------|-----------------------|---------------------------------------------------|
| `MAX_PAYLOAD_LENGTH`           | `58`    | `usb_defs.h`          | Max bytes per packet payload                      |
| `COMM_REPORT_SIZE`             | `64`    | `usb_descriptors.h`   | Full comm report size (incl. report ID)           |
| `MAX_RX_BUF_SIZE`              | `21500` | `usb_callbacks_rx.c`  | RX reassembly buffer size (bytes)                 |
| `MAX_TX_BUF_SIZE`              | `21500` | `usb_callbacks_tx.c`  | TX staging buffer size (bytes)                    |
| `RX_BLAST_BITMAP_BYTES`        | `48`    | `usb_callbacks_rx.c`  | Bitmap size in bytes (supports 384 packets)       |
| `RX_BLAST_MAX_PACKETS`         | `384`   | `usb_callbacks_rx.c`  | Max packets in a single blast transfer            |
| `RX_TIMEOUT_MS`                | `1000`  | `usb_callbacks_rx.c`  | Idle timeout before RX buffer is cleared          |
| `TX_TIMEOUT_MS`                | `1000`  | `usb_callbacks_tx.c`  | Idle timeout before TX state is reset             |
| `TX_NAK_RESEND_MAX_ATTEMPTS`   | `3`     | `usb_callbacks_tx.c`  | Max NAK retries in sequential mode                |
| `TX_BLAST_MAX_RECONCILE_ROUNDS`| `5`     | `usb_callbacks_tx.c`  | Max bitmap reconciliation rounds in blast mode    |
| `TX_QUEUE_LENGTH`              | `16`    | `usb_callbacks_tx.c`  | TX queue capacity (pending payloads)              |
| `PROCESS_QUEUE_LENGTH`         | `128`   | `usb_callbacks.c`     | Incoming packet processing queue capacity         |
| `NKRO_BYTES`                   | `29`    | `usb_descriptors.h`   | NKRO bitmap size (231 keys / 8, rounded up)       |
| `USB_MODULE_COUNT`             | `4`     | `usb_defs.h`          | Number of module callback slots                   |
