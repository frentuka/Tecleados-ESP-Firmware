# Binary Comms Migration Implementation Plan

## Goal Description
Strip JSON serialization out of the COMM protocol completely and transition to a pure binary (C-struct) wire format. This will drastically reduce payload sizes (up to 10x), eliminate dynamic memory allocation overhead (cJSON) in the firmware, and significantly accelerate Bluetooth sync times.

## How Hard Is It?

- **For the Firmware (Easy):** This is actually a **reduction in complexity**. The firmware already operates on raw binary structs internally (e.g., `cfg_layer_t`, `cfg_macro_t`). We will delete thousands of lines of `cJSON` parsing/printing code and replace it with direct `memcpy()` operations between the NVS storage and the COMM buffer.
- **For the Configurator (Medium):** This is where the work lies. The TypeScript application can no longer rely on `JSON.parse()`. It must use JavaScript's `DataView` or TypedArrays (like `Uint16Array`) to read and write bytes at precise offsets. We must carefully map the C-struct layouts (including arrays and primitives) to TypeScript parsers, explicitly enforcing Little-Endian byte order on every read/write.

## Proposed Architecture

1. **Natural Memory Alignment (Critical for ESP32):**
   > [!CAUTION]
   > Do **NOT** use `__attribute__((packed))` on complex structs like `cfg_macro_t`. The ESP32's Xtensa/RISC-V architectures strictly require natural alignment for 16-bit and 32-bit memory accesses. Packing these structs will cause `LoadStoreAlignment` exceptions (instant crash) during hot-path execution.
   
   Instead, enforce a strict **Decreasing Size Ordering** rule for all C-struct fields. By ordering fields from largest to smallest (e.g., `uint64_t` -> `uint32_t` -> `uint16_t` -> `uint8_t`/`bool` -> arrays), the compiler eliminates *internal* implicit padding between members.
   
   **Industry Standard Padding:** To handle any necessary alignment without relying on compiler-specific padding behavior, we will use **explicit reserved fields** (e.g., `uint8_t reserved[3];`). This is the professional standard for binary network protocols. It guarantees deterministic sizes across platforms and avoids compiler ambiguity.
   - Do not use `_padding[]` arrays. Instead, insert explicitly sized `reserved` fields to align members or to pad the end of the struct so its total size is naturally aligned to a 4-byte or 8-byte boundary.
   - Ensure all structs are explicitly zero-initialized (e.g., `memset(&struct, 0, sizeof(struct))`) before being populated, to avoid leaking uninitialized memory or garbage bytes over the wire.

   **Compile-Time Validation:** We must enforce this alignment during the build process to prevent future regressions. We will use `_Static_assert` to validate both the total size of the structs and the exact offsets of critical fields.

2. **Repurposing Existing Command Opcodes:**
   Since backwards compatibility with older cached configurators is not a concern, we will not introduce new hexadecimal values. We will completely convert the existing opcodes to natively handle binary payloads:
   - `CFG_CMD_GET = 0x00` (Fetch single item or index)
   - `CFG_CMD_SET = 0x01` (Save single item)
   - `CFG_CMD_STATUS = 0x03` (Unsolicited binary status pushes)

3. **No Protocol Versioning / Backwards Compatibility:**
   > [!NOTE]
   > This is **NOT** a production environment. There are no "prior configurator versions" out in the wild and no devices outside the lab running older software.
   
   We will intentionally ignore backwards compatibility. There is no need for magic headers or protocol versioning checks during handshakes. The firmware will assume the configurator sends correct data for the current schema. If a mismatched configurator omits a handshake or sends bad data, it is the configurator's fault and not a concern for the firmware to handle gracefully.

4. **COMM Buffer Resizing (Memory Optimization):**
   The `comm_module` currently uses massive 21.5 KB static buffers (`s_shared_rx_buf` and `s_shared_tx_buf`) for JSON strings. As part of this migration, these buffers must be resized based on the exact maximum packet size that may be sent:
   - 16 layers: 3456 bytes
   - Physical layout: 4096 bytes
   - Status widget request: 6 bytes
   - Indexes (`lay_idx` + `mac_idx` + `ck_idx` + `cmb_idx`): 429 bytes
   **Total Max Payload:** 7987 bytes.
   We will resize `s_shared_rx_buf` and `s_shared_tx_buf` to **10240 bytes (10KB) each** (using a macro like `MAX_BINARY_PAYLOAD_SIZE + COMM_HEADER_SIZE`). This provides a safe overhead margin above the max payload size while still freeing up over 22 KB of continuous DRAM overall.

## Proposed Changes

### Firmware (C)

1. **Remove cJSON Dependencies & Free PSRAM:**
   - Strip all `#include "cJSON.h"` from `config_module` files.
   - Delete all `_serialize` and `_deserialize` functions.
   - **Crucial:** Remove the "Dual-Path Storage (Auto-Upgrade)" logic inside `cfgmod_get_config()`.
   - **Memory Reclaim:** The `CONFIG_MODULE` currently allocates a massive 32KB PSRAM buffer used exclusively for framing JSON responses. This buffer must be completely removed, as binary payloads will now easily fit inside the `COMM_MODULE`'s static buffers.

2. **Update Structs for Natural Alignment and Arrays:**
   - Audit `cfg_layer_t`, `cfg_macro_t`, `cfg_combo_t`, `cfg_ckey_t`.
   - Reorder fields by **Decreasing Size Ordering** (largest to smallest) to naturally eliminate internal implicit padding, and add explicit trailing `reserved` bytes (e.g., `uint8_t reserved[n];`) to enforce size boundaries.
   - Ensure every struct containing an array includes an explicit `uint8_t count` field (e.g., `event_count`).
   - Add `_Static_assert()` definitions in the headers for all structs to strictly enforce their `sizeof()` and critical `offsetof()` boundaries.
   - **Module-Specific Binary Headers (Alignment Critical):** Instead of a single generic header, we will extend the existing wire header concepts into highly specific Request and Response headers for **each** module (`cfgmod`, `statusmod`, `sysmod`).
     *Why is this critical?* The `comm_module` strips the 1-byte `module_id` and passes the rest to the callback (e.g. `&s_rx_buf[1]`). To ensure the payload struct (like `cfg_macro_t`) starts on a perfect 8-byte boundary, the module-specific headers **must be exactly 7 bytes long**.
     
     ```c
     // --- Config Module Headers (7 bytes) ---
     typedef struct {
         uint8_t cmd;
         uint8_t key_id;
         uint8_t reserved[5];
     } cfgmod_req_header_t;
     
     typedef struct {
         uint8_t cmd;
         uint8_t key_id;
         uint8_t reserved;
         uint32_t status;
     } cfgmod_rsp_header_t;
     
     // --- Status Module Push Payload ---
     // Status pushes have no trailing payload; the struct IS the message.
     // To avoid alignment crashes when reading (if we ever receive it), all >8-bit
     // fields must be placed after 7 bytes of 8-bit data to reach offset 8 in s_rx_buf.
     // (Alternatively, since status pushes are mostly outbound, the TS Configurator
     // handles it easily with DataView).
     typedef struct {
         uint8_t  transport_mode;
         uint8_t  selected_profile;
         uint8_t  pairing_profile;
         uint8_t  split_state;
         uint8_t  split_role;
         uint8_t  reserved[2];      // Pad so the next field is at struct offset 7. (1+7 = 8 aligned!)
         uint16_t connected_bitmap; // Starts at struct offset 7. In s_rx_buf (starts at offset 1), it is at 1+7 = 8 (Aligned!)
     } statusmod_msg_t;
     
     // --- System Module Message Payload (7 bytes) ---
     // Handled by kb_manager.c (MODULE_SYSTEM). Currently handles matrix injection
     // (SYS_CMD_INJECT_KEY), but can easily be extended to trigger 16-bit Action
     // Codes directly (like SYS_ACTION_BLE_TOGGLE or SYS_ACTION_RGB_BRIGHTNESS_UP).
     typedef struct {
         uint8_t  sys_cmd;       // e.g. SYS_CMD_INJECT_KEY (0x01) or SYS_CMD_TRIGGER_ACTION
         uint8_t  row;           // For INJECT_KEY
         uint8_t  col;           // For INJECT_KEY
         uint8_t  state;         // For INJECT_KEY (1=press, 0=release)
         uint8_t  reserved[1];   // Pad to offset 5 so action_code is naturally aligned
         uint16_t action_code;   // Starts at struct offset 5 (absolute offset 6) -> ALIGNED!
     } sysmod_msg_t;
     ```
     By ensuring every module's custom header is exactly 7 bytes, the total offset from the start of the `s_shared_rx_buf` (including the 1-byte `module_id`) is exactly 8 bytes. This guarantees the payload struct immediately following the header is perfectly aligned, preventing `LoadStoreAlignment` hardware crashes on the ESP32.

3. **Refactor `cfgmod.c` Handlers:**
   - `CFG_CMD_GET`: Read the struct from NVS and `memcpy` the raw bytes into `out_payload`.
   - `CFG_CMD_SET`: 
     - **Memory Safety (No Stack Allocation):** FreeRTOS task stacks are small. Do **NOT** instantiate large structs on the stack (`cfg_macro_t mac;`) for `memcpy`, as this will cause an immediate stack overflow. Instead, perform bounds checking and validation *in-place* directly on the data residing in the global receive buffer (`s_shared_rx_buf`), which is already protected by an Exclusive Session Lock.
     - **Strict Bounds Validation (Security):** This must be applied *everywhere* to ensure boundaries are always respected. Before writing to NVS or processing further, run rigorous validation on the struct contents. Ensure array lengths (e.g., `event_count`) do not exceed their maximums, string buffers are explicitly null-terminated to prevent over-reads, and enumerations (like `exec_mode`) are within valid ranges. Malformed payloads must be rejected immediately.

4. **Audit `COMM_MODULE` and `SPLIT_MODULE` Binary Safety:**
   - Ensure the `comm_rx.c` / `comm_tx.c` state machines and transport adapters (USB/BLE) rely **exclusively** on the explicit `length` headers provided by the Blast chunks.
   - Completely purge any string-based boundary checks (like `strlen()`) from the routing and parsing logic, since binary data naturally contains `0x00` null bytes anywhere.
   - Verify that `SPLIT_MODULE`'s NVS mirroring (`split_config_sync.c`) expects purely binary NVS blobs and does not implicitly rely on legacy JSON blob sizes for its fragmentation offsets.

### Configurator (TypeScript)

1. **Schema Definition & Explicit Endianness:**
   > [!WARNING]
   > TypeScript's `DataView` methods default to Big-Endian if the endianness flag is omitted.
   
   - **Source of Truth:** Create schema definitions inside the existing `COMM_PROTOCOL.md` file. You **must explicitly list the exact byte offset** for every field.
   - **Hardcoded Offsets:** The TypeScript `DataView` parsing logic must rely on these hardcoded offsets (e.g., `dataView.getUint32(12, true)`) rather than sequentially advancing an index. This guarantees it remains immune to any unforeseen C-compiler padding quirks.
   - **Endianness:** The TypeScript parsers **must explicitly pass `true`** for the little-endian parameter on every single call (e.g., `dataView.getUint32(offset, true)`). This must be strictly applied everywhere.
   - **Array Bounds:** The Configurator must respect the `count` fields during serialization (padding the rest with zeros) and deserialization (ignoring elements beyond `count`).

2. **64-Bit Integer / Bitmask Handling:**
   > [!IMPORTANT]
   > The Config Module uses 64-bit bitmasks (e.g., `mac_idx`, `ck_idx`). Standard JS numbers lose precision after 53 bits.
   
   The generated parsers MUST mandate the use of `DataView.getBigUint64()` and strictly utilize the `BigInt` type in TypeScript for these bitmasks to prevent silent truncation.

3. **UTF-8 String Decoding:** For C-strings (like the 32-byte layout names), avoid feeding uninitialized memory (garbage bytes) into the `TextDecoder`. Find the first null byte and decode only the valid slice:
   ```typescript
   const nameBuffer = new Uint8Array(dataView.buffer, offset, 32);
   const nullIdx = nameBuffer.indexOf(0);
   const validLen = nullIdx === -1 ? 32 : nullIdx;
   const name = new TextDecoder('utf-8').decode(nameBuffer.subarray(0, validLen));
   ```

4. **Device Controller Update:**
   - Update the transport layer to stop converting data to JSON strings. `DeviceController` will serialize directly to `ArrayBuffer` and dispatch to the existing binary opcodes.

## Decisions Made

> [!NOTE]
> **Decision:** We are dropping all backward compatibility with older JSON payloads. Since all devices are test devices, we will simply perform a factory reset to wipe the legacy JSON NVS data during this migration.

> [!TIP]
> **Status Push Optimizations**
> The `MODULE_STATUS` pushes (unsolicited UI updates) fire frequently (e.g., on BLE connection changes). Migrating them to a packed 6-byte binary struct and broadcasting via `CFG_CMD_STATUS` will save significant UART/BLE bandwidth and eliminate JSON overhead.

## Step-by-Step Execution Plan

### Phase 1: Struct Updates & Memory Alignment (Firmware)
- [ ] Audit data structures: `cfg_layer_t`, `cfg_macro_t`, `cfg_combo_t`, `cfg_ckey_t`.
- [ ] Reorder struct fields using Decreasing Size Ordering to eliminate implicit padding.
- [ ] Add explicit trailing `reserved` fields (e.g., `uint8_t reserved[n]`) for natural alignment.
- [ ] Ensure every struct with an array has an explicit `uint8_t count` field.
- [ ] Add `_Static_assert()` definitions in headers for `sizeof()` and critical `offsetof()` boundaries.
- [ ] Define module-specific 7-byte headers (`cfgmod_req_header_t`, `cfgmod_rsp_header_t`).
- [ ] Update payload structures (`statusmod_msg_t`, `sysmod_msg_t`) ensuring aligned access at 8-byte offsets from `s_shared_rx_buf`.

### Phase 2: Refactoring Firmware Logic & Memory Reclamation
- [ ] Remove all cJSON dependencies (`#include "cJSON.h"`) and `_serialize`/`_deserialize` logic.
- [ ] Remove "Dual-Path Storage (Auto-Upgrade)" logic in `cfgmod_get_config()`.
- [ ] Remove the 32KB PSRAM JSON framing buffer in `CONFIG_MODULE`.
- [ ] Resize `s_shared_rx_buf` and `s_shared_tx_buf` in `comm_module` to 10240 bytes (10KB).
- [ ] Refactor `cfgmod.c` handlers (`CFG_CMD_GET`, `CFG_CMD_SET`) to use in-place `memcpy()` from `s_shared_rx_buf`.
- [ ] Implement strict bounds validation for array lengths, strings (null-termination), and enums during SET commands.

### Phase 3: COMM_MODULE & SPLIT_MODULE Binary Safety
- [ ] Update `comm_rx.c` and `comm_tx.c` state machines to rely exclusively on explicit `length` headers from Blast chunks.
- [ ] Purge all string-based boundary checks (e.g., `strlen()`) from routing and parsing logic.
- [ ] Verify `SPLIT_MODULE` NVS mirroring handles purely binary blobs without expecting JSON string lengths.

### Phase 4: Configurator (TypeScript) Schema & Parsers
- [ ] Update `COMM_PROTOCOL.md` with explicit byte offsets for every struct field.
- [ ] Implement `DataView` parsing logic using hardcoded offsets (avoid sequential pointer advancement).
- [ ] Mandate passing `true` for Little-Endian parsing on every `DataView` call.
- [ ] Implement robust 64-bit integer handling (`DataView.getBigUint64()`, `BigInt` type) for bitmasks.
- [ ] Implement safe C-string UTF-8 decoding (find first null byte before using `TextDecoder`).
- [ ] Handle array bounds during serialization/deserialization based on `count` fields.

### Phase 5: Configurator Transport Update
- [ ] Update `DeviceController` to serialize objects directly to `ArrayBuffer`.
- [ ] Dispatch to the repurposed binary opcodes (`CFG_CMD_GET`, `CFG_CMD_SET`, `CFG_CMD_STATUS`).
- [ ] Update status widget parsing to handle the new 6-byte packed `MODULE_STATUS` pushes.

## Verification Plan

1. **Unit Testing (Configurator):**
   - Write Jest tests in TypeScript that construct a mock `ArrayBuffer` simulating the ESP32's C-struct memory dump for `cfg_layer_t` and `cfg_layout_index_t`.
   - Verify the TS parsers correctly reconstruct the JS objects, arrays, safely decode strings without over-reading, and successfully handle 64-bit bitmasks with `BigInt`.
2. **Hardware Integration:**
   - Factory reset test devices to ensure old JSON blob collisions are completely avoided.
   - Flash firmware with JSON and legacy paths completely removed.
   - Verify that compilation successfully passes the new `_Static_assert` checks.
   - Sync outlines and layouts via USB (WebHID) to ensure alignment panics do not occur on the ESP32.
