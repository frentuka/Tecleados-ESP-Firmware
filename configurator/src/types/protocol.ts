/**
 * Protocol constants — single source of truth for the communication contract.
 * Must stay in sync with firmware usb_defs.h and cfgmod.h.
 */

// ── USB Device Identifiers ──────────────────────────────────────────────
export const VENDOR_ID = 0x303a;
export const PRODUCT_ID = 0x1324;
export const COMM_REPORT_ID = 3;
export const COMM_REPORT_SIZE = 63;
export const MAX_PAYLOAD_LENGTH = 58;

// ── Transport Flags ─────────────────────────────────────────────────────
export const PAYLOAD_FLAG_FIRST = 0x80;
export const PAYLOAD_FLAG_MID = 0x40;
export const PAYLOAD_FLAG_LAST = 0x20;
export const PAYLOAD_FLAG_ACK = 0x10;
export const PAYLOAD_FLAG_NAK = 0x08;

// Blast + Reconcile combined flags
export const PAYLOAD_FLAG_STATUS_REQ = 0x50; // MID|ACK
export const PAYLOAD_FLAG_BITMAP = 0x48; // MID|NAK

// Process flags
export const PAYLOAD_FLAG_OK = 0x04;
export const PAYLOAD_FLAG_ERR = 0x02;
export const PAYLOAD_FLAG_ABORT = 0x01;

// ── Module IDs ──────────────────────────────────────────────────────────
export const MODULE_CONFIG = 0x00;
export const MODULE_SYSTEM = 0x01;
export const MODULE_ACTION = 0x02;
export const MODULE_STATUS = 0x03;

// ── Config Commands ─────────────────────────────────────────────────────
export const CFG_CMD_GET = 0x00;
export const CFG_CMD_SET = 0x01;

// ── Config Key IDs (must match cfgmod_key_id_t in cfgmod.h) ─────────
export const CFG_KEY_TEST = 0x00;
export const CFG_KEY_HELLO = 0x01;
export const CFG_KEY_PHYSICAL_LAYOUT = 0x02;
export const CFG_KEY_LAYER_0 = 0x03;
export const CFG_KEY_LAYER_1 = 0x04;
export const CFG_KEY_LAYER_2 = 0x05;
export const CFG_KEY_LAYER_3 = 0x06;
export const CFG_KEY_MACROS = 0x07;
export const CFG_KEY_MACRO_LIMITS = 0x08;
export const CFG_KEY_MACRO_SINGLE = 0x09;
export const CFG_KEY_CKEYS = 0x0a;
export const CFG_KEY_CKEY_SINGLE = 0x0b;

// ── System Commands ─────────────────────────────────────────────────────
export const SYS_CMD_INJECT_KEY = 0x01;
export const SYS_CMD_CLEAR_INJECTED = 0x02;

// ── Action Code Ranges ──────────────────────────────────────────────────
export const ACTION_CODE_NONE = 0x0000;
export const ACTION_CODE_HID_MIN = 0x0001;
export const ACTION_CODE_HID_MAX = 0x00ff;
export const ACTION_CODE_MEDIA_MIN = 0x0100;
export const ACTION_CODE_MEDIA_MAX = 0x01ff;
export const ACTION_CODE_SYSTEM_MIN = 0x2000;
export const ACTION_CODE_SYSTEM_MAX = 0x20ff;
export const ACTION_CODE_CKEY_MIN = 0x3000;
export const ACTION_CODE_CKEY_MAX = 0x3fff;
export const ACTION_CODE_MACRO_MIN = 0x4000;
export const ACTION_CODE_MACRO_MAX = 0x4fff;
export const KB_KEY_TRANSPARENT = 0xffff;

export const MACRO_CODE_BASE = ACTION_CODE_MACRO_MIN;
export const CKEY_CODE_BASE = ACTION_CODE_CKEY_MIN;
export const CKEY_MAX_COUNT = 120;
