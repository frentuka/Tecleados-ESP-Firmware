# BLE COMM Implementation Review — Phase 0 & Phase 1

> **Verdict: Solid foundation with critical integration gaps in Phase 1.**
> Phase 0 is well-executed and structurally complete. Phase 1 created the right files with correct logic, but left them **unwired** — `blemod.c` has zero integration hooks, and `sdkconfig.defaults` is only partially updated. The code quality is high; the remaining work is mostly plumbing.

---

## Scoring Summary

| Area | Phase 0 | Phase 1 |
|------|:-------:|:-------:|
| Architecture & Design Fidelity | ✅ Excellent | ✅ Excellent |
| Code Quality | ✅ Clean | ✅ Clean |
| Plan Adherence | ✅ Complete | ⚠️ Partial |
| Integration / Wiring | ✅ Working | 🔴 **Not wired** |
| Build & Verification | ⬜ Untested | ⬜ Untested |

---

## Phase 0 — `comm_module` Extraction

### ✅ What Was Done Well

1. **Clean extraction.** The protocol engine was correctly separated from `usb_module` into `components/comm_module/`. All 9 source files + 9 headers are present and well-organized:
   - [comm_defs.h](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/include/comm_defs.h) — Protocol types, flags, `comm_packet_header_t`
   - [comm_transport.h](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/include/comm_transport.h) / [.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/comm_transport.c) — Transport abstraction
   - [comm_session.h](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/include/comm_session.h) / [.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/comm_session.c) — Blast-only exclusive lock
   - [comm_dispatch.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/comm_dispatch.c) — Processing task, callback routing, timeout task
   - [comm_rx.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/comm_rx.c) — RX state machine with blast mode
   - [comm_tx.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/comm_tx.c) — TX queue/task with blast mode
   - [comm_send.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/comm_send.c) — Transport-routed packet send
   - [comm_crc.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/comm_crc.c) — Dynamic-length CRC-8
   - [comm_module.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/comm_module.c) — Public API facade

2. **Transport abstraction works.** The `comm_transport_ops_t` vtable pattern is clean. USB correctly registers via `comm_transport_register(COMM_TRANSPORT_USB, &s_usb_ops)` in [usbmod.c:201](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/usb_module/usbmod.c#L201).

3. **Consumer migration complete.** All 5 consumer modules have been updated:
   - [cfgmod.c:699](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/config_module/cfgmod.c#L699) — `comm_register_module(MODULE_CONFIG, ...)`
   - [statusmod.c:214](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/status_module/statusmod.c#L214) — `comm_register_module(MODULE_STATUS, ...)`
   - [splitmod.c:154-155](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/split/splitmod.c#L154-L155) — `MODULE_SPLIT` + `MODULE_BLE`
   - [kb_manager.c:485](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/keyboard/kb_manager.c#L485) — `MODULE_SYSTEM`

4. **`comm_send_message` API.** Nice addition not in the original plan — wraps module ID prepending and `comm_send_payload`. Used correctly by consumers passing `source` for request-response and `COMM_TRANSPORT_BROADCAST` for status pushes.

5. **Init order correct.** [main.c:65](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/main/main.c#L65) calls `comm_init()` before `usb_init()`.

6. **Zero hardware dependencies.** [CMakeLists.txt](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/CMakeLists.txt) — `REQUIRES freertos esp_timer` only. No TinyUSB, no NimBLE. ✅

7. **Session lock is clean.** Mutex-protected, blast-only scope, idempotent re-lock by same transport.

8. **Variable packet sizing.** `COMM_MAX_PACKET_SIZE = 260` defined. CRC functions accept dynamic `len`. The `tx_get_max_payload_len()` and `rx_get_max_payload_len()` helpers query the transport's `get_max_packet_size()` at runtime. ✅

---

### ⚠️ Issues & Improvement Areas (Phase 0)

#### 1. 🟡 Public API Naming Divergence from Plan

The plan specified `comm_register_callback()` and `comm_send_payload()` as the public API. The implementation added a wrapper layer:
- `comm_register_module()` → `comm_register_callback()` (forwarding wrapper)
- `comm_send_message()` → `comm_send_payload()` (prepends module ID)

This is actually an **improvement** — the module-ID prepending encapsulation is cleaner. But the plan's `comm_module.h` signature is different from what was implemented. Document the deviation.

> [!NOTE]
> The naming is fine, but `comm_get_current_source()` is declared in [comm_dispatch.h](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/include/comm_dispatch.h#L11) but **not** re-exported in [comm_module.h](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/include/comm_module.h). Consumers who need to know the source transport would have to `#include "comm_dispatch.h"` directly, breaking the single-entry-point pattern. The plan explicitly required this in `comm_module.h`.

**Recommendation:** Add `comm_get_current_source()` to `comm_module.h`, or at minimum `#include "comm_dispatch.h"` from it.

#### 2. 🟡 `comm_session.h` Not Re-exported

The plan specified `comm_module.h` should include `comm_session.h` for transport disconnect cleanup (`comm_session_get_active()` / `comm_session_unlock()`). The current [comm_module.h](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/include/comm_module.h) only includes `comm_defs.h` and `comm_transport.h`. 

This forces the BLE transport adapter to directly `#include "comm_session.h"`, which it does — so it works, but it breaks the facade pattern.

#### 3. 🟡 `COMM_MAX_PAYLOAD_LENGTH` as a Macro Calling a Function

In both [comm_rx.c:30](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/comm_rx.c#L30) and [comm_tx.c:36](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/comm_tx.c#L36):
```c
#define COMM_MAX_PAYLOAD_LENGTH (rx_get_max_payload_len())
#define COMM_MAX_PAYLOAD_LENGTH (tx_get_max_payload_len())
```

This is a **macro that invokes a function call on every use**, which is misleading because macros typically expand to constants. While functionally correct (variable MTU demands dynamic sizing), it creates a readability trap — a reader seeing `COMM_MAX_PAYLOAD_LENGTH` in a tight loop may assume it's a compile-time constant.

**Recommendation:** Replace the macro with a direct inline function call. Use `tx_get_max_payload_len()` and `rx_get_max_payload_len()` directly at call sites. The macro adds no value and obscures behavior.

#### 4. 🟡 Blast Bitmap Offset Uses Dynamic `COMM_MAX_PAYLOAD_LENGTH`

In [comm_rx.c:88](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/comm_rx.c#L88):
```c
uint16_t offset = index * COMM_MAX_PAYLOAD_LENGTH;
```

This computes the offset into `s_rx_buf` based on the **current** max payload length. But during blast mode, the max payload length is determined at the start of the blast (when the FIRST packet arrives and the session is locked). If the MTU were to change mid-blast (unlikely but theoretically possible via `BLE_GAP_EVENT_MTU`), the offset calculation would produce wrong results.

**Recommendation:** Cache the `max_payload_len` at blast start time in the `comm_rx_session_t` state and use that cached value for all subsequent index-to-offset conversions during the blast. Same applies to [comm_tx.c:143](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/comm_tx.c#L143).

#### 5. 🟡 TX Task Acquires Session Lock for Every Send

In [comm_tx.c:311](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/comm_tx.c#L311):
```c
if (!comm_session_try_lock(item.target)) {
```

The TX task attempts `comm_session_try_lock()` for every dequeued item — even single-packet sends. The plan stated the session lock is **blast-only**. Single-packet TX operations should not acquire the lock.

**Impact:** If an RX blast is in progress from Transport A, and the processing task dispatches a callback that enqueues a single-packet TX reply to Transport A, the TX task will attempt to lock the session. It succeeds (same transport), but if it were a reply to Transport B, it would fail and drop the reply unnecessarily.

**Recommendation:** Only call `comm_session_try_lock()` when `total_packets > 1` (blast TX). For single-packet sends, skip the lock entirely.

#### 6. 🟢 Minor: `extern MessageBufferHandle_t` Coupling

[comm_transport.c:14](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/comm_transport.c#L14):
```c
extern MessageBufferHandle_t s_comm_message_buffer;
```

This `extern` directly accesses `comm_dispatch.c`'s internal state. A cleaner pattern would be exposing a `comm_dispatch_enqueue()` function. Not a blocker — it works — but it creates a tight coupling between two implementation files.

---

## Phase 1 — BLE COMM GATT Service

### ✅ What Was Done Well

1. **GATT service definition is correct.** [ble_comm_service.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/ble_module/ble_comm_service.c) defines all 4 UUIDs (service, RX, TX, MTU), all 3 characteristics with correct flags:
   - RX: `WRITE | WRITE_NO_RSP | WRITE_ENC` ✅
   - TX: `READ | NOTIFY | READ_ENC` ✅
   - MTU: `READ | NOTIFY | READ_ENC` ✅
   - `val_handle` properly captured for TX and MTU handles ✅

2. **UUID byte ordering is correct.** The 128-bit UUIDs are correctly represented in little-endian for NimBLE's `BLE_UUID128_INIT`. Matches the plan's hex strings. ✅

3. **RX access callback is clean.** [ble_comm_service.c:46-61](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/ble_module/ble_comm_service.c#L46-L61) — Correctly copies from mbuf, calls `comm_transport_receive_packet()`, and frees the copy. No heavy processing in the NimBLE host task context. ✅

4. **Transport adapter is faithful to plan.** [ble_comm_transport.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/ble_module/ble_comm_transport.c) implements all required functions:
   - `ble_comm_send_packet()` with mbuf retry/timeout ✅
   - `ble_comm_is_ready()` checks conn handle, subscription, and suspension ✅
   - `ble_comm_get_max_packet_size()` with `mtu - 3`, capped at 260 ✅
   - `ble_comm_set_conn_handle()` first-come-first-served ✅
   - `ble_comm_set_subscribed()` with duplicate rejection ✅
   - `ble_comm_on_disconnect()` with session unlock ✅
   - `ble_comm_on_mtu_change()` with MTU notification ✅
   - `ble_comm_reset_state()` for suspension cleanup ✅

5. **mbuf ownership handled correctly.** The comment in [ble_comm_transport.c:29-30](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/ble_module/ble_comm_transport.c#L29-L30) about `ble_gatts_notify_custom` consuming the mbuf is critical NimBLE knowledge. ✅

6. **Source files added to CMakeLists.** [CMakeLists.txt](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/ble_module/CMakeLists.txt) includes both `ble_comm_service.c` and `ble_comm_transport.c`. ✅

---

### 🔴 Critical Issues (Phase 1)

#### 1. 🔴 `blemod.c` Has ZERO Integration Hooks

This is the **most critical issue**. The plan (Section 1.5) specifies 6 hooks that must be added to `blemod.c`:

| Hook | Where | Status |
|------|-------|--------|
| `ble_comm_svc_register()` | `ble_hid_init()` | ❌ **Missing** |
| `ble_comm_transport_init()` | `ble_hid_init()` | ❌ **Missing** |
| `ble_comm_set_conn_handle()` | `BLE_GAP_EVENT_CONNECT` | ❌ **Missing** |
| `ble_comm_on_disconnect()` | `BLE_GAP_EVENT_DISCONNECT` | ❌ **Missing** |
| `ble_comm_set_subscribed()` | `BLE_GAP_EVENT_SUBSCRIBE` | ❌ **Missing** |
| `ble_comm_on_mtu_change()` | `BLE_GAP_EVENT_MTU` | ❌ **Missing** |
| `ble_comm_reset_state()` | `ble_hid_set_suspended()` | ❌ **Missing** |

**Without these hooks, the COMM service is registered in the GATT database but completely non-functional.** No connection handle is ever set, no subscription is ever tracked, no disconnect cleanup happens, and the transport adapter's `is_ready()` always returns `false`.

**This means Phase 1 Step 3 ("Update blemod.c") was NOT completed, despite being checked off in the task list.**

#### 2. 🔴 `comm_module` Not in BLE Module Dependencies

[CMakeLists.txt](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/ble_module/CMakeLists.txt):
```cmake
REQUIRES esp_timer driver freertos bt nvs_flash esp_event keyboard event_bus battery_module
```

**`comm_module` is missing from `REQUIRES`.** Both `ble_comm_service.c` and `ble_comm_transport.c` include `comm_transport.h`, `comm_session.h`, and `comm_defs.h` from `comm_module`. This will cause a **build failure** because the include paths won't resolve.

**Fix:** Add `comm_module` to the `REQUIRES` list.

#### 3. 🟡 `ble_gatts_add_dynamic_svcs` vs `ble_gatts_add_svcs`

[ble_comm_service.c:114](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/ble_module/ble_comm_service.c#L114):
```c
rc = ble_gatts_add_dynamic_svcs(comm_svc_defs);
```

This API is for **dynamically adding services after the GATT server is already started**. However, `ble_comm_svc_register()` should be called from `ble_hid_init()`, which runs **before** the NimBLE host starts. The standard pattern (used by `ble_hid_service.c`) is:

```c
ble_gatts_count_cfg(svc_defs);
ble_gatts_add_svcs(svc_defs);
```

`ble_gatts_add_dynamic_svcs` was likely chosen to avoid modifying the existing `ble_hid_service.c` service table, but it requires the host to be running. Need to verify when `ble_comm_svc_register()` is actually called in the boot sequence.

**Recommendation:** If called during `ble_hid_init()` (before host start), use `ble_gatts_add_svcs()`. If called later (after `nimble_port_run()`), `ble_gatts_add_dynamic_svcs()` is correct but requires `ble_gatts_start()` afterward.

---

### ⚠️ Improvement Areas (Phase 1)

#### 4. 🟡 sdkconfig.defaults Partially Updated

The plan specifies 3 changes to `sdkconfig.defaults`:

| Config | Plan Value | Actual Value | Status |
|--------|-----------|-------------|--------|
| `CONFIG_BT_NIMBLE_MAX_CCCDS` | → 21 | 21 | ✅ |
| `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU` | → 256 | 256 | ✅ |
| `CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT` | → 28 | 28 | ✅ |

All three sdkconfig values are present and correct. ✅

The comments in the sdkconfig still reference the old CCCD count reasoning ("5 × 3 connections with a small buffer") but the value is already updated to 21. The comment should be updated to explain the COMM service's additional CCCDs.

#### 5. 🟡 MTU Notification Without Subscription Check

[ble_comm_transport.c:103-111](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/ble_module/ble_comm_transport.c#L103-L111):
```c
void ble_comm_on_mtu_change(uint16_t conn_handle, uint16_t mtu) {
    if (s_comm_conn_handle == conn_handle) {
        // ... sends notification without checking subscription
    }
}
```

This sends a notification to the MTU characteristic without checking whether the client has subscribed (CCCD enabled). `ble_gatts_notify_custom` on an unsubscribed characteristic will return an error code, but it wastes an mbuf allocation attempt.

**Recommendation:** Add a `s_mtu_subscribed` flag (set via `BLE_GAP_EVENT_SUBSCRIBE` on the MTU handle) and check it before sending.

#### 6. 🟡 No `ble_comm_get_max_packet_size` Safety Floor

[ble_comm_transport.c:49-53](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/ble_module/ble_comm_transport.c#L49-L53):
```c
uint16_t max_size = ble_att_mtu(s_comm_conn_handle) - 3;
return max_size > 260 ? 260 : max_size;
```

If the negotiated MTU is very small (e.g., 23 — the BLE default), `max_size = 20`. This is technically valid (5 bytes framing + 15 bytes payload), but there's no safety floor. If the MTU is somehow 3 or less (malformed), `max_size` would underflow (unsigned subtraction → wrap to 65533).

**Recommendation:** Add `if (max_size < 5) return 5;` — the minimum viable packet is header(4) + CRC(1).

#### 7. 🟢 Minor: `malloc` in RX Callback

[ble_comm_service.c:51](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/ble_module/ble_comm_service.c#L51):
```c
uint8_t *data = malloc(len);
```

This `malloc` runs in the NimBLE host task context. For packets up to 260 bytes, this is fine, but if heap is fragmented, it could fail silently (the `NULL` check is present). Consider using a stack-allocated VLA like `comm_transport_receive_packet` does, since `len` is bounded by MTU (max ~260 bytes):

```c
uint8_t data[len];  // VLA, allocates on NimBLE task stack
os_mbuf_copydata(ctxt->om, 0, len, data);
comm_transport_receive_packet(COMM_TRANSPORT_BLE, data, len);
```

This avoids heap allocation entirely. NimBLE's default task stack should have room for ~260 bytes.

---

## Cross-Cutting Observations

### Architecture Quality: 🟢 Excellent

The decomposition follows the plan faithfully. The dependency graph is clean:
```
comm_module ← usb_module (transport registration)
comm_module ← ble_module (transport registration)
comm_module ← cfgmod, statusmod, splitmod, keyboard (callback registration)
```

No circular dependencies. `comm_module` depends only on `freertos` and `esp_timer`.

### Thread Safety: 🟢 Sound

- MessageBuffer for variable-length packet queueing ✅
- Mutex-protected session lock for blast exclusion ✅
- Non-blocking TX enqueue pattern ✅
- `s_current_source` scoped to processing task ✅
- `s_connected[]` atomic bool writes without lock ✅

### Memory Budget: 🟢 On Target

- 43 KB shared static buffers (unchanged from legacy) ✅
- No per-transport buffer duplication ✅
- Additional ~1.3 KB for NimBLE resources ✅

---

## Action Items Summary

### Must Fix Before Phase 1 Can Be Verified

| # | Priority | File | Issue |
|---|----------|------|-------|
| 1 | 🔴 Critical | [blemod.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/ble_module/blemod.c) | Add all 7 COMM integration hooks (service register, transport init, connect, disconnect, subscribe, MTU, suspend) |
| 2 | 🔴 Critical | [CMakeLists.txt](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/ble_module/CMakeLists.txt) | Add `comm_module` to `REQUIRES` |
| 3 | 🟡 Medium | [ble_comm_service.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/ble_module/ble_comm_service.c) | Verify `ble_gatts_add_dynamic_svcs` vs `ble_gatts_add_svcs` based on call timing |

### Should Fix (Correctness / Robustness)

| # | Priority | File | Issue |
|---|----------|------|-------|
| 4 | 🟡 Medium | [comm_rx.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/comm_rx.c) / [comm_tx.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/comm_tx.c) | Cache `max_payload_len` at blast start to prevent mid-blast MTU drift |
| 5 | 🟡 Medium | [comm_tx.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/comm_tx.c) | Skip session lock for single-packet TX (plan says blast-only) |
| 6 | 🟡 Medium | [ble_comm_transport.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/ble_module/ble_comm_transport.c) | Add safety floor to `ble_comm_get_max_packet_size()` |
| 7 | 🟡 Medium | [ble_comm_transport.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/ble_module/ble_comm_transport.c) | Check MTU subscription before sending MTU notification |

### Nice to Have (Code Quality)

| # | Priority | File | Issue |
|---|----------|------|-------|
| 8 | 🟢 Low | [comm_module.h](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/include/comm_module.h) | Re-export `comm_get_current_source()` and `comm_session.h` |
| 9 | 🟢 Low | [comm_rx.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/comm_rx.c) / [comm_tx.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/comm_tx.c) | Replace `COMM_MAX_PAYLOAD_LENGTH` macro with direct function calls |
| 10 | 🟢 Low | [comm_transport.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/comm_module/comm_transport.c) | Replace `extern MessageBufferHandle_t` with `comm_dispatch_enqueue()` |
| 11 | 🟢 Low | [ble_comm_service.c](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/components/ble_module/ble_comm_service.c) | Replace `malloc` with stack VLA in RX callback |
| 12 | 🟢 Low | [sdkconfig.defaults](file:///c:/appcrap/Antigravity/Tecleados-ESP-Firmware/sdkconfig.defaults) | Update CCCD comment to explain COMM service additions |

---

## Conclusion

Phase 0 is **production-quality work** — the extraction is clean, consumer migration is complete, and the transport abstraction is well-designed. The deviations from the plan (API naming, `comm_send_message` wrapper) are improvements, not regressions.

Phase 1 is **architecturally correct but incomplete**. The GATT service and transport adapter are well-implemented in isolation, but the critical `blemod.c` integration that makes them functional was never done. This is the #1 blocker for Phase 1 verification.

Once the 3 critical items are resolved, Phase 1 should be verifiable with nRF Connect (service discovery, write, notification). The medium-priority items should be addressed before Phase 2 to prevent subtle bugs under real-world BLE conditions.
