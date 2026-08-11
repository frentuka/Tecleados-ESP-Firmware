# COMM Module (`comm_module`)

> **Source:** `components/comm_module/`
> **Public API:** `include/comm_transport.h`, `include/comm_dispatch.h`, `include/comm_send.h`

The `comm_module` is the **transport-agnostic protocol engine** of the keyboard. It provides a reliable, ordered, flow-controlled data pipe for the configurator to read and write configuration, control the split link, manage BLE profiles, and query device status.

It implements the [Blast+Reconcile protocol](file:///home/srleg/Projects/Tecleados-ESP-Firmware/COMM_PROTOCOL.md) natively and supports multiple simultaneous physical transports (e.g., USB and BLE). 

---

## Directory Structure

```
components/comm_module/
├── CMakeLists.txt
├── COMM_MODULE.md           
├── comm_crc.c               # CRC-8 implementation
├── comm_dispatch.c          # Callback registry + processing queues
├── comm_rx.c                # RX state machine (Blast Receive)
├── comm_tx.c                # TX state machine (Blast Transmit)
├── comm_send.c              # Cross-transport single packet sender
├── comm_transport.c         # Transport adapter registry
├── comm_session.c           # Exclusive Session Lock manager
└── include/
    ├── comm_defs.h          # Protocol constants, flags, packet header, module IDs
    ├── comm_crc.h
    ├── comm_dispatch.h
    ├── comm_rx.h
    ├── comm_tx.h
    ├── comm_send.h
    ├── comm_transport.h
    └── comm_session.h
```

---

## Transport Abstraction

The core strength of the `comm_module` is that it doesn't know anything about USB or BLE. It defines a clean `comm_transport_ops_t` interface.

Physical transports (like `usb_module` and `ble_module`) register themselves at boot:
```c
comm_transport_register(COMM_TRANSPORT_USB, &usb_ops);
comm_transport_register(COMM_TRANSPORT_BLE, &ble_ops);
```

When a transport receives raw bytes from its physical medium (HID out endpoint, GATT write, etc.), it forwards them to the engine:
```c
comm_transport_receive_packet(COMM_TRANSPORT_BLE, raw_data, length);
```

The protocol engine handles everything else: variable packet length validation, CRC checking, Blast-mode chunking, and routing.

---

## Exclusive Session Lock & Memory Management

Configuring a keyboard involves transferring complex binary structs. To avoid catastrophic heap fragmentation, the protocol engine uses **Single Global Static Buffers**:

- `s_shared_rx_buf[10240]`
- `s_shared_tx_buf[10240]`

Because these buffers are shared across all transports, the engine enforces an **Exclusive Session Lock** (`comm_session.c`). 

When a multi-packet (Blast) transfer begins from Transport A, the session manager locks the COMM channel. If Transport B tries to send a packet while Transport A holds the lock, the packet is immediately rejected with `PAYLOAD_FLAG_ERR` (BUSY). The lock is automatically released upon transfer completion or a 1000ms watchdog timeout.

This guarantees absolute safety and zero heap fragmentation, regardless of how many configurators attempt to connect simultaneously.

---

## Callback Registry

Once an incoming payload is fully received (either sequentially or via Blast+Reconcile), the `comm_dispatch` task looks at the first byte of the payload: the **Module ID**.

It routes the payload to the appropriate registered callback. Modules register themselves at boot:

```c
// cfgmod.c
comm_register_callback(MODULE_CONFIG, cfg_usb_callback);

// kb_manager.c
comm_register_callback(MODULE_SYSTEM, kb_system_usb_callback);
```

If a module needs to send a response back to the configurator, it calls:
```c
comm_send_payload(comm_get_current_source(), response_data, response_len);
```
This safely routes the response back out the exact transport that initiated the request.

---

## Unsolicited State Pushes (Broadcast)

The `comm_module` supports broadcasting. When `statusmod` wants to push a state update (e.g., a new BLE profile connected) to all active configurators without being asked, it sends:

```c
comm_send_payload(COMM_TRANSPORT_BROADCAST, status_data, len);
```

The engine internally iterates all connected transports (transports that have confirmed a live configurator client) and enqueues the payload for each.
