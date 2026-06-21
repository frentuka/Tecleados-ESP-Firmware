# Configurator — Technical Documentation

A React 19 + TypeScript + Vite web app for configuring an ESP32-based programmable keyboard over WebHID. Runs entirely in the browser — no server, no install required.

---

## 1. Overview

The configurator lets you:
- Edit key mappings across 4 independent layers (Base, FN1, FN2, FN3)
- Create, edit, and delete macros with rich action types and execution modes
- Create, edit, and delete custom keys (PressRelease and MultiAction modes)
- Import physical layouts from KLE (Keyboard Layout Editor) JSON
- Export and import full layouts, macros, and custom keys as portable JSON files
- Monitor raw HID communication in Developer Mode
- Receive non-intrusive in-app feedback for every device operation via the global notification system

### Getting started

```sh
cd configurator
npm install
npm run dev        # Vite dev server at https://localhost:5173 (Runs over local HTTPS using @vitejs/plugin-basic-ssl for local testing of secure WebHID access)
npm run build      # TypeScript check + Vite production build
npm run preview    # Serve the production build locally
```

The app requires a Chromium-based browser (Chrome, Edge, Opera) — Firefox does not support WebHID. Since WebHID is restricted to Secure Contexts, local development is served over a temporary HTTPS server using `@vitejs/plugin-basic-ssl` exclusively for local testing purposes. When loading the local server, accept the browser's self-signed certificate warning to proceed. This self-signed setup is for testing only; any public/production deployments must use a standard, trusted SSL/TLS certificate (e.g., from Let's Encrypt).

---

## 2. Architecture

```
Browser
  └── React UI (App.tsx)
        ├── Zustand Stores         — global state (device, macros, custom keys, logs, notifications)
        ├── React Hooks            — local business logic (useMacros, useCustomKeys)
        └── DeviceController       — high-level typed API
              └── HIDTransport     — low-level WebHID transport
                    └── WebHID API — USB HID connection to the keyboard
                          └── ESP32 firmware (USB HID device, VID 0x303A / PID 0x1324)
```

### Layer responsibilities

| Layer            | File                           | Responsibility                                                       |
|------------------|--------------------------------|----------------------------------------------------------------------|
| React UI         | `App.tsx`, dashboards, modals  | Rendering, user interaction, sidebar navigation, settings/console modals |
| Zustand stores   | `stores/`                      | Global state shared across components                                |
| React hooks      | `hooks/`                       | Local async device operations with their own state                   |
| DeviceController | `services/DeviceController.ts` | Typed commands — fetchStatus, fetchMacros, saveMacro, etc.           |
| HIDTransport     | `services/HIDTransport.ts`     | Packet building, CRC-8, Blast+Reconcile TX/RX, task queue, reconnect |
| WebHID           | browser API                    | USB HID report send/receive                                          |

---

## 3. File & Directory Map

```
configurator/
├── src/
│   ├── App.tsx                         — Top-level: single-page layout, sidebar state, secret code listener, connection logic
│   ├── Sidebar.tsx                     — Right-edge icon rail + expandable panel (Macros/CKeys/Combos tabs, Settings/Console buttons)
│   ├── SettingsModal.tsx               — Device settings modal (wraps DeviceDashboard in a portal overlay)
│   ├── DevConsoleModal.tsx             — Developer console log viewer modal (replaces DevControlsPanel bottom strip)
│   ├── main.tsx                        — React entry point, DeviceController instantiation
│   ├── index.css                       — Global styles, dashboard layouts, utility classes, pointer isolation
│   ├── HIDService.ts                   — Backward-compat re-export façade (singleton instance)
│   ├── KeyDefinitions.ts               — HID keycodes, key names, browser key→HID map
│   ├── KeyboardLayoutEditor.tsx        — Always-visible layout editor: visual matrix editor, KLE/JSON portability
│   ├── MacrosDashboard.tsx             — Sidebar panel: Macro CRUD + Export/Import
│   ├── CustomKeysDashboard.tsx         — Sidebar panel: Custom Key CRUD + Export/Import
│   ├── StatusWidget.tsx                — Header widget: BLE/USB/Split status indicators
│   ├── SplitDashboard.tsx              — "Split" section: Pairing, role swap, latency, remote matrix visualizer
│   ├── DeviceDashboard.tsx             — Device settings: name, split link, BLE identity (rendered inside SettingsModal)
│   │
│   ├── components/
│   │   ├── Background3D.tsx            — Full-page 3D keyboard background (React Three Fiber; procedural geometry, split detection, Minkowski-sum baseplates)
│   │   ├── DevControlsPanel.tsx        — Developer Mode raw log viewer (legacy; now invoked via DevConsoleModal)
│   │   ├── MacroEditorModal.tsx        — Full macro editor with recording, element list, drag-and-drop
│   │   ├── MacroModeModal.tsx          — Inline modal to change a macro's execution mode
│   │   ├── MacroPreview.tsx            — Read-only summary of a macro's elements for the card view
│   │   ├── MacroIcons.tsx              — Execution mode icon components + getModeBadge() helper
│   │   ├── ExportModal.tsx             — Multi-select modal to choose which macros to export
│   │   ├── ImportModal.tsx             — Preview + confirm modal for importing a JSON macro file
│   │   ├── SearchableKeyModal.tsx      — Searchable key picker (HID keys, custom keys, macros, transparent)
│   │   ├── SidebarIcons.tsx            — SVG icons used in the main application navigation sidebar/header
│   │   └── Icons.tsx                   — Reusable SVG icon components (ActionTapIcon, etc.)
│   │
│   ├── hooks/
│   │   ├── useMacros.ts                — Macro state + all device operations (fetch, save, delete)
│   │   ├── useCustomKeys.ts            — Custom key state + all device operations
│   │   └── useConfirm.tsx              — Promise-based confirm dialog (renders portal, resolves on OK/Cancel)
│   │
│   ├── services/
│   │   ├── DeviceController.ts         — High-level business logic wrapping HIDTransport
│   │   └── HIDTransport.ts             — Low-level: WebHID, CRC-8, Blast+Reconcile, task queue, reconnect
│   │
│   ├── stores/
│   │   ├── deviceStore.ts              — Zustand: isConnected, deviceStatus, isDeveloperMode, controller ref
│   │   ├── macroStore.ts               — Zustand: macros[], macroLimits, macroCache, async fetch/save/delete
│   │   ├── customKeyStore.ts           — Zustand: customKeys[], async fetch/save/delete
│   │   ├── logStore.ts                 — Zustand: logs[] (max 200 entries), addLog, clearLogs
│   │   ├── notificationStore.ts        — Zustand: global notification state; showNotification(message, type)
│   │   └── layoutStore.ts              — Zustand: physicalLayout, layers, activeLayer, isConnected (shared between editor and 3D background)
│   │
│   ├── types/
│   │   ├── protocol.ts                 — All protocol constants (VID/PID, flag bits, module IDs, key IDs)
│   │   ├── device.ts                   — CommandResponse, DeviceStatus, LogMessage, PhysKey, LayerData, callbacks, NotificationType
│   │   ├── macros.ts                   — Macro, MacroElement, MacroAction, MacroLimits, ImportableMacro
│   │   ├── customKeys.ts               — CustomKey, CustomKeyPR, CustomKeyMA
│   │   └── index.ts                    — Barrel re-export
│   │
│   └── utils/
│       ├── packetUtils.ts              — getFlagsString() for log display, formatHex() helper
│       ├── kleParser.ts                — Parses KLE (Keyboard Layout Editor) JSON into PhysKey[][]
│       ├── layoutUtils.ts              — Physical layout JSON parse + serialize
│       ├── fileUtils.ts                — saveJsonFile(): File System Access API + <a> fallback
│       └── withTimeout.ts              — withTimeout<T>(promise, ms): wraps any promise with a deadline; throws TimeoutError on expiry
```

---

## 4. Communication Protocol

### USB Identifiers

| Field                  | Value                |
|------------------------|----------------------|
| Vendor ID              | `0x303A` (Espressif) |
| Product ID             | `0x1324`             |
| HID Report ID          | `3` (COMM report)    |
| Report size            | `63` bytes           |
| Max payload per packet | `58` bytes           |

The keyboard exposes multiple HID interfaces; the configurator filters for the one with `usagePage = 0xFFFF` (vendor-defined).

### Packet Layout

Each 63-byte HID report has this structure:

```
Byte  0     : flags       — bitmask (see flag table below)
Bytes 1–2   : remaining   — packets remaining after this one (little-endian uint16)
Byte  3     : payloadLen  — valid bytes in the payload field (0–58)
Bytes 4–61  : payload     — up to 58 bytes of application data
Byte  62    : CRC-8       — computed over bytes 0–61, polynomial 0x07
```

### Flags Byte

| Bit  | Constant             | Meaning                                   |
|------|----------------------|-------------------------------------------|
| 0x80 | `PAYLOAD_FLAG_FIRST` | First packet of a transfer                |
| 0x40 | `PAYLOAD_FLAG_MID`   | Middle packet                             |
| 0x20 | `PAYLOAD_FLAG_LAST`  | Last packet (commit)                      |
| 0x10 | `PAYLOAD_FLAG_ACK`   | Acknowledgment                            |
| 0x08 | `PAYLOAD_FLAG_NAK`   | Negative acknowledgment / bitmap response |
| 0x04 | `PAYLOAD_FLAG_OK`    | Command succeeded                         |
| 0x02 | `PAYLOAD_FLAG_ERR`   | Command failed                            |
| 0x01 | `PAYLOAD_FLAG_ABORT` | Abort transfer                            |

Combined flags used in Blast+Reconcile:

| Value              | Name                     | Meaning                                         |
|--------------------|---------------------------|------------------------------------------------|
| `0x50` (MID\|ACK)  | `PAYLOAD_FLAG_STATUS_REQ` | Host requests a bitmap of received packets     |
| `0x48` (MID\|NAK)  | `PAYLOAD_FLAG_BITMAP`     | Device replies with bitmap of received packets |

### Application Payload Format

The first 3 bytes of every application payload are a header:

```
Byte 0 : module   — 0x00=CONFIG, 0x01=SYSTEM, 0x02=ACTION, 0x03=STATUS
Byte 1 : command  — 0x00=GET, 0x01=SET
Byte 2 : key ID   — what config record to read/write (see CFG_KEY_* constants)
Bytes 3+: JSON    — UTF-8 encoded JSON data (may span multiple packets)
```

### CRC-8

Polynomial `0x07` (same as the firmware `usb_crc.c`). The browser-side table is pre-computed in `HIDTransport.ts`. The firmware verifies CRC on every received packet; the browser verifies it on every received packet via `handleInputReport`.

### Multi-Packet Protocol: Blast + Reconcile

When the payload exceeds 58 bytes, the transport uses the **Blast + Reconcile** algorithm:

**Transmit (host → device):**

```
Phase 1 — Handshake
  → Send packet[0] with FIRST flag
  ← Wait for ACK (3 s timeout)

Phase 2 — Blast
  → Send packets[1 .. N-2] with MID flag, no wait between them

Phase 3 — Reconcile (up to 5 rounds)
  → Send STATUS_REQ (PAYLOAD_FLAG_STATUS_REQ)
  ← Wait for BITMAP packet (3 s timeout)
     Bitmap: bit i = 1 means device received packet i
  → Retransmit any missing MID packets
  → Repeat until bitmap shows all MID packets received

Phase 4 — Commit
  → Send packet[N-1] with LAST flag
```

**Receive (device → host):**

The same algorithm runs in reverse. The device blasts responses; the browser accumulates packets using a receive-side bitmap (`blastRx` state). When the LAST packet arrives, the browser assembles the full payload from the buffer and resolves the pending `sendCommand` promise. The abort guard (`BLAST_RX_MAX_PACKETS = 5000`) prevents infinite accumulation on a runaway device.

### Task Queue

All commands are serialized through a FIFO task queue (`enqueueTask`). Only one command can be in-flight at a time. A 50 ms gap (`TASK_QUEUE_DELAY_MS`) is inserted between tasks to give the USB subsystem time to recover.

### Auto-Reconnect

If the device disconnects unexpectedly while `wantConnection` is true, `HIDTransport` starts polling every 2 s (`RECONNECT_INTERVAL_MS`) via `navigator.hid.getDevices()` looking for the previously-authorized device. It also listens for the `connect` WebHID event and fast-reconnects within 1 s if the same VID/PID reappears.

---

## 5. State Management

### Zustand stores (`src/stores/`)

| Store                 | Key state                                                      | Purpose                                   |
|-----------------------|----------------------------------------------------------------|-------------------------------------------|
| `deviceStore`         | `isConnected`, `deviceStatus`, `isDeveloperMode`, `controller` | Connection + global UI flags              |
| `macroStore`          | `macros[]`, `macroLimits`, `macroCache`                        | Macro list + per-macro detail cache       |
| `customKeyStore`      | `customKeys[]`                                                 | Custom key list                           |
| `logStore`            | `logs[]` (max 200)                                             | Communication log ring buffer             |
| `notificationStore`   | `notification`, `showNotification`, `clearNotification`        | Global user-feedback notification system  |
| `layoutStore`         | `physicalLayout`, `layers`, `activeLayer`, `isConnected`       | Bridge between `KeyboardLayoutEditor` and `Background3D` |

The stores hold **typed actions** that accept a `DeviceController` argument, keeping the async device logic inside the store rather than leaking into components.

> **Note:** The current `App.tsx` + hooks architecture does **not** read from the device/macro/customKey Zustand stores — it manages its own local state and uses `useMacros` / `useCustomKeys` hooks directly. The Zustand stores are prepared infrastructure for a future refactor. The exception is `notificationStore`, which **is actively used** by all dashboard components and `App.tsx` for UI feedback.

### React hooks (`src/hooks/`)

| Hook            | Manages                                  | Used by                      |
|-----------------|------------------------------------------|------------------------------|
| `useMacros`     | macros[], macroLimits, fetch/save/delete | `App.tsx`                    |
| `useCustomKeys` | customKeys[], fetch/save/delete          | `App.tsx`                    |
| `useConfirm`    | confirmation dialog                      | `useMacros`, `useCustomKeys` |

**`useMacros` internal pattern:** Uses a `macrosRef` (always up-to-date) alongside `useState` because async callbacks (e.g. the retry loop in `fetchMacros`) run as microtasks and would read stale closure state from `useState`. The ref is always the authoritative list; `setMacros` is called alongside every `macrosRef` update via the internal `syncMacros()` helper. An `optimistic reservation` pattern is used for new macros: the ID is reserved in state before the USB write, preventing collisions when multiple macros are created quickly.

### App-level state (`App.tsx`)

After hook extraction, `App.tsx` owns only:
- `isConnected` — drives conditional rendering of all panels
- `deviceStatus` — passed to `StatusWidget`
- `isDeveloperMode` — persisted in `localStorage`, unlocked via the code
- `logs[]` — ring of `LogMessage` entries for the dev panel strip
- `activeSection` — current navigation target (layout, macrosCkeys, split, identity)

---

## 6. Layout Editor (`KeyboardLayoutEditor.tsx`)

### Matrix dimensions

```typescript
const MATRIX_ROWS = 6;
const MATRIX_COLS = 18;
const LAYER_COUNT = 4;  // Base, FN1, FN2, FN3
```

These **must match the firmware** (`kb_layout.h`). The UI hard-codes `0xFFFF` (`KB_KEY_TRANSPARENT`) as the "pass-through" sentinel: a transparent key falls through to the layer below.

### Layers

Layer 0 (Base) is the active layout by default. Layers 1–3 (FN1–FN3) are activated by the physical FN1/FN2 keys. Layer 3 activates when both FN1 and FN2 are held simultaneously.

Each layer is a `number[][]` (6 × 18 matrix of action codes). The configurator loads all 4 layers from the device on connect, lets you edit them visually, and saves them one at a time via `CFG_KEY_LAYER_0`–`CFG_KEY_LAYER_3`.

### Physical layout

A "physical layout" is a flat `PhysKey[]` array describing the visual position and size of each key (`row`, `col`, `x`, `y`, `w`, `h` in key units). It is stored on the device separately from keymaps (as `CFG_KEY_PHYSICAL_LAYOUT`) and controls how the visual editor renders keys.

If no physical layout is stored on the device, the editor falls back to a generic grid.

### KLE import

The editor accepts **KLE (Keyboard Layout Editor) raw JSON** pasted into a text area. `parseKleJson()` in `utils/kleParser.ts` converts it into a `PhysKey[][]`. After parsing:

1. Bounds validation checks that no key has `row >= MATRIX_ROWS` or `col >= MATRIX_COLS`. Out-of-bounds keys show an error and abort.
2. On success, `setPhysicalLayout(parsed)` updates the local state and the "Save Physical Layout" button pushes it to the device.

### Test mode / Row-Col Edit

Accessed via the "..." options menu in the Layout section (Developer Mode only). 

When test mode is active, physical key presses are injected via `SYS_CMD_INJECT_KEY`. In Row-Col Edit mode, matrix coordinates are displayed over each key, and logical matrix positions can be reassigned.

### Portability (Export/Import)

The entire layout (all 4 layers) and the physical layout geometry can be exported to a single JSON file. This is accessed via the options menu in the Layout section.

---

## 7. Macro System

### Data model

```typescript
interface Macro {
    id:          number;           // 0-based slot index on the device
    name:        string;           // Display name (UTF-8, max firmware-defined length)
    elements:    MacroElement[];   // Ordered list of actions
    execMode?:   number;           // Execution mode (0–7, default 0)
    stackMax?:   number;           // Max concurrent stacks (mode 2 only)
    repeatCount?: number;          // Burst repeat count (mode 7 only)
}

type MacroElement =
    | { type: 'key'; key: number; action?: 'tap'|'press'|'release'; inlineSleep?: number; pressTime?: number }
    | { type: 'sleep'; duration: number };
```

`MacroElement.key` is a USB HID usage code (or action code for media/system keys). `inlineSleep` adds a delay *after* the key event in milliseconds. A standalone `sleep` element is a pure delay without a key event.

### Execution modes

| `execMode` | Badge | Category | Behaviour                                                               |
|------------|-------|----------|-------------------------------------------------------------------------|
| 0          | 1×    | once     | Run once per keypress; stack up to 1 instance                           |
| 1          | 1!    | once     | Run once; if already running, ignore new presses                        |
| 2          | N+    | once     | Run once; allow N concurrent stacks (`stackMax`)                        |
| 3          | ↻⬇    | repeat   | Loop while key is held (stop when released — finish current iteration)  |
| 4          | ↺⬇    | repeat   | Loop while key is held (cancel immediately on release)                  |
| 5          | ↻⏻   | repeat   | Toggle: press to start looping, press again to stop (finish iteration)  |
| 6          | ↺⏻   | repeat   | Toggle: press to start looping, press again to cancel immediately       |
| 7          | N×    | burst    | Run exactly `repeatCount` times in rapid succession                     |

### Recording flow

In `MacroEditorModal`, clicking **Record** starts a `keydown`/`keyup` listener. While recording:
- Each physical key press appends a `{ type: 'key', key: hid_code, action: 'tap' }` element.
- The `BROWSER_CODE_TO_HID` map in `KeyDefinitions.ts` converts browser `event.code` strings to HID usage codes.
- A `recordingStateRef` mirrors `isRecording` state for use inside imperative event listeners (prevents stale closure).
- **Timeline Performance Optimization**: The `updatePlayhead` animation loop explicitly separates DOM Reads (e.g. `el.clientWidth`, `el.scrollLeft`) from DOM Writes (e.g. `style.width`, `style.left`, `style.minWidth`) into discrete phases. It also batches container width expansion in large 2000px chunks. This strict enforcement of the DOM Read-Write cycle completely eliminates synchronous layout thrashing (forced reflows) ensuring recording remains fluid at 60 FPS without high CPU usage.

Elements can also be added manually from a searchable key picker, re-ordered by drag-and-drop, and each element allows customizing the action (`tap`/`press`/`release`) and `inlineSleep`.

### Device limits

On connect, the app queries `CFG_KEY_MACRO_LIMITS` which returns `{ maxMacros, maxEvents }`. `maxMacros` caps how many macros can exist; `maxEvents` caps the total number of `MacroElement` entries in a single macro. The Create button is disabled when the limit is reached.

### Device encoding

Macros are sent as JSON via `CFG_KEY_MACRO_SINGLE` (SET). The JSON is the full `Macro` object. Bulk fetch uses `CFG_KEY_MACROS` (GET) which returns a list of all macros (outline only — no elements). Per-macro elements are then fetched individually with `CFG_KEY_MACRO_SINGLE` (GET, body: `{ id }`).

Delete sends `CFG_KEY_MACRO_SINGLE` (SET, body: `{ delete: id }`).

### Action code mapping (in key assignments)

When a macro is assigned to a key position in the layout editor:

```
Key position action code = MACRO_BASE (0x4000) + macro.id
```

`MACRO_BASE` (`ACTION_CODE_MACRO_MIN = 0x4000`) is defined in `types/protocol.ts`. The firmware resolves codes in this range to the corresponding macro slot.

### Export / Import

**Export:** Opens `ExportModal` where you pick which macros to export. Full macro data is fetched for each selection, IDs are stripped, and the array is saved as JSON via `saveJsonFile()`.

**Import:** Triggers a file input. The loaded JSON array is previewed in `ImportModal`. On confirm, each macro is saved with `id: -1` (triggering auto-ID assignment in `useMacros`). The `importGuardRef` prevents double-submission from React strict mode double-invocations.

---

## 8. Custom Keys

Custom keys are programmable key behaviours that sit between the hardware matrix and the USB HID output. A custom key is referenced in a layer by its action code:

```
Key position action code = CKEY_BASE (0x3000) + customKey.id
```

Up to `CKEY_MAX_COUNT = 120` custom keys are supported per device.

### Modes

#### PressRelease (PR) mode — `mode: 0`

```typescript
interface CustomKeyPR {
    pressAction:     number;   // HID code to send on key press
    releaseAction:   number;   // HID code to send on key release
    pressDuration:   number;   // ms to hold the press event (default 20 ms)
    releaseDuration: number;   // ms to hold the release event (default 20 ms)
    waitForFinish:   boolean;  // if true, block the next press until this one completes
}
```

Useful for keys that need different press and release behaviours, or for injecting long-duration keypresses (e.g. for accessibility).

#### MultiAction (MA) mode — `mode: 1`

```typescript
interface CustomKeyMA {
    tapAction:          number;  // Action on single tap
    doubleTapAction:    number;  // Action on double tap
    holdAction:         number;  // Action on hold
    doubleTapThreshold: number;  // ms window to detect a double tap (default 300 ms)
    holdThreshold:      number;  // ms before a press is classified as hold (default 500 ms)
    tapDuration:        number;  // ms to hold each tap action event (default 20 ms)
    doubleTapDuration:  number;  // ms to hold double tap action event (default 20 ms)
    holdDuration:       number;  // ms to hold hold action event (default 20 ms)
}
```

The firmware classifies each physical keypress as one of three gestures based on timing. The `doubleTapThreshold` and `holdThreshold` must be tuned to avoid mis-fires.

### Device encoding

Fetch all custom keys: `CFG_KEY_CKEYS` (GET) → list of outlines.
Fetch single: `CFG_KEY_CKEY_SINGLE` (GET, body: `{ id }`).
Save: `CFG_KEY_CKEY_SINGLE` (SET, body: full `CustomKey` object).
Delete: `CFG_KEY_CKEY_SINGLE` (SET, body: `{ delete: id }`).

ID assignment follows the same "smallest available slot" pattern as macros. `id: -1` triggers auto-ID allocation.

---

## 9. Combos

Combos allow users to trigger an action when a specific set of keys are pressed simultaneously. A combo consists of 2 to 8 keys on the matrix and an action code.

### Data model

```typescript
export interface Combo {
    id: number;
    name: string;
    keys: { row: number; col: number }[]; // Physical keys
    action: number;                       // HID code to send
    activeLayers: number[];               // e.g. [0, 1] means active on Base and FN1
    strictOrder: boolean;                 // If true, keys must be pressed in exact order
}
```

### Device encoding

Fetch all combos: `CFG_KEY_COMBOS` (GET) → list of combos.
Fetch single: `CFG_KEY_COMBO_SINGLE` (GET, body: `{ id }`).
Save: `CFG_KEY_COMBO_SINGLE` (SET, body: full `Combo` object).
Delete: `CFG_KEY_COMBO_SINGLE` (SET, body: `{ delete: id }`).

Up to 32 combos are supported per device (`COMBO_MAX`).

---

## 10. Developer Mode

Developer Mode is toggled by typing the **Developer Code** while the application is focused. There is no visible button for this to prevent accidental activation:
`↑` `↑` `↓` `↓` `←` `→` `←` `→` `B` `A`

### DevControlsPanel

When active, a `DevControlsPanel` appears as a strip at the bottom of the viewport. It provides:

**Left — Config Explorer:**
- **Target Module** selector — CONFIG or SYSTEM.
- **Key ID** selector — selects which config record to read/write.
- **Auto-Form** — populated by a GET of the selected key; allows editing and SETing raw JSON records.

**Right — Raw Packet Log:**
- Real-time display of every HID report sent/received.
- Decoded flags (e.g. `[FIRST|LAST|ACK]`) and text-decoded payloads for rapid protocol debugging.

### Unlocked Sections

Developer mode unlocks the **Identity** section in the main header navigation, which allows modifying device names and split hardware variants. It also unlocks advanced tools in the Layout section's options menu (KLE Import, Physical Layout Save, Matrix Row/Col Edit).

### Packet flags display

`getFlagsString(flags: number)` in `utils/packetUtils.ts` converts the flags byte to a human-readable string:

| Flags byte           | Output          |
|----------------------|-----------------|
| `0xA0` (FIRST+LAST)  | `[FIRST\|LAST]` |
| `0x40` (MID)         | `[MID]`         |
| `0x10` (ACK)         | `[ACK]`         |
| `0x00`               | `[NONE]`        |

### Macro ID display

In Developer Mode, each macro card shows its raw HID action code: `ID: 0x4000` through `0x401F` (for slots 0–31). This makes it easy to cross-reference the firmware keymap tables.

---

## 11. Notification System

A global, non-intrusive toast notification system provides consistent user feedback across all dashboards. It replaces any use of `alert()` or local error state.

### Store (`src/stores/notificationStore.ts`)

```typescript
showNotification(message: string, type?: NotificationType): void
// type: 'info' | 'warning' | 'error' | 'success'
```

Any component can import `useNotificationStore` and call `showNotification` without prop drilling.

### Notification types

| Type      | Color  | Use case                                          | Auto-dismiss |
|-----------|--------|---------------------------------------------------|--------------|
| `success` | Green  | Save confirmed, import/export completed           | 2.5 s        |
| `info`    | Blue   | Informational status changes                      | 6 s          |
| `warning` | Amber  | Non-fatal issues (e.g. layout not stored, import needs save) | 6 s |
| `error`   | Red    | Operation failed or timed out                     | 6 s          |

### Auto-dismiss behavior (`App.tsx`)

- A CSS transition makes the toast slide in/out.
- **Success** notifications dismiss after **2.5 s**. All others dismiss after **6 s**. Linux permission errors persist for **20 s**.
- **Hover-to-pause**: moving the mouse over the toast cancels the timer. On mouse-leave, a grace period (1.5 s for success, 4 s for others) restarts before dismissal.
- **Manual Dismissal**: Every notification features an "x" close button in the top-right corner for immediate removal.
- **Actionable Notifications**: Some notifications include interactive buttons, such as the "Refresh Now" button for Linux system locks.
- **Linux Permission Help**: On Linux, connectivity failures (Permission Denied or System Lock) trigger a persistent, detailed help overlay with the necessary `udev` rules and a copyable fix command.

### Coverage

The notification system is used in every dashboard for all save, delete, import, and export operations:

| Component | Events notified |
|-----------|-----------------|
| `MacrosDashboard` | Save, delete, export, import |
| `CustomKeysDashboard` | Save, delete, export, import |
| `KeyboardLayoutEditor` | Layer save (per layer), KLE physical layout save, JSON export/import, Key Test Mode toggle |
| `SplitDashboard` | Pairing start/cancel, unpair, role swap |
| `DeviceIdentityDashboard` | Identity save, load failure |

---

## 12. Save Timeout Guard (`src/utils/withTimeout.ts`)

All write operations to the device are wrapped with a 7-second timeout to prevent the UI from hanging indefinitely if the firmware does not respond.

```typescript
// Throws TimeoutError if promise does not resolve within ms milliseconds
export function withTimeout<T>(promise: Promise<T>, ms: number): Promise<T>

export class TimeoutError extends Error {
    readonly isTimeout = true;
}
```

**Applied to:**
- `useMacros.ts` — `saveMacro`, `deleteMacro`
- `useCustomKeys.ts` — `saveCustomKey`, `deleteCustomKey`
- `DeviceIdentityDashboard.tsx` — `saveDeviceIdentity`
- `SplitDashboard.tsx` — `splitStartPairing`, `splitCancelPairing`, `splitUnpair`, `splitRoleSwap`
- `KeyboardLayoutEditor.tsx` — per-layer saves, KLE physical layout SET

On timeout, a `TimeoutError` is caught and surfaced as an `error` notification with a "please retry" message. The `isSaving`/`busy` state is always cleared in a `finally` block so the UI remains usable.

---

## 13. Packet Flow: End-to-End Example

**Fetching a single macro (id=2):**

```
App               useMacros           HIDTransport           Device
 |                     |                    |                    |
 | fetchMacros()       |                    |                    |
 |-------------------->|                    |                    |
 |              sendCommand([00,00,07])      |                    |
 |              (MODULE_CONFIG, GET, MACROS) |                    |
 |                     |---> enqueueTask    |                    |
 |                     |                    |---FIRST|LAST------>|
 |                     |                    |<--FIRST|LAST-------|  JSON list
 |                     |                    |---ACK------------->|
 |                     |<-- CommandResponse  |                    |
 |                     |                    |                    |
 |              fetchSingleMacro(2)          |                    |
 |                     |  sendCommand([00,00,09,{"id":2}])        |
 |                     |---> enqueueTask    |                    |
 |                     |                    |---FIRST|LAST------>|
 |                     |                    |<--FIRST ----------|  JSON part 1
 |                     |                    |<--MID  ----------|   JSON part 2
 |                     |                    |<--LAST ----------|   JSON part 3
 |                     |                    |---BITMAP--------->|  (ACK all parts)
 |                     |                    |<--ACK-------------|
 |                     |<-- CommandResponse  |                    |
 |<-- macros[] updated  |                    |                    |
```

---

## 14. Key Definitions & Action Code Ranges

Defined in `types/protocol.ts` and `KeyDefinitions.ts`:

| Range | Constant | Description |
|-------|----------|-------------|
| `0x0001–0x00FF` | `ACTION_CODE_HID_MIN/MAX` | Standard USB HID keyboard codes |
| `0x0100–0x01FF` | `ACTION_CODE_MEDIA_MIN/MAX` | Consumer/media control codes |
| `0x2000–0x20FF` | `ACTION_CODE_SYSTEM_MIN/MAX` | System control (e.g. `0x2001`=BrightUp) |
| `0x3000–0x3FFF` | `ACTION_CODE_CKEY_MIN/MAX` | Custom key slots (base 0x3000 + id) |
| `0x4000–0x4FFF` | `ACTION_CODE_MACRO_MIN/MAX` | Macro slots (base 0x4000 + id) |
| `0xFFFF` | `KB_KEY_TRANSPARENT` | Pass-through to next layer |
| `0x0000` | `ACTION_CODE_NONE` | No-op / unassigned |

---

## 15. 3D Background Studio Environment (`Background3D.tsx` / `App.tsx` / `index.css`)

The configurator features a highly immersive, multi-layered background environment combining a **procedurally generated real-time 3D keyboard canvas** with an ultra-clean, minimalist studio vignette backdrop. It requires no external assets and relies entirely on runtime generation for lightweight execution.

### 1. Minimalist Dark Studio Vignette Backdrop
To keep the absolute focus on the gorgeous 3D model, its materials, shadows, and subtle reflections, the background is completely free of distracting tech patterns, lights, grids, or particles:
- **Clean Studio Gradient**: A smooth, professional, ultra-dark obsidian radial vignette backdrop with a localized warm amber-copper sunset glow directly behind the keyboard (`radial-gradient(circle at 50% 35%, #2d1304 0%, #0c0501 55%, #050201 100%)`) provides an extremely premium, eye-friendly, and deep atmospheric backdrop.
- **Transparent 3D Canvas**: The 3D Three.js canvas renders transparently (`gl={{ alpha: true }}`) allowing the deep, smooth studio backdrop gradient to show through.
- **Atmospheric Studio Fog**: A subtle, matched dark warm-charcoal fog (`<fog attach="fog" args={['#0c0501', 26, 48]} />`) blends the distant edges of the scene smoothly, keeping the atmosphere clearly present with a beautiful warm copper rim-glow while retaining perfect model clarity in the foreground.
- **Premium Studio Scene Lighting**: Specular and ambient lights inside the 3D scene are tuned to create a realistic specular highlights bloom across the keyboard keycaps, baseplate, and switches. Dedicated dual warm orange spotlights (`#ffa552` and `#ff7300` at `650.0` intensity each) are positioned symmetrically behind the keyboard at `[-9, 6, -10]` and `[9, 6, -10]`, targeting the center of the keyboard to project a clear, stunning double rim lighting glow on the backside without blinding the user.
- **Contact Shadows**: High-end floor contact shadows (`ContactShadows`) are cast directly beneath the keyboard model, anchoring it in the 3D space as if resting on a premium dark matte studio surface.

### 2. Pointer Isolation and Interactivity
- **Pointer Isolation**: All transparent structural HTML panels use `pointer-events: none`, allowing clicks and drags on empty space to pass directly through to the 3D Canvas. Interactive dashboard controls explicitly use `pointer-events: auto` to prevent conflicts.
- **Canvas-Wide Drag Rotation**: A native `pointerdown` listener allows smooth drag-rotation of the 3D keyboard model from any empty backdrop area, while aborting automatically if the click starts on any interactive 2D panel or keyboard keycap.

### 3. Auto-Rotation and Weaving Animation
To achieve a premium, high-end studio feel, the keyboard model avoids continuous spinning in favor of a smooth, lifelike weaving animation:
- **Sine-Wave Weaving**: The keyboard oscillates smoothly left-to-right along the Y-axis (`Math.sin(state.clock.elapsedTime * weaveFreq + weaveSeed) * weaveAmp`), creating a subtle perspective shift.
- **Randomized Phase Offset**: A `weaveSeed` ref is initialized at load with a random phase (`Math.random() * Math.PI * 2`) ensuring that each page load starts from a fresh, unique angle.
- **Tuned Motion Design**: The animation uses a slow frequency (`weaveFreq = 0.18` rad/s, resulting in a ~35-second full cycle) and subtle amplitude (`weaveAmp = 0.26` rad, approximately ±15° of rotation) for an extremely calm and premium visual presence.
- **Smooth Transition**: The auto-rotate strength is smoothly interpolated (using `lerp`) when transitioning between active user-controlled drag rotation and the idle weaving animation, avoiding abrupt visual snaps.

### 4. 3D Model Rendering Pipeline

1. **Geometry source** — `Background3D.tsx` reads `physicalLayout` from `layoutStore`. If no layout is loaded, it renders a hardcoded 65% keyboard with realistic key colour assignments.
2. **Key colouring** — each keycap is coloured according to the action code stored in the active layer at the key's `{row, col}` position, using `getKeyClass()` from `KeyDefinitions.ts`:

   | Key class | Colour |
   |---|---|
   | Standard alphanumerics | Dark charcoal (`#111111`) |
   | Modifiers + action keys (Ctrl, Shift, Alt, Enter, Esc, Caps, Menu, arrows) | Deep blue (`#2a61a8`) |
   | F1–F12 | Forest green (`#2a7a3b`) |
   | System actions, macros, custom keys | Deep purple (`#6436b5`) |
   | Unassigned / transparent | Near-black (`#080b0f`) |

3. **Split detection** — column gaps in the middle of the physical layout signal a split keyboard. The layout is split into two clusters, each rendered with independent ergonomic tenting (Z-axis roll) and a lateral gap.
4. **Custom baseplates** — each cluster gets a backplate whose outline is computed via a **Minkowski sum on its 2D Convex Hull**: the hull of all key corners is inflated with smooth circular arcs at every vertex, then extruded into a bevelled 3D slab. This produces a rounded, organic silhouette that exactly matches each half's physical outline.
5. **Rotation correctness** — KLE rotation pivots `(rx, ry)` are fully replicated in 3D: keys are placed inside a `<group>` at the pivot position, then the group is rotated, so angled thumb clusters land in their correct world positions without clipping.

### Connection-driven visibility

The `isConnected` flag is stored in `layoutStore` and set by `App.tsx`'s connection handler. `Background3D` reads it and applies:

- **Fade-in on connect**: `opacity 0 → 1` + `translateY(40px → 0)` over 2.5 s (CSS, spring easing).
- **Key colour fade-in**: keycap `opacity` is animated per-frame via `useFrame` over ~2.5 s so colours bloom in gradually.
- **Fade-out on disconnect**: the layout store is cleared (`physicalLayout → null`, `layers → [null,…]`), reversing both animations and sinking the model out of view.

### State Wiring

| State | Source | Consumer |
|---|---|---|
| `physicalLayout` | `KeyboardLayoutEditor` (on device load / KLE import) | `Background3D` (geometry generation) |
| `layers` / `activeLayer` | `KeyboardLayoutEditor` (on device load) | `Background3D` (key colouring) |
| `isConnected` | `App.tsx` (connection handler) | `Background3D` (fade in/out) |

All shared state lives in `stores/layoutStore.ts` (Zustand). Clearing the layout store on disconnect (setting `physicalLayout → null`, `layers → [null,…]`) is what triggers the fade-out.

### 2D/3D Interaction & Pointer Isolation

To prevent conflicts between the 2D layout editor (dragging to select keys) and the 3D canvas background (dragging to rotate the keyboard model), the application implements a strict pointer-events isolation system:

1. **CSS Pointer Isolation (`index.css`)**:
   - Transparent structural layout wrappers and page dashboard containers (`.app-layout`, `.app-main-content`, `.app-sections-area`, `.section-container`, `.macros-ckeys-split-view`, `.layout-editor`, `.macros-dashboard`, `.custom-keys-dashboard`, `.dd-page`, etc.) are styled with `pointer-events: none`. This allows clicks on any empty space around the dashboards to pass directly through the DOM layers to the 3D Canvas.
   - All interactive 2D UI components, dashboards, columns, controls, modals, and settings blocks (`.main-header`, `.layout-toolbar`, `.keyboard-grid`, `.glass-panel`, `.modal-overlay`, `.devctrl-page`, `.list-column`, `.dd-page-header`, `.dd-sections`, `.dd-section`, etc., along with standard buttons, links, and inputs) have `pointer-events: auto` explicitly set. They fully intercept all mouse interactions, preventing rotation triggers when editing layouts.

2. **Canvas-Wide Drag Rotation (`Background3D.tsx`)**:
   - A native `pointerdown` listener is bound directly to the `window` within `useEffect` inside the `KeyboardModel` component.
   - To prevent conflict with 2D dashboard interactivity, the handler utilizes a highly comprehensive `.closest()` selector filter. If a click starts on any interactive panel, column, modal, dropdown, header, button, input, or keyboard grid, the handler immediately aborts.
   - Any drag starting on the empty background space or around the 3D keyboard model initiates the rotation seamlessly, providing a robust, responsive, and cross-browser customizer experience without any dependency on standard DOM hit-testing constraints.

