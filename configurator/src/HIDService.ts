/**
 * HIDService — Backward-compatible façade.
 *
 * Re-exports all protocol constants and types from the new module structure
 * so existing imports like `import { hidService, MODULE_CONFIG } from './HIDService'`
 * continue to work without changes.
 *
 * New code should import directly from:
 *   - types/protocol.ts   — protocol constants
 *   - types/device.ts     — device types
 *   - types/macros.ts     — macro types
 *   - types/customKeys.ts — custom key types
 *   - services/DeviceController.ts — high-level API
 *   - services/HIDTransport.ts     — low-level transport
 */

// Re-export all protocol constants
export {
    VENDOR_ID,
    PRODUCT_ID,
    COMM_REPORT_ID,
    COMM_REPORT_SIZE,
    MAX_PAYLOAD_LENGTH,
    PAYLOAD_FLAG_FIRST,
    PAYLOAD_FLAG_MID,
    PAYLOAD_FLAG_LAST,
    PAYLOAD_FLAG_ACK,
    PAYLOAD_FLAG_NAK,
    PAYLOAD_FLAG_STATUS_REQ,
    PAYLOAD_FLAG_BITMAP,
    PAYLOAD_FLAG_OK,
    PAYLOAD_FLAG_ERR,
    PAYLOAD_FLAG_ABORT,
    MODULE_CONFIG,
    MODULE_SYSTEM,
    MODULE_ACTION,
    MODULE_STATUS,
    CFG_CMD_GET,
    CFG_CMD_SET,
    CFG_KEY_TEST,
    CFG_KEY_HELLO,
    CFG_KEY_PHYSICAL_LAYOUT,
    CFG_KEY_LAYER_0,
    CFG_KEY_LAYER_1,
    CFG_KEY_LAYER_2,
    CFG_KEY_LAYER_3,
    CFG_KEY_MACROS,
    CFG_KEY_MACRO_LIMITS,
    CFG_KEY_MACRO_SINGLE,
    CFG_KEY_CKEYS,
    CFG_KEY_CKEY_SINGLE,
    SYS_CMD_INJECT_KEY,
    SYS_CMD_CLEAR_INJECTED,
    ACTION_CODE_NONE,
    ACTION_CODE_HID_MIN,
    ACTION_CODE_HID_MAX,
    ACTION_CODE_MEDIA_MIN,
    ACTION_CODE_MEDIA_MAX,
    ACTION_CODE_SYSTEM_MIN,
    ACTION_CODE_SYSTEM_MAX,
    ACTION_CODE_CKEY_MIN,
    ACTION_CODE_CKEY_MAX,
    ACTION_CODE_MACRO_MIN,
    ACTION_CODE_MACRO_MAX,
    KB_KEY_TRANSPARENT,
    MACRO_CODE_BASE,
    CKEY_CODE_BASE,
    CKEY_MAX_COUNT,
} from './types/protocol';

// Re-export device types
export type {
    CommandResponse,
    DeviceStatus,
    LogMessage,
    PhysKey,
    LayerData,
    LogCallback,
    RawPacketCallback,
    ConnectionCallback,
    StatusUpdateCallback,
} from './types/device';

// Re-export custom key types
export type {
    CustomKey,
    CustomKeyPR,
    CustomKeyMA,
} from './types/customKeys';

// Re-export macro types
export type {
    Macro,
    MacroElement,
    MacroAction,
    MacroLimits,
    ModeCategory,
} from './types/macros';

// Re-export transport utilities
export { computeCrc8 } from './services/HIDTransport';

// Create and export the singleton
import { DeviceController } from './services/DeviceController';
export const hidService = new DeviceController();
export { DeviceController };
