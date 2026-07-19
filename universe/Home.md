#  Universe Home

Welcome to the **Tecleados ESP Firmware Documentation**. This Obsidian vault serves as the centralized knowledge base for the firmware architecture, module interactions, and project-wide context.

---
## Modules

The firmware is divided into highly specific modules that run in the `components/` directory. Explore them below to understand the big picture:

- [KEYBOARD_MODULE](file:///home/srleg/Projects/Tecleados-ESP-Firmware/universe/modules/KEYBOARD_MODULE.md) - The central hub: architecture of the 1000Hz scanning engine, layer logic, and macro processing.
- [SPLIT_MODULE](file:///home/srleg/Projects/Tecleados-ESP-Firmware/universe/modules/SPLIT_MODULE.md) - Explains the architecture, connections, and magic behind split keyboard Bluetooth/Esp-Now linking and roles proxy.
- [BLE_MODULE](file:///home/srleg/Projects/Tecleados-ESP-Firmware/universe/modules/BLE_MODULE.md) - Explains the NimBLE HID peripheral stack: advertising, pairing, bonding, and all its connections to the keyboard, split, config, USB, and status modules.
- [USB_MODULE](file:///home/srleg/Projects/Tecleados-ESP-Firmware/universe/modules/USB_MODULE.md) - Details the bidirectional HID communication, NKRO/Boot protocol switching, and the custom system channel for real-time configuration.
- [STATUS_MODULE](file:///home/srleg/Projects/Tecleados-ESP-Firmware/universe/modules/STATUS_MODULE.md) - The state aggregator: manages real-time synchronization of BLE, Split, and USB states for the Configurator.
- [CONFIG_MODULE](file:///home/srleg/Projects/Tecleados-ESP-Firmware/universe/modules/CONFIG_MODULE.md) - The single source of truth for persistent settings, NVS storage, and the USB wire protocol GET/SET handlers.
- [CONFIGURATOR](file:///home/srleg/Projects/Tecleados-ESP-Firmware/universe/modules/CONFIGURATOR.md) - The browser-based WebHID configurator: layout editor, KLE import, macro editor, split management, and the Blast+Reconcile transport that talks to all of the above.

