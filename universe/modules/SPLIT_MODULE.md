# Split Module (splitmod)

> **Source:** `components/split/` — 14 `.c` files organized in layers (see File Map).
> **Public API:** `include/splitmod.h`, `include/split_protocol.h`

The **Split Module** turns two independent ESP32 halves into a single, cohesive keyboard. It owns the low-latency wireless link, dynamic role negotiation, and the transparent proxying of matrix state, BLE control, and configuration data between Master and Slave.

Traffic rides on **ESP-NOW** for peer-to-peer communication with sub-millisecond keystroke overhead.

---

## Internal Architecture

The module uses a **Master/Slave Proxy** model where roles are negotiated at every connect. Internally it is structured as a **layered pipeline** — the public API (`splitmod.c`) is now a thin orchestrator that wires together single-responsibility sub-modules.

### 1. Dynamic Role Negotiation
Roles are decided by priority, not hardcoded to Left/Right. The algorithm in `split_role_decide()` ([split_role.c]) evaluates in strict priority order:

1. **Unsynced BLE data**: if one side has a fresh bond that hasn't been synced to the peer yet, it *must* become Master so the bond isn't lost.
2. **USB host connection**: a half physically plugged into a USB host wins Master — it is already producing HID reports via USB and the host expects them from that device.
3. **BLE host connection**: the side with an active BLE connection owns the wireless output path.
4. **Last persisted role**: the recorded role from the previous session drives continuity after a reconnect, preventing spurious inversions.
5. **MAC tiebreaker**: the side with the higher MAC address wins — fully deterministic.

Every priority uses provably antisymmetric checks: when one side returns at a given priority, the other side also returns at that same priority with the complementary role. It is therefore **impossible for both sides to end up with the same role**.

> **Implementation note**: the `proposed_role` field is still present in `split_role_negotiate_payload_t` and `split_pair_data_t` for wire compatibility but is transmitted as 0 and ignored in all role decisions.

Implemented in [split_role.c]; driven by [split_dispatch.c] on `ROLE_NEGOTIATE` reception.

### 2. Encrypted ESP-NOW Transport
All traffic is secured using **AES-128-CCM** with anti-replay:
- **Pairing**: unencrypted X25519 ECDH exchange derives a shared session key ([split_pair.c], [split_crypto.c]).
- **MIC**: every packet carries a MIC authenticated by [split_transport.c] before dispatch.
- **Replay window**: 16-bit modular sequence window in [split_session.c] — forward half accepted, rest dropped.

### 3. Fragmentation Protocol
ESP-NOW's ~240-byte payload limit means large transfers (the full config blob, macro DB, BLE bonds) are fragmented and reassembled by [split_config_sync.c]. Reassembly tracks received fragments in a **32-bit bitmap** → **max 32 fragments / ~7.2 KB per logical message**. The receiver enforces this limit; any `fragment_total > 32` is rejected with `ESP_ERR_INVALID_SIZE`.

### 4. State-Machine Task
One FreeRTOS task ([split_task.c]) drives the whole connection lifecycle at ~100 Hz:

| State          | Tick behavior                                                                                 |
| -------------- | --------------------------------------------------------------------------------------------- |
| `IDLE`         | No peer paired — waiting. No periodic transmissions.                                          |
| `PAIRING`      | Broadcast DISCOVERY every 500 ms; respect pairing deadline.                                   |
| `CONNECTING`   | Retransmit `ROLE_NEGOTIATE` every 500 ms until the peer ACKs.                                 |
| `CONNECTED`    | 150 ms heartbeat (slave→master, with master echo for RTT), bench probes (master), drain deferred config-sync work. |
| `DISCONNECTED` | Reconnect with exponential backoff (500 ms → 5 s).                                            |

The task owns all NVS-heavy / blocking work (config sync push). Event-bus and WiFi-task contexts only **request** work via two `volatile bool` flags and one `volatile uint32_t` kind-mask — they never block:

- `s_config_sync_pending` — triggers a full push after initial connect (with settle delay).
- `s_config_sync_kind_mask` — bitmask of `(1 << cfgmod_kind_t)` for incremental pushes from `on_config_updated`.
- `s_reverse_ble_sync` — peer sent stale/conflicting ble_cfg; push ours back.

---

## Layered Architecture

```
splitmod.c                     ← public API + event handler registration
   │
   ├─► split_task.c            ← main loop, timers, deferred-work pump
   │       │
   │       └─► split_dispatch.c  ← inbound message routing (per-msg handlers)
   │                │
   │                ├─► split_bridge.c   ← kb / BLE routing, BLE proxy
   │                ├─► split_bench.c    ← RTT benchmark (master probes)
   │                ├─► split_sync.c     ← remote matrix state
   │                ├─► split_config_sync.c ← fragmented NVS replication
   │                ├─► split_pair.c     ← X25519 ECDH + NVS pairing data
   │                └─► split_role.c     ← role-decision logic
   │
   ├─► split_session.c         ← all session state (state, role, MACs, seq, RSSI…)
   ├─► split_transport.c       ← ESP-NOW send/recv, AES-CCM, MIC verify
   ├─► split_crypto.c          ← X25519 + HKDF primitives
   ├─► split_protocol.c        ← shared protocol helpers
   └─► split_usb.c             ← MODULE_SPLIT commands from Configurator
```

Every file has a single responsibility. No source owns shared mutable state — that lives in `split_session.c` behind accessor functions (seq allocator protected by `portMUX_TYPE` for cross-core safety).

---

## Cross-Module Connections

### [[KEYBOARD_MODULE]] — Matrix Merging
- **Slave**: [split_bridge.c] registers `on_matrix_change` as the `kb_manager` callback; every change is serialised into `SPLIT_MSG_KEY_STATE_FULL` (always full, never delta — ESP-NOW is fire-and-forget, a dropped delta would corrupt the peer's view forever). While connected as slave, `kb_manager_set_paused(true)` suppresses local HID output.
- **Master**: [split_dispatch.c] feeds received bitmaps into `kb_manager_set_remote_matrix()`. The local scanner XOR-merges them with its own matrix.
- **Battery-adaptive scan rate**: the slave's heartbeat tick also reads the battery level and calls `kb_manager_set_scan_divisor()` (÷1 / ÷2 / ÷4 at >30 % / 10-30 % / <10 % charge) to reduce scan load at low battery.

### [[BLE_MODULE]] — Radio Management & Proxy
- **Role-based suspend**: [split_bridge.c] suspends the slave's BLE radio (`ble_hid_set_suspended(true)`) to avoid 2.4 GHz contention and host-side confusion.
- **MAC sharing**: on first master promotion `split_bridge` auto-populates `cfg_system.ble_shared_addr` from its own BT MAC. After a role swap, the new master advertises with the same address → seamless reconnect on the host.
- **BLE state handover**: when a half becomes Master (via negotiation or role swap), it calls `ble_hid_seed_handover_state(ble_connected_bitmap, selected_profile)` with the values the outgoing Master reported — so BLE connection context is transferred without dropping the host.
- **Configurator proxy**: BLE commands arriving on the slave's `MODULE_BLE` USB channel are tunneled over `SPLIT_MSG_BLE_CMD` and executed on the master (source of truth). Master pushes `SPLIT_MSG_BLE_STATUS` back to the slave for widget sync. This callback (`split_bridge_ble_usb_callback`) is registered for `MODULE_BLE` in `splitmod_init`.

### [[USB_MODULE]] — Host Interface
- `tud_mounted()` is sampled and sent in `ROLE_NEGOTIATE`; a half with an active USB-host connection wins Priority 3 of role negotiation.
- [split_usb.c] handles configurator traffic on `MODULE_SPLIT`: START_PAIRING, CANCEL_PAIRING, UNPAIR, GET_STATUS, GET_REMOTE_MATRIX, ROLE_SWAP, RUN_BENCH, GET_BENCH.

### [[CONFIG_MODULE]] — Persistent Sync
- [splitmod.c] listens for `CONFIG_EVENT_KIND_UPDATED`. Whenever a syncable key is updated, it **sets the bit** for that `cfgmod_kind_t` in `s_config_sync_kind_mask` — the event-bus task never blocks on NVS.
- [split_config_sync.c] owns fragmentation, reassembly, and ACK/retry.
- **BLE config sync direction** is role-based:
  - **Slave** always accepts `ble_cfg` from Master ("trust mode").
  - **Master** ignores `ble_cfg` received from Slave. If the version numbers differ, it triggers a reverse sync to correct the slave.
- **Bond sync guard**: Master ignores all incoming bond data from Slave (always triggers reverse sync). A Slave only accepts incoming bonds if the peer has at least as many bond records locally; otherwise it requests a reverse sync.

---

## Message Protocol

The protocol ([split_protocol.h]) uses a standardized header for all over-the-air packets.

| Type   | Name                              | Description |
|--------|-----------------------------------|-------------|
| `0x01` | `DISCOVERY`                       | Pairing broadcast beacon. Unencrypted. |
| `0x02` | `PAIR_REQUEST`                    | X25519 key exchange initiation. Unencrypted. |
| `0x03` | `PAIR_RESPONSE`                   | X25519 key exchange completion. Unencrypted. |
| `0x10` | `ROLE_NEGOTIATE`                  | Handshake that decides who is Master. |
| `0x11` | `ROLE_SWAP_REQ`                   | Request role swap. |
| `0x12` | `ROLE_SWAP_ACK`                   | Acknowledge role swap. |
| `0x20` | `KEY_STATE_FULL`                  | Full 14-byte matrix bitmap from slave. |
| `0x21` | `KEY_STATE_DELTA`                 | Incremental: only changed bytes (with bitmask). |
| `0x30` | `HEARTBEAT`                       | Slave→Master keepalive; Master echoes back unchanged `sent_us` for RTT measurement. |
| `0x31` | `DISCONNECT`                      | Graceful shutdown signal. |
| `0x40` | `CONFIG_SYNC`                     | Fragmented NVS data fragment. |
| `0x41` | `CONFIG_SYNC_ACK`                 | Config sync acknowledged by receiver. |
| `0x50` | `PING`                            | RTT benchmark probe (Master → Slave). |
| `0x51` | `PONG`                            | RTT benchmark reply (Slave echoes PING payload). |
| `0x60` | `BLE_CMD`                         | Slave → Master: forward a BLE command from USB. |
| `0x61` | `BLE_STATUS`                      | Master → Slave: push live BLE state. |

### Heartbeat RTT Measurement
The slave sends `HEARTBEAT` with `sent_us = esp_timer_get_time()`. The master **echoes the exact same `sent_us` value back** in its own heartbeat reply. When the slave receives the echo, it computes `RTT = now_us - sent_us` and stores `latency_us = RTT / 2`.

---

## Dependency Flow

```mermaid
graph LR
    subgraph Slave
        KB_S[kb_manager] -- matrix --> BR_S[split_bridge]
        BR_S -- suspends --> BLE_S[blemod]
        BR_S --> TP_S[split_transport]
    end

    subgraph Master
        TP_S -. "ESP-NOW<br/>(AES-CCM)" .-> TP_M[split_transport]
        TP_M --> DP_M[split_dispatch]
        DP_M -- remote matrix --> KB_M[kb_manager]
        KB_M -- HID --> BLE_M[blemod]
    end
```

---

## File Map

### Public API
| File | Responsibility |
|---|---|
| `splitmod.c` | Public API (`splitmod_*`), event-handler registration, init/deinit wiring. Thin orchestrator. |

### Core State & Loop
| File | Responsibility |
|---|---|
| `split_session.c` | All session state (state, role, MACs, seq counters, RSSI, latency, peer_last_seen, link_stale). Cross-core-safe seq allocator. |
| `split_task.c` | Main ~100 Hz state-machine task: IDLE / PAIRING / CONNECTING / CONNECTED / DISCONNECTED ticks + deferred-work pump. |
| `split_dispatch.c` | Inbound message dispatcher — per-type handlers, anti-replay + stale-recovery prelude. |

### Service Layers
| File | Responsibility |
|---|---|
| `split_transport.c` | Low-level ESP-NOW send/receive, AES-CCM encrypt/authenticate, peer table. WiFi STA mode with power-save disabled for minimum latency. |
| `split_crypto.c` | X25519 ECDH + HKDF primitives. |
| `split_pair.c` | Pairing FSM (DISCOVERY → PAIR_REQUEST → PAIR_RESPONSE), pairing data in NVS. |
| `split_role.c` | Role-decision logic (`split_role_decide`, `split_role_on_negotiate`, `split_role_on_swap_req/ack`). Persists last role. |
| `split_sync.c` | Remote matrix state + `KEY_STATE_FULL/DELTA` serialisation. Mutex-protected for WiFi-task / kb-task cross-core access. |
| `split_config_sync.c` | Fragment + reassemble NVS entries over ESP-NOW. 32-fragment (7.2 KB) max. Role-aware BLE/bond sync guards. |
| `split_bridge.c` | kb_manager + BLE routing for the current role; BLE proxy (cmd from slave, status to slave); power management on role transitions. |
| `split_bench.c` | RTT benchmark — master sends PING probes, records RTT on PONG. |
| `split_usb.c` | `MODULE_SPLIT` USB commands from the Configurator. |
| `split_protocol.c` | Shared protocol helpers (header (de)serialise). |

---

## Concurrency & Safety Notes

- **Cross-core seq allocator**: `split_session_next_seq()` uses `portMUX_TYPE` critical section. The transport TX context, event-bus task, and WiFi RX task all mint seq numbers; without the mutex two cores could hand out the same seq and poison the anti-replay window.
- **Deferred work**: `on_config_updated` runs on the event-bus task. It must not call `split_config_sync_push` directly — that path does `vTaskDelay(10 ms)` per-fragment retry and would starve the bus. Instead it sets a bit in `s_config_sync_kind_mask`; `split_task` drains it.
- **Anti-replay**: dropped replay frames are *not* logged at INFO — a flapping link would spam the log. Use `ESP_LOGD` for visibility during debugging.
- **NVS from split_task**: the task stack must live in **internal DRAM** (`MALLOC_CAP_INTERNAL`). NVS writes disable the SPI cache; a SPIRAM-backed stack would crash mid-write.
- **Remote matrix mutex**: `split_sync.c` uses `portMUX_TYPE` for `s_remote` because the WiFi task writes it (on `KEY_STATE_FULL/DELTA` reception) while the keyboard-manager task reads it.

---

## Recent Changes (2026-04)

- **Clean-code refactor**: `splitmod.c` split from 1305 → 259 lines across 7 new modules (session, task, dispatch, bridge, bench, usb + keeps existing transport/pair/role/sync/config_sync/crypto/protocol). No functional changes.
- **Config-sync capacity fix**: reassembly bitmap widened from `uint8_t` → `uint32_t`. Previously `1u << idx` with `idx ≥ 8` was UB and silently corrupted multi-fragment pushes once any payload exceeded 8 fragments (≈1.8 KB). New cap: 32 fragments / ≈7.2 KB; oversize messages are rejected at fragment-0 with `ESP_ERR_INVALID_SIZE`.
- **BLE/Bond sync guards**: config sync now enforces role-based ownership — Master ignores BLE config and bond data received from Slave, triggering corrective reverse syncs instead.
- **BLE state handover**: `ble_hid_seed_handover_state()` is called when a half becomes Master to transfer BLE connection context from the former master, preventing spurious disconnects on the host side.
- **Battery-adaptive scan rate**: slave-side heartbeat tick now reads battery level and adjusts `kb_manager_set_scan_divisor` to reduce power draw at low charge.
- **USB priority bug fix**: `own_usb_connected` / `peer_usb_connected` were being collected and transmitted in `ROLE_NEGOTIATE` but never consulted in `split_role_decide()`. Priority 2 (USB) is now implemented — a half with a live USB host wins Master over BLE-only or disconnected peers.
- **Default connectivity mode changed to USB**: `cfg_ble.c` previously defaulted `ble_routing_enabled = true` on a fresh flash. Changed to `false` so a keyboard boots in USB mode and BLE must be explicitly enabled.
- **Role preference removed**: the `proposed_role` / `preferred_role` system was broken — it was not antisymmetric, meaning both halves could resolve to the same role. The preference check has been removed from `split_role_decide()`. The wire field is retained (sent as 0) for compatibility. The `last_role` priority has also been rewritten with explicit mirror checks that guarantee both sides always resolve at the same priority level, making dual-same-role impossible by construction.
