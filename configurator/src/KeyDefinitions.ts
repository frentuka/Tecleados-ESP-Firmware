// HID key name lookup
export const HID_KEY_NAMES: Record<number, string> = {
    0x00: 'NONE', 0x04: 'A', 0x05: 'B', 0x06: 'C', 0x07: 'D',
    0x08: 'E', 0x09: 'F', 0x0A: 'G', 0x0B: 'H', 0x0C: 'I',
    0x0D: 'J', 0x0E: 'K', 0x0F: 'L', 0x10: 'M', 0x11: 'N',
    0x12: 'O', 0x13: 'P', 0x14: 'Q', 0x15: 'R', 0x16: 'S',
    0x17: 'T', 0x18: 'U', 0x19: 'V', 0x1A: 'W', 0x1B: 'X',
    0x1C: 'Y', 0x1D: 'Z', 0x1E: '1', 0x1F: '2', 0x20: '3',
    0x21: '4', 0x22: '5', 0x23: '6', 0x24: '7', 0x25: '8',
    0x26: '9', 0x27: '0', 0x28: 'ENTER', 0x29: 'ESC',
    0x2A: 'BKSP', 0x2B: 'TAB', 0x2C: 'SPACE', 0x2D: '-',
    0x2E: '=', 0x2F: '[', 0x30: ']', 0x31: '\\',
    0x33: ';', 0x34: "'", 0x35: '`', 0x36: ',',
    0x37: '.', 0x38: '/', 0x39: 'CAPS',
    0x3A: 'F1', 0x3B: 'F2', 0x3C: 'F3', 0x3D: 'F4',
    0x3E: 'F5', 0x3F: 'F6', 0x40: 'F7', 0x41: 'F8',
    0x42: 'F9', 0x43: 'F10', 0x44: 'F11', 0x45: 'F12',
    0x46: 'PRTSC', 0x47: 'SCRLK', 0x48: 'PAUSE',
    0x49: 'INS', 0x4A: 'HOME', 0x4B: 'PGUP',
    0x4C: 'DEL', 0x4D: 'END', 0x4E: 'PGDN',
    0x4F: '→', 0x50: '←', 0x51: '↓', 0x52: '↑',
    0x65: 'MENU',
    0xE0: 'LCTRL', 0xE1: 'LSHIFT', 0xE2: 'LALT', 0xE3: 'LGUI',
    0xE4: 'RCTRL', 0xE5: 'RSHIFT', 0xE6: 'RALT', 0xE7: 'RGUI',
};

// System action code names (from kb_layout.h)
export const SYSTEM_ACTION_NAMES: Record<number, string> = {
    0x2000: 'L.BASE', 0x2001: 'L.FN1', 0x2002: 'L.FN2',
    0x2003: 'BLE ON', 0x2004: 'BLE OFF', 0x2005: 'BLE TG',
    0x2006: 'BLE 1', 0x2007: 'BLE 2', 0x2008: 'BLE 3',
    0x2009: 'BLE 4', 0x200A: 'BLE 5', 0x200B: 'BLE 6',
    0x200C: 'BLE 7', 0x200D: 'BLE 8', 0x200E: 'BLE 9',
    0x2010: 'BRI+', 0x2011: 'BRI-',
    0x2012: 'VOL+', 0x2013: 'VOL-', 0x2014: 'MUTE',
    0x2015: 'M.NXT', 0x2016: 'M.PRV', 0x2017: 'M.TOG',
    0x2018: 'RGB▸', 0x2019: 'RGB◂', 0x201A: 'RGB S+',
    0x201B: 'RGB S-', 0x201C: 'RGB B+', 0x201D: 'RGB B-',
};

export const TRANSPARENT = 0xFFFF;
export const MACRO_BASE  = 0x4000;
export const CKEY_BASE   = 0x3000;

export function getKeyClass(code: number): string {
    if (code === TRANSPARENT) return 'key-transparent';
    if (code === 0) return 'key-none';
    if (code >= 0x2000 && code <= 0x20FF) return 'key-system';
    if (code >= CKEY_BASE  && code <= 0x3FFF) return 'key-ckey';
    if (code >= MACRO_BASE && code <= 0x40FF) return 'key-macro';
    const isModifier = code >= 0xE0 && code <= 0xE7;
    const isAction = (code >= 0x28 && code <= 0x2B) || // Enter, Esc, Bksp, Tab
        (code >= 0x49 && code <= 0x52) || // Ins, Home, PgDn, Del, End, PgUp, Right, Left, Down, Up
        code === 0x39; // Caps
    if (isModifier || isAction) return 'key-modifier';
    return 'key-normal';
}

export const ALL_KEYS: { label: string; value: number }[] = [
    { label: '(Transparent ▽)', value: TRANSPARENT },
    { label: '(None)', value: 0x00 },
    // Letters
    ...Array.from({ length: 26 }, (_, i) => ({
        label: String.fromCharCode(65 + i),
        value: 0x04 + i,
    })),
    // Numbers
    ...Array.from({ length: 10 }, (_, i) => ({
        label: `${(i + 1) % 10}`,
        value: i === 9 ? 0x27 : 0x1E + i,
    })),
    // Symbols and special keys
    { label: 'Enter', value: 0x28 }, { label: 'Escape', value: 0x29 },
    { label: 'Backspace', value: 0x2A }, { label: 'Tab', value: 0x2B },
    { label: 'Space', value: 0x2C }, { label: '- _', value: 0x2D },
    { label: '= +', value: 0x2E }, { label: '[ {', value: 0x2F },
    { label: '] }', value: 0x30 }, { label: '\\ |', value: 0x31 },
    { label: '; :', value: 0x33 }, { label: "' \"", value: 0x34 },
    { label: '` ~', value: 0x35 }, { label: ', <', value: 0x36 },
    { label: '. >', value: 0x37 }, { label: '/ ?', value: 0x38 },
    { label: 'Caps Lock', value: 0x39 },
    // F-keys
    ...Array.from({ length: 12 }, (_, i) => ({
        label: `F${i + 1}`,
        value: 0x3A + i,
    })),
    // Nav
    { label: 'Print Screen', value: 0x46 }, { label: 'Scroll Lock', value: 0x47 },
    { label: 'Pause', value: 0x48 }, { label: 'Insert', value: 0x49 },
    { label: 'Home', value: 0x4A }, { label: 'Page Up', value: 0x4B },
    { label: 'Delete', value: 0x4C }, { label: 'End', value: 0x4D },
    { label: 'Page Down', value: 0x4E },
    { label: '→ (Arrow Right)', value: 0x4F }, { label: '← (Arrow Left)', value: 0x50 },
    { label: '↓ (Arrow Down)', value: 0x51 }, { label: '↑ (Arrow Up)', value: 0x52 },
    { label: 'Menu', value: 0x65 },
    // Modifiers
    { label: 'Left Ctrl', value: 0xE0 }, { label: 'Left Shift', value: 0xE1 },
    { label: 'Left Alt', value: 0xE2 }, { label: 'Left GUI', value: 0xE3 },
    { label: 'Right Ctrl', value: 0xE4 }, { label: 'Right Shift', value: 0xE5 },
    { label: 'Right Alt', value: 0xE6 }, { label: 'Right GUI', value: 0xE7 },
    // System actions
    { label: 'Layer Base', value: 0x2000 }, { label: 'Layer FN1', value: 0x2001 },
    { label: 'Layer FN2', value: 0x2002 },
    { label: 'Brightness +', value: 0x2010 }, { label: 'Brightness -', value: 0x2011 },
    { label: 'Volume +', value: 0x2012 }, { label: 'Volume -', value: 0x2013 },
    { label: 'Mute', value: 0x2014 },
    { label: 'Media Next', value: 0x2015 }, { label: 'Media Prev', value: 0x2016 },
    { label: 'Media Toggle', value: 0x2017 },
    { label: 'RGB Mode ▸', value: 0x2018 }, { label: 'RGB Mode ◂', value: 0x2019 },
    { label: 'RGB Speed +', value: 0x201A }, { label: 'RGB Speed -', value: 0x201B },
    { label: 'RGB Bright +', value: 0x201C }, { label: 'RGB Bright -', value: 0x201D },
    { label: 'BLE Toggle', value: 0x2005 },
    { label: 'BLE ON', value: 0x2003 }, { label: 'BLE OFF', value: 0x2004 },
    ...Array.from({ length: 9 }, (_, i) => ({
        label: `BLE ${i + 1}`,
        value: 0x2006 + i,
    })),
];

// ── Browser Code to HID mapping ──
export const BROWSER_CODE_TO_HID: Record<string, number> = {
    'KeyA': 0x04, 'KeyB': 0x05, 'KeyC': 0x06, 'KeyD': 0x07, 'KeyE': 0x08,
    'KeyF': 0x09, 'KeyG': 0x0A, 'KeyH': 0x0B, 'KeyI': 0x0C, 'KeyJ': 0x0D,
    'KeyK': 0x0E, 'KeyL': 0x0F, 'KeyM': 0x10, 'KeyN': 0x11, 'KeyO': 0x12,
    'KeyP': 0x13, 'KeyQ': 0x14, 'KeyR': 0x15, 'KeyS': 0x16, 'KeyT': 0x17,
    'KeyU': 0x18, 'KeyV': 0x19, 'KeyW': 0x1A, 'KeyX': 0x1B, 'KeyY': 0x1C,
    'KeyZ': 0x1D,
    'Digit1': 0x1E, 'Digit2': 0x1F, 'Digit3': 0x20, 'Digit4': 0x21, 'Digit5': 0x22,
    'Digit6': 0x23, 'Digit7': 0x24, 'Digit8': 0x25, 'Digit9': 0x26, 'Digit0': 0x27,
    'Enter': 0x28, 'Escape': 0x29, 'Backspace': 0x2A, 'Tab': 0x2B, 'Space': 0x2C,
    'Minus': 0x2D, 'Equal': 0x2E, 'BracketLeft': 0x2F, 'BracketRight': 0x30, 'Backslash': 0x31,
    'Semicolon': 0x33, 'Quote': 0x34, 'Backquote': 0x35, 'Comma': 0x36, 'Period': 0x37, 'Slash': 0x38,
    'CapsLock': 0x39,
    'F1': 0x3A, 'F2': 0x3B, 'F3': 0x3C, 'F4': 0x3D, 'F5': 0x3E, 'F6': 0x3F,
    'F7': 0x40, 'F8': 0x41, 'F9': 0x42, 'F10': 0x43, 'F11': 0x44, 'F12': 0x45,
    'PrintScreen': 0x46, 'ScrollLock': 0x47, 'Pause': 0x48, 'Insert': 0x49, 'Home': 0x4A,
    'PageUp': 0x4B, 'Delete': 0x4C, 'End': 0x4D, 'PageDown': 0x4E,
    'ArrowRight': 0x4F, 'ArrowLeft': 0x50, 'ArrowDown': 0x51, 'ArrowUp': 0x52,
    'ContextMenu': 0x65,
    'ControlLeft': 0xE0, 'ShiftLeft': 0xE1, 'AltLeft': 0xE2, 'MetaLeft': 0xE3,
    'ControlRight': 0xE4, 'ShiftRight': 0xE5, 'AltRight': 0xE6, 'MetaRight': 0xE7,
};

export function getKeyName(
    code: number,
    macros?: { id: number, name: string }[],
    customKeys?: { id: number, name: string }[]
): string {
    if (code === TRANSPARENT) return '▽';
    if (code === 0) return '';
    if (code >= CKEY_BASE && code <= 0x3FFF) {
        const id = code - CKEY_BASE;
        const ck = customKeys?.find(k => k.id === id);
        return ck ? `CK:${ck.name}` : `CK[${id}]`;
    }
    if (code >= MACRO_BASE && code <= 0x40FF) {
        const macro = macros?.find(m => (MACRO_BASE + m.id) === code);
        return macro ? macro.name : `M${code - MACRO_BASE}`;
    }
    if (SYSTEM_ACTION_NAMES[code]) return SYSTEM_ACTION_NAMES[code];
    if (HID_KEY_NAMES[code]) return HID_KEY_NAMES[code];
    return `0x${code.toString(16).toUpperCase().padStart(4, '0')}`;
}

export function getMacroKeyOptions(macros: { id: number, name: string }[]) {
    return macros.map(m => ({
        label: m.name,
        value: MACRO_BASE + m.id
    }));
}

export function getCKeyOptions(customKeys: { id: number, name: string }[]) {
    return customKeys.map(ck => ({
        label: `CK: ${ck.name}`,
        value: CKEY_BASE + ck.id
    }));
}
