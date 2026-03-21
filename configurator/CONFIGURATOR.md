# Configurator — Technical Documentation

A React 19 + TypeScript + Vite web app for configuring an ESP32-based programmable keyboard over WebHID. Runs entirely in the browser — no server, no install required.

---

## 1. Overview

The configurator lets you:
- Edit key mappings across 4 independent layers (Base, FN1, FN2, FN3)
- Create, edit, and delete macros with rich action types and execution modes
- Create, edit, and delete custom keys (PressRelease and MultiAction modes)
- Import physical layouts from KLE (Keyboard Layout Editor) JSON
- Export and import macro sets as portable JSON files
- Monitor raw HID communication in Developer Mode

### Getting started

```sh
cd configurator
npm install
npm run dev        # Vite dev server at http://localhost:5173
npm run build      # TypeScript check + Vite production build
npm run preview    # Serve the production build locally
```

The app requires a Chromium-based browser (Chrome, Edge, Opera) — Firefox does not support WebHID.

---

## 2. Architecture

```
Browser
  └── React UI (App.tsx)
        ├── Zustand Stores         — global state (device, macros, custom keys, logs)
        ├── React Hooks            — local business logic (useMacros, useCustomKeys)
        └── DeviceController       — high-level typed API
              └── HIDTransport     — low-level WebHID transport
                    └── WebHID API — USB HID connection to the keyboard
                          └── ESP32 firmware (USB HID device, VID 0x303A / PID 0x1324)
```

### Layer responsibilities

| Layer            | File                           | Responsibility                                                       |
|------------------|--------------------------------|----------------------------------------------------------------------|
| React UI         | `App.tsx`, dashboards, modals  | Rendering, user interaction, routing                                 |
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
│   ├── App.tsx                         — Top-level: connection wiring, log aggregation, root layout
│   ├── main.tsx                        — React entry point, DeviceController instantiation
│   ├── index.css                       — All CSS (variables, layout, components)
│   ├── HIDService.ts                   — Backward-compat re-export façade (all protocol constants + hidService singleton)
│   ├── KeyDefinitions.ts               — HID keycodes, key names, browser key→HID map, action code ranges
│   ├── KeyboardLayoutEditor.tsx        — Visual key map editor: layers, physical layout, KLE import, test mode
│   ├── MacrosDashboard.tsx             — Macro list + CRUD + export/import workflow
│   ├── CustomKeysDashboard.tsx         — Custom key list + CRUD with PR/MA mode editor
│   ├── StatusWidget.tsx                — BLE/USB transport mode + profile + pairing status display
│   ├── SearchableKeyModal.tsx          — Legacy entry point; delegates to components/SearchableKeyModal.tsx
│   │
│   ├── components/
│   │   ├── DevControlsPanel.tsx        — Developer Mode panel: config GET/SET form + raw log viewer
│   │   ├── MacroEditorModal.tsx        — Full macro editor with recording, element list, drag-and-drop
│   │   ├── MacroModeModal.tsx          — Inline modal to change a macro's execution mode
│   │   ├── MacroPreview.tsx            — Read-only summary of a macro's elements for the card view
│   │   ├── MacroIcons.tsx              — Execution mode icon components + getModeBadge() helper
│   │   ├── ExportModal.tsx             — Multi-select modal to choose which macros to export
│   │   ├── ImportModal.tsx             — Preview + confirm modal for importing a JSON macro file
│   │   ├── SearchableKeyModal.tsx      — Searchable key picker (HID keys, custom keys, macros, transparent)
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
│   │   └── logStore.ts                 — Zustand: logs[] (max 200 entries), addLog, clearLogs
│   │
│   ├── types/
│   │   ├── protocol.ts                 — All protocol constants (VID/PID, flag bits, module IDs, key IDs)
│   │   ├── device.ts                   — CommandResponse, DeviceStatus, LogMessage, PhysKey, LayerData, callbacks
│   │   ├── macros.ts                   — Macro, MacroElement, MacroAction, MacroLimits, ImportableMacro
│   │   ├── customKeys.ts               — CustomKey, CustomKeyPR, CustomKeyMA
│   │   └── index.ts                    — Barrel re-export
│   │
│   └── utils/
│       ├── packetUtils.ts              — getFlagsString() for log display, formatHex() helper
│       ├── kleParser.ts                — Parses KLE (Keyboard Layout Editor) JSON into PhysKey[][]
│       ├── layoutUtils.ts              — Physical layout JSON parse + serialize
│       └── fileUtils.ts               — saveJsonFile(): File System Access API + <a> fallback
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

| Store            | Key state                                                      | Purpose                             |
|------------------|----------------------------------------------------------------|-------------------------------------|
| `deviceStore`    | `isConnected`, `deviceStatus`, `isDeveloperMode`, `controller` | Connection + global UI flags        |
| `macroStore`     | `macros[]`, `macroLimits`, `macroCache`                        | Macro list + per-macro detail cache |
| `customKeyStore` | `customKeys[]`                                                 | Custom key list                     |
| `logStore`       | `logs[]` (max 200) | Communication log ring buffer             |

The stores hold **typed actions** that accept a `DeviceController` argument, keeping the async device logic inside the store rather than leaking into components.

> **Note:** The current `App.tsx` + hooks architecture does **not** read from these Zustand stores — it manages its own local state and uses `useMacros` / `useCustomKeys` hooks directly. The Zustand stores are prepared infrastructure for a future refactor that would unify all state. New features should use the stores; the existing App flow still uses hooks.

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
- `isDeveloperMode` — persisted in `localStorage`, passed to dashboards
- `logs[]` — ring of `LogMessage` entries for the dev panel

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

### Test mode

When test mode is active (`isTestMode === true`), physical key presses on the real keyboard are injected via `SYS_CMD_INJECT_KEY`. The highlighted key in the editor tracks which key was most recently pressed, giving visual feedback of the physical key matrix mapping.

### Export

The current physical layout can be exported to JSON (the same format accepted by KLE import) via `saveJsonFile()` from `utils/fileUtils.ts`.

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

## 9. Developer Mode

Toggle the **DEV MODE** switch in the top-right corner of the header. The state is persisted in `localStorage`.

### DevControlsPanel

When Developer Mode is on, a `DevControlsPanel` appears at the bottom. It has two columns:

**Left — Controls:**
- **Enable/disable toggle** — disables sending until explicitly enabled, preventing accidental writes.
- **Target Module** selector — CONFIG or SYSTEM.
- **Key ID** selector (CONFIG module only) — selects which config record to read/write.
- **Configuration Form** — auto-generated form populated by a GET of the selected key. Edit fields and click **Save Payload** to send a SET.
- **Clear Logs** button.

When the module or key ID changes, a GET is fired automatically to populate the form with the current device values.

**Right — Device Logs:**
- Every received HID packet is shown with its timestamp, flag string (e.g. `[FIRST|ACK]`), payload length, remaining count, and either a hex dump or decoded text payload.
- Text log entries (e.g. "Device connected", "Found 3 macros") are injected via `addLog()` in `App.tsx`.

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

## 10. Packet Flow: End-to-End Example

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

## 11. Key Definitions & Action Code Ranges

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
