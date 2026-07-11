# Firmware Layouts — Dynamic Create / Rename / Delete

> **Goal:** Allow users to create, rename, and delete keyboard layouts (layers) via the Web Configurator. The Base layout (index 0) is permanent and immutable in name/existence. All other layouts (up to 15 additional, 16 total) can be freely managed.

---

## Phase Checklist

- [ ] **Phase 0 — Metadata Data Model** — Design the `cfg_layout_index_t` metadata struct and NVS schema
- [ ] **Phase 1 — Config Module (`cfg_layouts`)** — Rewrite storage, caching, and CRUD API
- [ ] **Phase 2 — Keyboard Module (`kb_layout`, `kb_macro`)** — Dynamic layer count, adaptive layer switching
- [ ] **Phase 3 — Wire Protocol** — New `CFG_KEY_*` endpoints for layout CRUD and limits
- [ ] **Phase 4 — Configurator Sync** — Protocol types, DeviceController, layout store, and UI
- [ ] **Phase 5 — Split Module** — Config-sync fragmentation for variable-count layers
- [ ] **Phase 6 — Documentation** — Update all affected `.md` files
- [ ] **Phase 7 — Verification** — Build, flash, end-to-end test

---

## Current Architecture Snapshot

### What exists today

| Aspect | Current State |
|--------|--------------|
| Layer count | **4** — hardcoded via `KB_LAYER_COUNT` in [kb_layout.h](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/include/kb_layout.h#L71) |
| Layer names | Hardcoded `Base`, `FN1`, `FN2`, `FN3` in both firmware and configurator |
| NVS keys | `ly0`–`ly3` in namespace `cfg_lay` — fixed set, no index |
| PSRAM cache | `s_psram_cache` = array of `KB_LAYER_COUNT` × `cfg_layer_t` — allocated once in [cfg_layouts.c](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/config_module/cfg_layouts.c#L139) |
| DRAM mirrors | `s_dram_base` (layer 0 always) + `s_dram_swap` (one hot-swapped layer) |
| Layer switching | FN1/FN2 held-key booleans → `update_layer_state()` in [kb_macro.c](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_macro.c#L117) |
| Wire protocol | `CFG_KEY_LAYER_0..3` (key IDs `0x03..0x06`) — each is a separate endpoint |
| Configurator | `LAYER_COUNT = 4`, `LAYER_NAMES = ['Base', 'FN1', 'FN2', 'FN3']` hardcoded in [KeyboardLayoutEditor.tsx](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/KeyboardLayoutEditor.tsx#L31-L32) |
| Compile-time defaults | `keymaps[4][6][18]` in [kb_layout.c](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_layout.c#L11) — only used for layer 0 fallback after this change |

### What needs to change

The system must evolve from **4 fixed, unnamed layers** to **1–16 named, dynamically managed layers** with an index-based NVS storage model (like macros/combos/custom keys).

---

## Phase 0 — Metadata Data Model

### 0.1 — Layout Index Struct

Introduce a `cfg_layout_index_t` — the single source of truth for which layouts exist and their names. Stored as a single NVS blob under key `lay_idx` in namespace `cfg_lay`.

```c
#define CFG_LAYOUT_MAX_COUNT    16
#define CFG_LAYOUT_NAME_LEN     24   // 23 chars + null

typedef struct __attribute__((packed)) {
    uint16_t active_mask;                              // Bit N = 1 if layout N exists (bit 0 always set)
    char     names[CFG_LAYOUT_MAX_COUNT][CFG_LAYOUT_NAME_LEN]; // Per-slot name
} cfg_layout_index_t;
```

**Design decisions:**

- `active_mask` is a `uint16_t` (16 bits → 16 layouts). Bit 0 is permanently set and cannot be cleared (Base layer protection).
- `names[0]` is always `"Base"` and cannot be overwritten.
- Empty slots have their bit cleared in `active_mask`; their name is irrelevant (zeroed on delete for hygiene).
- NVS key: `lay_idx` in namespace `cfg_lay`.

### 0.2 — NVS Key Pattern

Individual layer blobs use the key pattern `ly_<N>` (e.g. `ly_0`, `ly_1`, ... `ly_15`) in namespace `cfg_lay`.

### 0.3 — Memory Budget

| Item | Size | Location |
|------|------|----------|
| `cfg_layer_t` | 6 × 18 × 2 = **216 bytes** | — |
| PSRAM cache (16 layers) | 216 × 16 = **3,456 bytes** | PSRAM |
| `s_dram_base` | 216 bytes | DRAM |
| `s_dram_swap` | 216 bytes | DRAM |
| `cfg_layout_index_t` | 2 + (16 × 24) = **386 bytes** | DRAM (static) |
| **Total PSRAM** | **~3.4 KB** | |
| **Total DRAM** | **~818 bytes** | |

This is well within budget. The 16-layer limit is driven by the `uint16_t active_mask` and NVS key length constraints, not memory.

---

## Phase 1 — Config Module (`cfg_layouts`)

### Files Modified

| File | Change |
|------|--------|
| [cfg_layouts.h](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/config_module/include/cfg_layouts.h) | New API surface: `cfg_layout_create`, `cfg_layout_delete`, `cfg_layout_rename`, index accessors |
| [cfg_layouts.c](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/config_module/cfg_layouts.c) | Rewrite for dynamic layer count + index management |
| [cfg_storage_keys.h](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/config_module/include/cfg_storage_keys.h) | Replace `CFG_ST_LAYER_0..3` with `CFG_ST_LAYER_FMT` pattern |

### 1.1 — New Header API (`cfg_layouts.h`)

```c
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "kb_matrix.h"

#define CFG_LAYOUT_MAX_COUNT    16
#define CFG_LAYOUT_NAME_LEN     24

// One layer of the keymap (6 rows × 18 cols)
typedef struct cfg_layer {
    uint16_t keys[KB_MATRIX_ROW_COUNT][KB_MATRIX_COL_COUNT];
} cfg_layer_t;

// Persistent index: tracks which layout slots are populated + their names
typedef struct __attribute__((packed)) {
    uint16_t active_mask;
    char     names[CFG_LAYOUT_MAX_COUNT][CFG_LAYOUT_NAME_LEN];
} cfg_layout_index_t;

// ── Registration & Init ──
void        cfg_layouts_register(void);
esp_err_t   cfg_layout_load_all(void);

// ── Hot-path lookup (called from kb_manager scan loop) ──
uint16_t    cfg_layout_get_action_code(uint8_t row, uint8_t col, uint8_t layer);

// ── Per-layer CRUD ──
esp_err_t   cfg_layout_get_layer(uint8_t layer, cfg_layer_t *out);
esp_err_t   cfg_layout_set_layer(uint8_t layer, const cfg_layer_t *in);

// ── Dynamic management ──
esp_err_t   cfg_layout_create(const char *name, uint8_t *out_id);
esp_err_t   cfg_layout_delete(uint8_t id);
esp_err_t   cfg_layout_rename(uint8_t id, const char *new_name);

// ── Index accessors ──
uint8_t     cfg_layout_get_count(void);                  // Number of active layouts
bool        cfg_layout_exists(uint8_t id);               // Check if slot is populated
const cfg_layout_index_t *cfg_layout_get_index(void);    // Read-only pointer to in-memory index
```

### 1.2 — Rewrite `cfg_layouts.c`

**Key changes from the current implementation:**

1. **`s_psram_cache` allocation** changes from `KB_LAYER_COUNT` to `CFG_LAYOUT_MAX_COUNT` (16 slots, always pre-allocated).

2. **`cfg_layout_load_all()`:**
   - Allocate PSRAM cache for 16 slots (zero-initialized).
   - Try to load `lay_idx` from NVS.
   - **If `lay_idx` not found** → First boot initialization:
     - Build an initial index: `active_mask = 0x0001`, `names = {"Base"}`.
     - Save the `lay_idx` to NVS.
   - **If `lay_idx` found** → iterate `active_mask` bits, load each `ly_<N>` into `s_psram_cache[N]`.
   - Always: copy slot 0 → `s_dram_base`.

3. **`cfg_layout_get_action_code()`** — add bounds check `layer >= CFG_LAYOUT_MAX_COUNT` and check `cfg_layout_exists(layer)`. If the layer doesn't exist, fall through to base.

4. **`cfg_layout_create()`:**
   - Find first zero bit in `active_mask` (excluding bit 0). Return `ESP_ERR_NO_MEM` if all 16 slots full.
   - Initialize `s_psram_cache[id]` with all `KB_KEY_TRANSPARENT` (inherit base).
   - Set the name in `s_idx.names[id]`.
   - Set bit in `active_mask`.
   - Persist the layer blob (`ly_<id>`) and the updated index (`lay_idx`) to NVS.
   - Return the allocated `id` via `out_id`.

5. **`cfg_layout_delete()`:**
   - Reject `id == 0` (Base layer protection). Return `ESP_ERR_NOT_ALLOWED`.
   - Clear bit in `active_mask`.
   - Zero `s_psram_cache[id]` and `s_idx.names[id]`.
   - Persist updated index to NVS (the orphaned NVS blob is left for overwrite — matches the macro pattern).
   - If `id == s_swap_layer_idx`, reset `s_swap_layer_idx = 0xFF` and zero `s_dram_swap`.
   - Post `CONFIG_EVENT_KIND_UPDATED` so keyboard module can react (e.g., if deleted layer was active).

6. **`cfg_layout_rename()`:**
   - Reject `id == 0` (Base name is permanent).
   - Check `cfg_layout_exists(id)`.
   - Update `s_idx.names[id]`.
   - Persist updated index to NVS.
   - Post config update event.

7. **`layout_update_cb()`** — adapt to handle `ly_<N>` key pattern (parse the numeric ID from the key string) instead of matching against a fixed array.

---

## Phase 2 — Keyboard Module

### Files Modified

| File | Change |
|------|--------|
| [kb_layout.h](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/include/kb_layout.h) | Replace `KB_LAYER_COUNT 4` with `CFG_LAYOUT_MAX_COUNT`, remove fixed enum |
| [kb_layout.c](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_layout.c) | Shrink compile-time `keymaps[]` to **layer 0 only** (Base default) |
| [kb_macro.c](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_macro.c) | Generalize layer switching beyond FN1/FN2 booleans |
| [kb_system_action.h](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/include/kb_system_action.h) | Add `SYS_ACTION_LAYER_N` range for layers 0–15 |

### 2.1 — `kb_layout.h` Changes

```c
// BEFORE:
#define KB_LAYER_COUNT 4
typedef enum { KB_LAYER_BASE=0, KB_LAYER_FN1=1, KB_LAYER_FN2=2, KB_LAYER_FN3=3 } kb_layer_t;

// AFTER:
#define KB_LAYER_MAX       CFG_LAYOUT_MAX_COUNT  // 16
#define KB_LAYER_BASE      0

// Compile-time default: only Base layer
extern const uint16_t keymaps_base[KB_MATRIX_ROW_COUNT][KB_MATRIX_COL_COUNT];
```

> **Important:** `KB_LAYER_COUNT` is removed entirely. Code that used it for array bounds now uses `CFG_LAYOUT_MAX_COUNT` (the max possible) or queries `cfg_layout_get_count()` for the actual active count.

### 2.2 — `kb_layout.c` Changes

The compile-time `keymaps[4][6][18]` array is replaced with a single `keymaps_base[6][18]` — only the Base layer factory default is stored in flash. All other layers default to `KB_KEY_TRANSPARENT`.

### 2.3 — Layer Switching (`kb_macro.c`)

**Current model:** Two booleans `s_is_fn1_held`, `s_is_fn2_held` produce 4 layer states via `update_layer_state()`.

**New model:**

To avoid colliding with existing BLE and System actions (which occupy `ACTION_CODE_SYSTEM_MIN + 3` through `+32`), all new layer actions will be placed in a dedicated block starting at offset `0x40`. There is no backward compatibility with old code layouts.

The system will support four distinct types of layer actions for each of the 16 layers:

```c
// In kb_layout.h (or kb_system_action.h):
#define SYS_ACTION_LAYER_MOMENTARY_MIN (ACTION_CODE_SYSTEM_MIN + 0x40) // 0x2040 - 0x204F
#define SYS_ACTION_LAYER_TOGGLE_MIN    (ACTION_CODE_SYSTEM_MIN + 0x50) // 0x2050 - 0x205F
#define SYS_ACTION_LAYER_ON_MIN        (ACTION_CODE_SYSTEM_MIN + 0x60) // 0x2060 - 0x206F
#define SYS_ACTION_LAYER_OFF_MIN       (ACTION_CODE_SYSTEM_MIN + 0x70) // 0x2070 - 0x207F

// Utility checks:
#define IS_LAYER_MOMENTARY(a) ((a) >= SYS_ACTION_LAYER_MOMENTARY_MIN && (a) <= SYS_ACTION_LAYER_MOMENTARY_MIN + 15)
#define IS_LAYER_TOGGLE(a)    ((a) >= SYS_ACTION_LAYER_TOGGLE_MIN && (a) <= SYS_ACTION_LAYER_TOGGLE_MIN + 15)
#define IS_LAYER_ON(a)        ((a) >= SYS_ACTION_LAYER_ON_MIN && (a) <= SYS_ACTION_LAYER_ON_MIN + 15)
#define IS_LAYER_OFF(a)       ((a) >= SYS_ACTION_LAYER_OFF_MIN && (a) <= SYS_ACTION_LAYER_OFF_MIN + 15)
#define LAYER_ID_FROM_ACTION(a) ((uint8_t)((a) & 0x0F))
```

> **Configurator Note:** These actions should **never be visible** in the configuration UI's key picker *unless* the target layer actually exists in the current layout outline.

#### Layer Action Behaviors
1. **Momentary (Hold):** The default action. Activates the layer when the key is pressed and deactivates it upon release. 
2. **Toggle:** Sets the layer active on press and keeps it active on release. Pressing the toggle key again while the layer is active will deactivate it, falling back to the highest remaining active layer (or base).
3. **Set Active (On):** Sets the layer active on press and does nothing on release.
4. **Set Inactive (Off):** Sets the layer inactive on press and does nothing on release. Often used as a "Set Base Layer" action to clear a specific toggled layer.

**Priority-based layer stack:**

Replace `s_is_fn1_held` / `s_is_fn2_held` with two bitmasks `s_momentary_layers` and `s_toggled_layers` (`uint16_t`):

```c
static uint16_t s_momentary_layers = 0; // bit N set = layer N is being held
static uint16_t s_toggled_layers   = 0; // bit N set = layer N was toggled on

static void update_layer_state(void) {
    uint16_t active = s_momentary_layers | s_toggled_layers;
    // Highest active layer wins. If no layers active → base.
    if (active == 0) {
        s_active_layer = KB_LAYER_BASE;
    } else {
        // Find highest set bit. __builtin_clz is safe because active != 0.
        // NOTE: ESP32-S3 unsigned is 32-bit, so subtract from 31.
        s_active_layer = 31 - __builtin_clz((unsigned)active);
        
        // Validate that the layer actually exists
        if (!cfg_layout_exists(s_active_layer)) {
            // Fallback: find next highest that exists
            uint16_t valid = active;
            while (valid) {
                uint8_t top = 31 - __builtin_clz((unsigned)valid);
                if (cfg_layout_exists(top)) { s_active_layer = top; return; }
                valid &= ~(1u << top);
            }
            s_active_layer = KB_LAYER_BASE;
        }
    }
}
```

**In `process_system_action()`**, replace the two `SYS_ACTION_LAYER_FN1` / `SYS_ACTION_LAYER_FN2` checks with:

```c
    if (IS_LAYER_MOMENTARY(action)) {
        uint8_t layer_id = LAYER_ID_FROM_ACTION(action);
        if (is_pressed) s_momentary_layers |= (1u << layer_id);
        else            s_momentary_layers &= ~(1u << layer_id);
        update_layer_state();
        return;
    }

    if (IS_LAYER_TOGGLE(action)) {
        if (is_pressed) {
            uint8_t layer_id = LAYER_ID_FROM_ACTION(action);
            s_toggled_layers ^= (1u << layer_id); // Toggle the bit
            update_layer_state();
        }
        return;
    }

    if (IS_LAYER_ON(action)) {
        if (is_pressed) {
            uint8_t layer_id = LAYER_ID_FROM_ACTION(action);
            s_toggled_layers |= (1u << layer_id);
            update_layer_state();
        }
        return;
    }

    if (IS_LAYER_OFF(action)) {
        if (is_pressed) {
            uint8_t layer_id = LAYER_ID_FROM_ACTION(action);
            s_toggled_layers &= ~(1u << layer_id);
            update_layer_state();
        }
        return;
    }
```

### 2.4 — Deleted-layer Safety

If the currently active layer is deleted via the configurator while the user is typing:
- The `CONFIG_EVENT_KIND_UPDATED` handler in `cfg_layouts.c` invalidates the cache.
- `kb_macro.c` should also subscribe to `CONFIG_EVENT_KIND_UPDATED` for `CFGMOD_KIND_LAYOUT` events and call `update_layer_state()` to re-evaluate the active layer (will fall back to base if the held layer no longer exists).

---

## Phase 3 — Wire Protocol

### Files Modified

| File | Change |
|------|--------|
| [cfgmod.h](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/config_module/include/cfgmod.h) | New `cfgmod_key_id_t` entries |
| [cfgmod.c](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/config_module/cfgmod.c) | New handler blocks for layout CRUD |

### 3.1 — New Key IDs

Add to `cfgmod_key_id_t`:

```c
CFG_KEY_LAYOUTS       = 0x10,  // GET: layout outline (all IDs + names)
CFG_KEY_LAYOUT_SINGLE = 0x11,  // GET: full layer by {id}  |  SET: upsert/rename/delete
CFG_KEY_LAYOUT_LIMITS = 0x12,  // GET: {maxLayouts: 16}
```

### 3.2 — Wire Protocol Endpoints

#### `CFG_KEY_LAYOUTS` — GET

**Request:** `[cmd=GET, key_id=0x10]` (no payload)

**Response:**
```json
{
    "layouts": [
        { "id": 0, "name": "Base" },
        { "id": 1, "name": "FN1" },
        { "id": 5, "name": "Gaming" }
    ]
}
```

Returns only active layouts (bits set in `active_mask`), in ascending ID order.

#### `CFG_KEY_LAYOUT_LIMITS` — GET

**Request:** `[cmd=GET, key_id=0x12]` (no payload)

**Response:**
```json
{ "maxLayouts": 16 }
```

#### `CFG_KEY_LAYOUT_SINGLE` — GET

**Request:** `[cmd=GET, key_id=0x11, payload={"id": 5}]`

**Response:**
```json
{
    "id": 5,
    "name": "Gaming",
    "keys": [[...], [...], ...]
}
```

#### `CFG_KEY_LAYOUT_SINGLE` — SET (Create)

**Request:** `[cmd=SET, key_id=0x11, payload={"create": "My Layer"}]`

**Response:** Status only. On success, the new layout ID is included:
```json
{ "id": 7 }
```

The created layer is initialized with all `KB_KEY_TRANSPARENT`.

#### `CFG_KEY_LAYOUT_SINGLE` — SET (Update keys)

**Request:** `[cmd=SET, key_id=0x11, payload={"id": 5, "keys": [[...], ...]}]`

**Response:** Status only.

#### `CFG_KEY_LAYOUT_SINGLE` — SET (Rename)

**Request:** `[cmd=SET, key_id=0x11, payload={"id": 5, "rename": "New Name"}]`

**Response:** Status only.

#### `CFG_KEY_LAYOUT_SINGLE` — SET (Delete)

**Request:** `[cmd=SET, key_id=0x11, payload={"delete": 5}]`

**Response:** Status only. Attempting to delete id=0 returns `ESP_ERR_NOT_ALLOWED`.

### 3.3 — Handler Implementation in `cfgmod.c`

Add handler blocks following the same pattern as macros/combos/custom keys:

```c
// Layout outline
} else if (hdr.key_id == CFG_KEY_LAYOUTS && hdr.cmd == CFG_CMD_GET) {
    write_json_response(layouts_serialize_outline(),
                        out_payload, out_payload_max, status_size,
                        &status, &actual_payload_len);

// Layout limits
} else if (hdr.key_id == CFG_KEY_LAYOUT_LIMITS && hdr.cmd == CFG_CMD_GET) {
    write_json_response(layouts_serialize_limits(),
                        out_payload, out_payload_max, status_size,
                        &status, &actual_payload_len);

// Layout single GET
} else if (hdr.key_id == CFG_KEY_LAYOUT_SINGLE && hdr.cmd == CFG_CMD_GET) {
    // Parse {id: N}, call layouts_serialize_single(N)
    ...

// Layout single SET (create/update/rename/delete)
} else if (hdr.key_id == CFG_KEY_LAYOUT_SINGLE && hdr.cmd == CFG_CMD_SET) {
    // Dispatch based on JSON keys: "create", "delete", "rename", or "keys"
    ...
```

### 3.4 — Serialization Functions (in `cfg_layouts.c`)

Add these alongside the existing code:

```c
cJSON *layouts_serialize_outline(void);                    // {layouts: [{id, name}, ...]}
cJSON *layouts_serialize_single(uint8_t id);               // {id, name, keys: [[...]]}
cJSON *layouts_serialize_limits(void);                     // {maxLayouts: 16}
```

---

## Phase 4 — Configurator Sync

### Files Modified

| File | Change |
|------|--------|
| [protocol.ts](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/types/protocol.ts) | Add `CFG_KEY_LAYOUTS`, `CFG_KEY_LAYOUT_SINGLE`, `CFG_KEY_LAYOUT_LIMITS` |
| [DeviceController.ts](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/services/DeviceController.ts) | Add `fetchLayouts()`, `fetchLayoutSingle()`, `createLayout()`, `deleteLayout()`, `renameLayout()`, `saveLayout()` |
| [layoutStore.ts](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/stores/layoutStore.ts) | Dynamic layer list with names, variable count |
| [KeyboardLayoutEditor.tsx](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/KeyboardLayoutEditor.tsx) | Remove hardcoded `LAYER_COUNT`/`LAYER_NAMES`, add create/rename/delete UI |
| [HIDService.ts](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/HIDService.ts) | Export new key constants |
| [App.tsx](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/App.tsx) | Update layer fetch logic to use new endpoints |

### 4.1 — Protocol Constants (`protocol.ts`)

```typescript
export const CFG_KEY_LAYOUTS       = 0x10;
export const CFG_KEY_LAYOUT_SINGLE = 0x11;
export const CFG_KEY_LAYOUT_LIMITS = 0x12;
```

### 4.2 — DeviceController Methods

```typescript
// Fetch the outline of all existing layouts
async fetchLayouts(): Promise<{id: number, name: string}[]>

// Fetch a single layout's full keymap
async fetchLayoutSingle(id: number): Promise<{id: number, name: string, keys: number[][]}>

// Fetch layout limits
async fetchLayoutLimits(): Promise<{maxLayouts: number}>

// Create a new layout (returns the allocated ID)
async createLayout(name: string): Promise<number>

// Delete a layout by ID
async deleteLayout(id: number): Promise<void>

// Rename a layout
async renameLayout(id: number, newName: string): Promise<void>

// Save layer keymap data
async saveLayout(id: number, keys: number[][]): Promise<void>
```

### 4.3 — Layout Store Expansion

The `useLayoutStore` currently assumes a fixed 4-slot array. It must evolve to:

```typescript
interface LayoutMeta {
    id: number;
    name: string;
}

interface LayoutState {
    layoutMetas: LayoutMeta[];                    // Dynamic list of active layouts
    layerDataCache: Map<number, LayerData>;        // Loaded layer data (keyed by layout ID)
    activeLayerId: number;                         // Currently selected layout ID (not index)
    maxLayouts: number;                            // From firmware limits
    // ...existing fields (physicalLayout, pressedCodes, etc.)
}
```

### 4.4 — UI Changes (`KeyboardLayoutEditor.tsx`)

1. **Layer tabs** become **dynamic** — rendered from `layoutMetas` instead of hardcoded `LAYER_NAMES`.
2. **Base layer tab** has no rename/delete icons. All others show a rename (pencil) and delete (trash) icon.
3. A **"+" button** after the last tab triggers `createLayout()` → prompts for a name → adds the new tab.
4. **Delete** shows a confirmation dialog: "Delete layout 'Gaming'? This cannot be undone."
5. **Rename** shows an inline input field on the tab.
6. Layer fetch changes from `CFG_KEY_LAYER_0 + layerIdx` to `CFG_KEY_LAYOUT_SINGLE` with `{id: layoutId}`.
7. Layer save changes similarly.

---

## Phase 5 — Split Module

### Files Modified

| File | Change |
|------|--------|
| Split config sync | Must handle variable-count layouts during master→slave NVS mirroring |

The split module already fragments NVS blobs for sync. The change here is minimal:
- When syncing `CFGMOD_KIND_LAYOUT`, sync the `lay_idx` blob first, then sync each individual `ly_<N>` blob for active layouts.
- The slave's `layout_update_cb()` handles reloading after each blob arrives.

> **Note:** The existing split sync mechanism already operates on arbitrary NVS key/blob pairs. No structural changes are needed — only the set of keys to sync is now dynamic (driven by `active_mask`).

---

## Phase 6 — Documentation Updates

| Document | Changes |
|----------|---------|
| [README.md](file:///home/srleg/Projects/Tecleados-ESP-Firmware/README.md) | Update layer count from 4→16, add create/rename/delete to features, update Config Key ID table, update Layer System section |
| [CONFIG_MODULE.md (local)](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/config_module/CONFIG_MODULE.md) | Update NVS layout, add `lay_idx` documentation, update API table |
| [KEYBOARD_MODULE.md (local)](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/KEYBOARD_MODULE.md) | Update layer switching documentation, new action code range |
| [CONFIG_MODULE (universe)](file:///home/srleg/Projects/Tecleados-ESP-Firmware/universe/modules/CONFIG_MODULE.md) | Sync with local doc changes |
| [KEYBOARD_MODULE (universe)](file:///home/srleg/Projects/Tecleados-ESP-Firmware/universe/modules/KEYBOARD_MODULE.md) | Sync with local doc changes |
| [COMM_PROTOCOL.md](file:///home/srleg/Projects/Tecleados-ESP-Firmware/COMM_PROTOCOL.md) | Add new key IDs `0x10–0x12` |

---

## Phase 7 — Verification

### 7.1 — Build Verification

```bash
idf.py build    # Must compile with zero warnings
```

### 7.2 — Functional Tests

| Test | Expected Result |
|------|----------------|
| Fresh flash (no NVS) | Base layer loaded from `keymaps_base[]`. Only "Base" in outline. `lay_idx` created with `active_mask=0x0001`. |
| Create layout via configurator | New layout appears. PSRAM cache updated. NVS blob persisted. |
| Delete layout via configurator | Layout removed from outline. Cache invalidated. If active → falls back to base. |
| Rename layout via configurator | Name updated in index. No keymap data change. |
| Layer switch (hold key assigned to new layer) | Correct keymap activates from PSRAM cache. |
| Delete active layer while held | Active layer falls back to next highest held, or base. |
| Create 16th layout | Success. |
| Create 17th layout | Returns `ESP_ERR_NO_MEM`. UI shows "Maximum layouts reached". |
| Delete Base layout | Returns error. UI shows "Base layout cannot be deleted". |
| Rename Base layout | Returns error. UI shows "Base layout cannot be renamed". |
| Split sync | Slave receives updated layouts after master create/rename/delete. |

### 7.3 — Memory Verification

```bash
# Monitor heap during operation
idf.py -p PORT monitor
# Check: heap_caps_get_free_size(MALLOC_CAP_SPIRAM) after 16-layer allocation
# Check: heap_caps_get_free_size(MALLOC_CAP_INTERNAL) stays healthy
```

---

## Risk Assessment

| Risk                                                  | Mitigation                                                                |
| -------------------------------------------------------| ---------------------------------------------------------------------------|
| NVS key length overflow (`ly_15` = 5 chars, limit 15) | Well within limits                                                        |
| Held-layer deleted mid-press                          | `update_layer_state()` validates against `cfg_layout_exists()`            |
| PSRAM allocation failure                              | `cfg_layout_load_all()` already handles this with `ESP_ERR_NO_MEM` return |

---

## Execution Order (Recommended)

1. **Phase 0** → Pure design, no code
2. **Phase 1** → Firmware config module changes (foundation for everything)
3. **Phase 2** → Keyboard module changes (depends on Phase 1 API)
4. **Phase 3** → Wire protocol (depends on Phase 1 CRUD functions)
5. **Phase 7 (partial)** → Build verification after firmware phases
6. **Phase 4** → Configurator sync (depends on Phase 3 endpoints)
7. **Phase 5** → Split module (can be parallelized with Phase 4)
8. **Phase 6** → Documentation (after all code is stable)
9. **Phase 7 (full)** → End-to-end test
