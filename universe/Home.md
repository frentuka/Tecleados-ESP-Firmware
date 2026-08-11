#  Universe Home

Welcome to the **Tecleados ESP Firmware Documentation**. This Obsidian vault serves as the centralized knowledge base for the firmware architecture, module interactions, and project-wide context.

---
## Modules

The firmware is divided into highly specific modules that run in the `components/` directory. Explore them below to understand the big picture:

- [KEYBOARD_MODULE](modules/KEYBOARD_MODULE.md) - The central hub: architecture of the 1000Hz scanning engine, layer logic, and macro processing.
- [SPLIT_MODULE](modules/SPLIT_MODULE.md) - Explains the architecture, connections, and magic behind split keyboard Bluetooth/Esp-Now linking and roles proxy.
- [BLE_MODULE](modules/BLE_MODULE.md) - Explains the NimBLE HID peripheral stack: advertising, pairing, bonding, and the Custom GATT COMM service.
- [USB_MODULE](modules/USB_MODULE.md) - Details the physical USB HID bridging for NKRO/Boot protocols and the COMM transport adapter.
- [COMM_MODULE](modules/COMM_MODULE.md) - The transport-agnostic protocol engine for wireless and wired configuration via Blast+Reconcile.
- [STATUS_MODULE](modules/STATUS_MODULE.md) - The state aggregator: manages real-time synchronization of BLE, Split, and USB states for the Configurator.
- [CONFIG_MODULE](modules/CONFIG_MODULE.md) - The single source of truth for persistent settings, NVS storage, and the COMM GET/SET handlers.
- [CONFIGURATOR](modules/CONFIGURATOR.md) - The browser-based Configurator (WebHID and Web Bluetooth): layout editor, KLE import, macro editor, and split management.

