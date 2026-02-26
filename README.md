# ESP32 Keyboard Firmware

This is a custom keyboard firmware written in C for the ESP32 (specifically targeting the ESP32-S3). It's built directly on top of the ESP-IDF and handles both USB and Bluetooth (BLE) connections.

## What it does

- **USB & Bluetooth LE:** Connect via USB, Bluetooth or both.
- **Custom Matrix:** Scans the physical key switches and handles debouncing.
- **RGB Feedback:** Uses the onboard WS2812 LED to show you what's going on (like lighting up red when Caps Lock is on).
- **NKRO:** Supports N-Key Rollover.
- **JSON Configs:** System configurations and layouts are handled via a central config module.

## `components/` directory:

- `keyboard/` - The brain. Matrix scanning, layout mapping, and state tracking (like keeping track of whether Caps Lock is active).
- `usb_module/` - TinyUSB stack for acting like a standard wired keyboard.
- `ble_module/` - Bluetooth Low Energy HID implementation using NimBLE protocol.
- `rgb_module/` - Drives the smart RGB LED using the ESP's RMT peripheral.
- `config_module/` - Handles saving and loading configuration data.
- `button_module/` - Handles the onboard BOOT button for quick testing and macros.

## To-do

- [ ] Implement VIA-like configuration interface.
- [ ] Add battery support.
- [ ] Add 2.4GHz dongle wireless connectivity.
- [ ] Add host-slave communication support for split keyboards.
- [ ] Add QMK firmware compatibility.
- [ ] Add OLED display support.