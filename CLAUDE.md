# Tecleados ESP Firmware — Claude Instructions

## Module Documentation

A `universe/` folder contains the authoritative knowledge base for this firmware's architecture and module interactions.

**Whenever a module is mentioned, read its documentation file before answering.**

| Module | File |
|--------|------|
| Keyboard / scanning / layers / macros | `universe/modules/KEYBOARD_MODULE.md` |
| Split / Bluetooth split link / ESP-NOW / roles | `universe/modules/SPLIT_MODULE.md` |
| BLE / NimBLE / HID peripheral / pairing / bonding | `universe/modules/BLE_MODULE.md` |
| USB / HID / NKRO / system channel | `universe/modules/USB_MODULE.md` |
| Status / state sync / Configurator | `universe/modules/STATUS_MODULE.md` |
| Config / NVS / persistent settings / GET-SET protocol | `universe/modules/CONFIG_MODULE.md` |

Start with `universe/Home.md` for a high-level overview of the whole system.

When uncertain which module is relevant, read `universe/Home.md` first — it maps the module responsibilities and cross-links.
