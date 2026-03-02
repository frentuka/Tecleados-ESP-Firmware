import { useState, useEffect, useCallback, useRef } from 'react';
import {
    hidService,
    MODULE_CONFIG,
    CFG_CMD_GET,
    CFG_CMD_SET,
    CFG_KEY_PHYSICAL_LAYOUT,
    CFG_KEY_LAYER_0,
} from './HIDService';
import {
    getKeyClass,
    getKeyName,
    BROWSER_CODE_TO_HID,
} from './KeyDefinitions';
import SearchableKeyModal from './SearchableKeyModal';
import type { Macro } from './App';

// ── Matrix dimensions (must match firmware) ──
const LAYER_COUNT = 4;
const LAYER_NAMES = ['Base', 'FN1', 'FN2', 'FN3'];

// ── Types ──
type LayerData = number[][]; // ROWS × COLS

interface KeyboardLayoutEditorProps {
    isConnected: boolean;
    isDeveloperMode: boolean;
    macros: Macro[];
    onLog: (text: string) => void;
}

// ── Factory default keymaps (mirrors keymaps[] in kb_layout.h) ──
// Values are standard USB HID usage codes + system action codes from firmware
// Matrix: 6 rows × 18 cols
const T = 0xFFFF; // KB_KEY_TRANSPARENT
const N = 0x00;   // HID_KEY_NONE (unused position)
const DEFAULT_KEYMAPS: LayerData[] = [
    // Layer 0: Base
    [
        [0x29, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2D, 0x2E, 0x2A, 0x49, N, N, N],
        [0x2B, 0x14, 0x1A, 0x08, 0x15, 0x17, 0x1C, 0x18, 0x0C, 0x12, 0x13, 0x2F, 0x30, 0x31, 0x4A, N, N, N],
        [0x39, 0x04, 0x16, 0x07, 0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F, 0x33, 0x34, 0x28, N, 0x4B, N, N, N],
        [0xE1, N, 0x1D, 0x1B, 0x06, 0x19, 0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0xE5, 0x52, 0x4E, N, N, N],
        [0xE0, 0xE3, 0xE2, N, N, 0x2C, N, N, N, 0xE6, 0x2001, 0x2002, 0x50, 0x51, 0x4F, N, N, N],
        [N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N],
    ],
    // Layer 1: FN1
    [
        [0x35, 0x2011, 0x2010, T, T, T, T, 0x2016, 0x2017, 0x2015, 0x2014, 0x2013, 0x2012, T, 0x4C, N, N, N],
        [T, T, T, T, T, T, T, T, T, T, T, T, T, T, 0x4D, N, N, N],
        [T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, N, N, N],
        [T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, N, N, N],
        [T, T, T, T, T, T, T, T, T, 0x65, T, T, T, T, T, N, N, N],
        [N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N],
    ],
    // Layer 2: FN2
    [
        [T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, N, N, N],
        [T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, N, N, N],
        [T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, N, N, N],
        [T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, N, N, N],
        [T, T, T, T, T, T, T, T, T, 0x65, T, T, T, T, T, N, N, N],
        [N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N],
    ],
    // Layer 3: FN3 (FN1+FN2)
    [
        [T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, N, N, N],
        [T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, N, N, N],
        [T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, N, N, N],
        [T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, N, N, N],
        [T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, N, N, N],
        [N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N],
    ],
];

// ── Physical layout (65% KLE) ──
// Each entry: { row, col, w } where w = width in KLE units (1u baseline)
// Keys consumed by wider neighbours (NONE slots) are omitted.
interface PhysKey { row: number; col: number; w: number; h: number; x: number; y: number; }
const DEFAULT_PHYSICAL_LAYOUT: PhysKey[][] = [
    // Row 0: ESC  1  2  3  4  5  6  7  8  9  0  -  =  BKSP(2u)  DEL
    [{ row: 0, col: 0, w: 1, h: 1, x: 0, y: 0 }, { row: 0, col: 1, w: 1, h: 1, x: 1, y: 0 }, { row: 0, col: 2, w: 1, h: 1, x: 2, y: 0 }, { row: 0, col: 3, w: 1, h: 1, x: 3, y: 0 }, { row: 0, col: 4, w: 1, h: 1, x: 4, y: 0 }, { row: 0, col: 5, w: 1, h: 1, x: 5, y: 0 }, { row: 0, col: 6, w: 1, h: 1, x: 6, y: 0 }, { row: 0, col: 7, w: 1, h: 1, x: 7, y: 0 }, { row: 0, col: 8, w: 1, h: 1, x: 8, y: 0 }, { row: 0, col: 9, w: 1, h: 1, x: 9, y: 0 }, { row: 0, col: 10, w: 1, h: 1, x: 10, y: 0 }, { row: 0, col: 11, w: 1, h: 1, x: 11, y: 0 }, { row: 0, col: 12, w: 1, h: 1, x: 12, y: 0 }, { row: 0, col: 13, w: 2, h: 1, x: 13, y: 0 }, { row: 0, col: 14, w: 1, h: 1, x: 15, y: 0 }],
    // Row 1: TAB(1.5u)  Q W E R T Y U I O P [ ] \(1.5u) HOME
    [{ row: 1, col: 0, w: 1.5, h: 1, x: 0, y: 1 }, { row: 1, col: 1, w: 1, h: 1, x: 1.5, y: 1 }, { row: 1, col: 2, w: 1, h: 1, x: 2.5, y: 1 }, { row: 1, col: 3, w: 1, h: 1, x: 3.5, y: 1 }, { row: 1, col: 4, w: 1, h: 1, x: 4.5, y: 1 }, { row: 1, col: 5, w: 1, h: 1, x: 5.5, y: 1 }, { row: 1, col: 6, w: 1, h: 1, x: 6.5, y: 1 }, { row: 1, col: 7, w: 1, h: 1, x: 7.5, y: 1 }, { row: 1, col: 8, w: 1, h: 1, x: 8.5, y: 1 }, { row: 1, col: 9, w: 1, h: 1, x: 9.5, y: 1 }, { row: 1, col: 10, w: 1, h: 1, x: 10.5, y: 1 }, { row: 1, col: 11, w: 1, h: 1, x: 11.5, y: 1 }, { row: 1, col: 12, w: 1, h: 1, x: 12.5, y: 1 }, { row: 1, col: 13, w: 1.5, h: 1, x: 13.5, y: 1 }, { row: 1, col: 14, w: 1, h: 1, x: 15, y: 1 }],
    // Row 2: CAPS(1.75u) A S D F G H J K L ; ' ENTER(2.25u) PGUP
    [{ row: 2, col: 0, w: 1.75, h: 1, x: 0, y: 2 }, { row: 2, col: 1, w: 1, h: 1, x: 1.75, y: 2 }, { row: 2, col: 2, w: 1, h: 1, x: 2.75, y: 2 }, { row: 2, col: 3, w: 1, h: 1, x: 3.75, y: 2 }, { row: 2, col: 4, w: 1, h: 1, x: 4.75, y: 2 }, { row: 2, col: 5, w: 1, h: 1, x: 5.75, y: 2 }, { row: 2, col: 6, w: 1, h: 1, x: 6.75, y: 2 }, { row: 2, col: 7, w: 1, h: 1, x: 7.75, y: 2 }, { row: 2, col: 8, w: 1, h: 1, x: 8.75, y: 2 }, { row: 2, col: 9, w: 1, h: 1, x: 9.75, y: 2 }, { row: 2, col: 10, w: 1, h: 1, x: 10.75, y: 2 }, { row: 2, col: 11, w: 1, h: 1, x: 11.75, y: 2 }, { row: 2, col: 12, w: 2.25, h: 1, x: 12.75, y: 2 }, { row: 2, col: 14, w: 1, h: 1, x: 15, y: 2 }],
    // Row 3: LSHIFT(2.25u) Z X C V B N M , . / RSHIFT(1.75u) ↑ PGDN
    [{ row: 3, col: 0, w: 2.25, h: 1, x: 0, y: 3 }, { row: 3, col: 2, w: 1, h: 1, x: 2.25, y: 3 }, { row: 3, col: 3, w: 1, h: 1, x: 3.25, y: 3 }, { row: 3, col: 4, w: 1, h: 1, x: 4.25, y: 3 }, { row: 3, col: 5, w: 1, h: 1, x: 5.25, y: 3 }, { row: 3, col: 6, w: 1, h: 1, x: 6.25, y: 3 }, { row: 3, col: 7, w: 1, h: 1, x: 7.25, y: 3 }, { row: 3, col: 8, w: 1, h: 1, x: 8.25, y: 3 }, { row: 3, col: 9, w: 1, h: 1, x: 9.25, y: 3 }, { row: 3, col: 10, w: 1, h: 1, x: 10.25, y: 3 }, { row: 3, col: 11, w: 1, h: 1, x: 11.25, y: 3 }, { row: 3, col: 12, w: 1.75, h: 1, x: 12.25, y: 3 }, { row: 3, col: 13, w: 1, h: 1, x: 14, y: 3 }, { row: 3, col: 14, w: 1, h: 1, x: 15, y: 3 }],
    // Row 4: LCTRL(1.25u) LGUI(1.25u) LALT(1.25u) SPACE(6.25u) RALT FN1 FN2 ← ↓ →
    [{ row: 4, col: 0, w: 1.25, h: 1, x: 0, y: 4 }, { row: 4, col: 1, w: 1.25, h: 1, x: 1.25, y: 4 }, { row: 4, col: 2, w: 1.25, h: 1, x: 2.5, y: 4 }, { row: 4, col: 5, w: 6.25, h: 1, x: 3.75, y: 4 }, { row: 4, col: 9, w: 1, h: 1, x: 10, y: 4 }, { row: 4, col: 10, w: 1, h: 1, x: 11, y: 4 }, { row: 4, col: 11, w: 1, h: 1, x: 12, y: 4 }, { row: 4, col: 12, w: 1, h: 1, x: 13, y: 4 }, { row: 4, col: 13, w: 1, h: 1, x: 14, y: 4 }, { row: 4, col: 14, w: 1, h: 1, x: 15, y: 4 }],
];

// Parse physical layout JSON from device
// Format: {"rows":6,"cols":18,"layout":[[row,col,w100,h100,x100,y100,...],[...]]}
function parsePhysicalLayoutJson(jsonText: string): PhysKey[][] | null {
    try {
        const parsed = JSON.parse(jsonText);
        if (!parsed.layout || !Array.isArray(parsed.layout)) return null;
        return parsed.layout.map((visualRow: number[]) => {
            const keys: PhysKey[] = [];
            for (let i = 0; i + 5 < visualRow.length; i += 6) {
                keys.push({
                    row: visualRow[i],
                    col: visualRow[i + 1],
                    w: visualRow[i + 2] / 100,
                    h: visualRow[i + 3] / 100,
                    x: visualRow[i + 4] / 100,
                    y: visualRow[i + 5] / 100
                });
            }
            return keys;
        });
    } catch {
        return null;
    }
}

// Serialize physical layout to JSON for device storage (6-tuple per key)
function serializePhysicalLayout(layout: PhysKey[][], matrixRows: number, matrixCols: number): string {
    const flat = layout.map(visualRow =>
        visualRow.flatMap(k => [
            k.row,
            k.col,
            Math.round(k.w * 100),
            Math.round(k.h * 100),
            Math.round(k.x * 100),
            Math.round(k.y * 100)
        ])
    );
    return JSON.stringify({ rows: matrixRows, cols: matrixCols, layout: flat });
}

// ── KLE JSON Parser ──
// KLE raw data uses a relaxed format with unquoted keys like {w:2, c:"#ccc"}.
// We pre-process to make it valid JSON before parsing.
function parseKleJson(kleText: string): PhysKey[][] | null {
    try {
        let text = kleText.trim();

        // KLE raw data is rows like [row1],[row2],... NOT wrapped in outer [].
        // If it starts with [[ it's already a proper nested array.
        // Otherwise wrap it to create [[row1],[row2],...]
        if (!text.startsWith('[[')) {
            text = '[' + text + ']';
        }

        // Fix unquoted keys: {w:2} → {"w":2}, {c:"#ccc",w:1.5} → {"c":"#ccc","w":1.5}
        // Match word characters followed by : that aren't inside quotes
        text = text.replace(/({|,)\s*([a-zA-Z_]\w*)\s*:/g, '$1"$2":');

        // Fix hex color values without quotes: "#cccccc" is fine, but #cccccc needs quotes
        // Actually KLE uses "#cccccc" format which is already quoted, so this should be fine

        let data = JSON.parse(text);
        if (!Array.isArray(data)) return null;

        const result: PhysKey[][] = [];
        let matrixRow = 0;
        let currentY = 0;

        for (const row of data) {
            if (!Array.isArray(row)) continue;

            const physRow: PhysKey[] = [];
            let currentX = 0;
            let currentW = 1;
            let currentH = 1;
            let matrixCol = 0;

            for (const item of row) {
                if (typeof item === 'string') {
                    // This is a key
                    physRow.push({
                        row: matrixRow,
                        col: matrixCol,
                        w: currentW,
                        h: currentH,
                        x: currentX,
                        y: currentY
                    });
                    currentX += currentW;
                    matrixCol++;
                    // Reset per-key properties
                    currentW = 1;
                    currentH = 1;
                } else if (typeof item === 'object' && item !== null) {
                    // Property object — applies to next key(s)
                    if (item.w !== undefined) currentW = item.w;
                    if (item.h !== undefined) currentH = item.h;
                    if (item.x !== undefined) currentX += item.x;
                    if (item.y !== undefined) currentY += item.y;
                }
            }

            if (physRow.length > 0) {
                result.push(physRow);
                matrixRow++;
                currentY += 1; // move to next row by default
            }
        }

        return result.length > 0 ? result : null;
    } catch (e) {
        console.error('KLE parse error:', e);
        return null;
    }
}


export default function KeyboardLayoutEditor({ isConnected, isDeveloperMode, macros, onLog }: KeyboardLayoutEditorProps) {
    const [activeLayer, setActiveLayer] = useState(0);
    const [layers, setLayers] = useState<(LayerData | null)[]>([null, null, null, null]);
    const [layerStatus, setLayerStatus] = useState<('idle' | 'loading' | 'loaded' | 'error')[]>(['idle', 'idle', 'idle', 'idle']);
    const [physicalLayout, setPhysicalLayout] = useState<PhysKey[][] | null>(null);
    const [physLayoutStatus, setPhysLayoutStatus] = useState<'idle' | 'loading' | 'loaded' | 'error'>('idle');
    const [showKleImport, setShowKleImport] = useState(false);
    const [kleInput, setKleInput] = useState('');
    const [kleError, setKleError] = useState<string | null>(null);
    const [selectedKey, setSelectedKey] = useState<{ row: number; col: number } | null>(null);
    const [isModalOpen, setIsModalOpen] = useState(false);
    const [isSaving, setIsSaving] = useState(false);
    const [hasChanges, setHasChanges] = useState<boolean[]>([false, false, false, false]);
    const [pressedCodes, setPressedCodes] = useState<Set<number>>(new Set());
    const [isMenuOpen, setIsMenuOpen] = useState(false);
    const menuRef = useRef<HTMLDivElement>(null);
    const fileInputRef = useRef<HTMLInputElement>(null);

    // ── Export/Import ──
    const exportLayout = async () => {
        const data = {
            version: 1,
            physicalLayout,
            layers,
            timestamp: new Date().toISOString()
        };
        const fileName = `${hidService.getDeviceName()}_${new Date().toISOString().slice(0, 10)}.json`;
        const jsonContent = JSON.stringify(data, null, 2);

        // Try File System Access API (Save As dialog)
        if ('showSaveFilePicker' in window) {
            try {
                const handle = await (window as any).showSaveFilePicker({
                    suggestedName: fileName,
                    types: [{
                        description: 'JSON Files',
                        accept: { 'application/json': ['.json'] },
                    }],
                });
                const writable = await handle.createWritable();
                await writable.write(jsonContent);
                await writable.close();
                onLogRef.current('Layout exported to JSON via Save As');
                return;
            } catch (err: any) {
                if (err.name === 'AbortError') return; // User cancelled
                console.error('showSaveFilePicker error:', err);
                // Fall back to legacy download
            }
        }

        // Legacy Fallback (extensionless fix included)
        const blob = new Blob([jsonContent], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = fileName;
        a.click();
        URL.revokeObjectURL(url);
        onLogRef.current('Layout exported to JSON (Download)');
    };

    const handleImportClick = () => {
        fileInputRef.current?.click();
    };

    const importLayout = (e: React.ChangeEvent<HTMLInputElement>) => {
        const file = e.target.files?.[0];
        if (!file) return;

        const reader = new FileReader();
        reader.onload = (event) => {
            try {
                const data = JSON.parse(event.target?.result as string);
                if (data.physicalLayout) {
                    setPhysicalLayout(data.physicalLayout);
                    setPhysLayoutStatus('loaded');
                }
                if (data.layers && Array.isArray(data.layers)) {
                    setLayers(data.layers);
                    setLayerStatus(data.layers.map((l: LayerData | null) => l ? 'loaded' : 'idle'));
                    setHasChanges(data.layers.map((l: LayerData | null) => l !== null));
                }
                onLogRef.current('Layout imported from JSON. Remember to save layers to device.');
            } catch (err) {
                onLogRef.current('Failed to parse layout JSON');
                console.error(err);
            }
        };
        reader.readAsText(file);
        // Reset input
        e.target.value = '';
    };


    // ── Global Key Listeners ──
    useEffect(() => {
        const syncModifiers = (e: KeyboardEvent) => {
            setPressedCodes(prev => {
                let changed = false;
                const next = new Set(prev);

                // Helper to sync a modifier group
                const sync = (isDown: boolean, codes: number[]) => {
                    if (!isDown) {
                        for (const c of codes) {
                            if (next.has(c)) {
                                next.delete(c);
                                changed = true;
                            }
                        }
                    }
                };

                sync(e.metaKey, [0xE3, 0xE7]); // LGUI, RGUI
                sync(e.shiftKey, [0xE1, 0xE5]); // LSHIFT, RSHIFT
                sync(e.ctrlKey, [0xE0, 0xE4]); // LCTRL, RCTRL
                sync(e.altKey, [0xE2, 0xE6]); // LALT, RALT

                return changed ? next : prev;
            });
        };

        const handleKeyDown = (e: KeyboardEvent) => {
            const hid = BROWSER_CODE_TO_HID[e.code];
            if (hid !== undefined) {
                setPressedCodes(prev => {
                    const next = new Set(prev);
                    next.add(hid);
                    return next;
                });
                // onLogRef.current(`Key Down: ${e.code} (0x${hid.toString(16)})`);
            }
            syncModifiers(e);
        };

        const handleKeyUp = (e: KeyboardEvent) => {
            const hid = BROWSER_CODE_TO_HID[e.code];
            if (hid !== undefined) {
                setPressedCodes(prev => {
                    const next = new Set(prev);
                    next.delete(hid);
                    return next;
                });
            }
            syncModifiers(e);
        };

        const handleBlur = () => {
            setPressedCodes(new Set());
            onLogRef.current('Window blurred - clearing active keys');
        };

        const handleVisibilityChange = () => {
            if (document.hidden) {
                setPressedCodes(new Set());
            }
        };
        const handleFocus = () => {
            setPressedCodes(new Set());
        };

        const handleMouseMove = (e: MouseEvent) => {
            syncModifiers(e as unknown as KeyboardEvent);
        };

        window.addEventListener('keydown', handleKeyDown);
        window.addEventListener('keyup', handleKeyUp);
        window.addEventListener('blur', handleBlur);
        window.addEventListener('focus', handleFocus);
        window.addEventListener('mousemove', handleMouseMove);
        document.addEventListener('visibilitychange', handleVisibilityChange);

        return () => {
            window.removeEventListener('keydown', handleKeyDown);
            window.removeEventListener('keyup', handleKeyUp);
            window.removeEventListener('blur', handleBlur);
            window.removeEventListener('focus', handleFocus);
            window.removeEventListener('mousemove', handleMouseMove);
            document.removeEventListener('visibilitychange', handleVisibilityChange);
        };
    }, []);

    // ── Click outside menu to close ──
    useEffect(() => {
        const handleClickOutside = (event: MouseEvent) => {
            if (menuRef.current && !menuRef.current.contains(event.target as Node)) {
                setIsMenuOpen(false);
            }
        };
        if (isMenuOpen) {
            document.addEventListener('mousedown', handleClickOutside);
        }
        return () => document.removeEventListener('mousedown', handleClickOutside);
    }, [isMenuOpen]);

    // Stabilize onLog with a ref so it never triggers dependency chains
    const onLogRef = useRef(onLog);
    onLogRef.current = onLog;

    // Guard: only auto-fetch layers once per connection
    const hasFetchedRef = useRef(false);

    // ── Build a config command payload ──
    // Wire format: [MODULE_CONFIG, cmd, keyId, ...data]
    // No payload_len field — the transport layer provides total length
    const buildConfigPayload = (cmd: number, keyId: number, data?: Uint8Array): Uint8Array => {
        const headerLen = 2; // cmd + keyId (no payload_len — transport provides length)
        const dataLen = data ? data.length : 0;
        const buf = new Uint8Array(1 + headerLen + dataLen);
        buf[0] = MODULE_CONFIG;
        buf[1] = cmd;
        buf[2] = keyId;
        if (data) buf.set(data, 3);
        return buf;
    };

    // ── Fetch a single layer from device ──
    const fetchLayer = useCallback(async (layerIdx: number): Promise<boolean> => {
        if (!isConnected) return false;

        setLayerStatus(prev => { const n = [...prev]; n[layerIdx] = 'loading'; return n; });

        const keyId = CFG_KEY_LAYER_0 + layerIdx;
        const payload = buildConfigPayload(CFG_CMD_GET, keyId);
        onLogRef.current(`Requested Layer ${layerIdx} (GET)`);

        const resp = await hidService.sendCommand(payload);

        if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
            try {
                const parsed = JSON.parse(resp.jsonText);
                if (parsed.keys && Array.isArray(parsed.keys)) {
                    setLayers(prev => {
                        const next = [...prev];
                        next[layerIdx] = parsed.keys;
                        return next;
                    });
                    setLayerStatus(prev => { const n = [...prev]; n[layerIdx] = 'loaded'; return n; });
                    onLogRef.current(`Layer ${layerIdx} loaded (${LAYER_NAMES[layerIdx]})`);
                    return true;
                }
            } catch (e) {
                console.error('Layout parse error:', e);
            }
        }

        setLayerStatus(prev => { const n = [...prev]; n[layerIdx] = 'error'; return n; });
        onLogRef.current(`Layer ${layerIdx} fetch failed`);
        return false;
    }, [isConnected]);

    // ── Fetch physical layout + all layers sequentially on connect ──
    useEffect(() => {
        if (!isConnected) {
            hasFetchedRef.current = false;
            return;
        }
        if (hasFetchedRef.current) return;
        hasFetchedRef.current = true;

        (async () => {
            // Fetch physical layout first
            setPhysLayoutStatus('loading');
            const plPayload = buildConfigPayload(CFG_CMD_GET, CFG_KEY_PHYSICAL_LAYOUT);
            console.log('[LayoutEditor] Fetching physical layout, payload:', Array.from(plPayload).map(b => b.toString(16).padStart(2, '0')).join(' '));
            onLogRef.current('Requested Physical Layout (GET)');

            const plResp = await hidService.sendCommand(plPayload);
            console.log('[LayoutEditor] Physical layout response:', plResp ? { status: plResp.status, cmd: plResp.cmd, keyId: plResp.keyId, jsonLen: plResp.jsonText.length, json: plResp.jsonText.substring(0, 100) } : 'NULL (timeout)');

            if (plResp && plResp.status === 0 && plResp.jsonText.trim().length > 0) {
                const parsed = parsePhysicalLayoutJson(plResp.jsonText);
                if (parsed) {
                    setPhysicalLayout(parsed);
                    setPhysLayoutStatus('loaded');
                    onLogRef.current('Physical layout loaded from device');
                } else {
                    setPhysLayoutStatus('error');
                    setPhysicalLayout(DEFAULT_PHYSICAL_LAYOUT);
                    onLogRef.current('Physical layout parse error - using default');
                }
            } else {
                setPhysLayoutStatus('error');
                setPhysicalLayout(DEFAULT_PHYSICAL_LAYOUT);
                onLogRef.current('Physical layout fetch failed - using default');
            }

            await new Promise(r => setTimeout(r, 100));

            // Then fetch all layers
            for (let i = 0; i < LAYER_COUNT; i++) {
                console.log(`[LayoutEditor] Fetching layer ${i}...`);
                await fetchLayer(i);
                await new Promise(r => setTimeout(r, 100));
            }
        })();
    }, [isConnected, fetchLayer]);

    // ── Save all modified layers sequentially ──
    const saveAllModifiedLayers = async () => {
        if (!isConnected) return;

        setIsSaving(true);
        const layersToSave = hasChanges.map((changed, i) => changed ? i : -1).filter(i => i !== -1);

        for (const layerIdx of layersToSave) {
            const layerData = layers[layerIdx];
            if (!layerData) continue;

            const keyId = CFG_KEY_LAYER_0 + layerIdx;
            const jsonStr = JSON.stringify({ keys: layerData });
            const jsonBytes = new TextEncoder().encode(jsonStr);
            const payload = buildConfigPayload(CFG_CMD_SET, keyId, jsonBytes);

            const resp = await hidService.sendCommand(payload);
            if (resp && resp.status === 0) {
                onLogRef.current(`Layer ${layerIdx} (${LAYER_NAMES[layerIdx]}) saved to device`);
                setHasChanges(prev => {
                    const next = [...prev];
                    next[layerIdx] = false;
                    return next;
                });
            } else {
                onLogRef.current(`Layer ${layerIdx} save failed`);
            }
            // Small delay between commands
            await new Promise(r => setTimeout(r, 50));
        }

        setIsSaving(false);
    };

    // ── Key edit handler ──
    const handleKeyChange = (row: number, col: number, newValue: number) => {
        setLayers(prev => {
            const next = [...prev];
            if (!next[activeLayer]) return next;
            const layerCopy = next[activeLayer]!.map(r => [...r]);
            layerCopy[row][col] = newValue;
            next[activeLayer] = layerCopy;
            return next;
        });
        setHasChanges(prev => {
            const next = [...prev];
            next[activeLayer] = true;
            return next;
        });
    };

    const currentLayer = layers[activeLayer];

    return (
        <div className="layout-editor">
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '0.5rem' }}>
                <h2 className="section-title">Keyboard Layout</h2>
                <div className="menu-container" ref={menuRef}>
                    <button className="btn-icon" onClick={() => setIsMenuOpen(!isMenuOpen)} title="Options">
                        <svg viewBox="0 0 24 24" width="24" height="24" fill="currentColor">
                            <path d="M12 8c1.1 0 2-.9 2-2s-.9-2-2-2-2 .9-2 2 .9 2 2 2zm0 2c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2zm0 6c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2z" />
                        </svg>
                    </button>
                    {isMenuOpen && (
                        <div className="dropdown-menu">
                            <button className="dropdown-item" onClick={() => { fetchLayer(activeLayer); setIsMenuOpen(false); }}>
                                <svg viewBox="0 0 24 24" width="18" height="18" fill="currentColor">
                                    <path d="M17.65 6.35C16.2 4.9 14.21 4 12 4c-4.42 0-7.99 3.58-7.99 8s3.57 8 7.99 8c3.73 0 6.84-2.55 7.73-6h-2.08c-.82 2.33-3.04 4-5.65 4-3.31 0-6-2.69-6-6s2.69-6 6-6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z" />
                                </svg>
                                Refresh
                            </button>
                            <button className="dropdown-item" onClick={() => { exportLayout(); setIsMenuOpen(false); }}>
                                <svg viewBox="0 0 24 24" width="18" height="18" fill="currentColor">
                                    <path d="M19 9h-4V3H9v6H5l7 7 7-7zM5 18v2h14v-2H5z" />
                                </svg>
                                Export full layout
                            </button>
                            <button className="dropdown-item" onClick={() => { handleImportClick(); setIsMenuOpen(false); }}>
                                <svg viewBox="0 0 24 24" width="18" height="18" fill="currentColor">
                                    <path d="M9 16h6v-6h4l-7-7-7 7h4v6zm-4 2h14v2H5v-2z" />
                                </svg>
                                Import full layout
                            </button>
                            {isDeveloperMode && (
                                <button className="dropdown-item" onClick={() => { setShowKleImport(true); setKleError(null); setIsMenuOpen(false); }}>
                                    <svg viewBox="0 0 24 24" width="18" height="18" fill="currentColor">
                                        <path d="M20 5H4c-1.1 0-1.99.9-1.99 2L2 17c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V7c0-1.1-.9-2-2-2zm-9 3h2v2h-2V8zm0 3h2v2h-2v-2zM8 8h2v2H8V8zm0 3h2v2H8v-2zM5 8h2v2H5V8zm0 3h2v2H5v-2zm9 7H8v-2h6v2zm0-5h2v2h-2v-2zm0-3h2v2h-2V8zm3 3h2v2h-2v-2zm0-3h2v2h-2V8z" />
                                    </svg>
                                    Import physical layout
                                </button>
                            )}
                            <div className="dropdown-divider" />
                            <button className="dropdown-item dropdown-item-danger" onClick={() => {
                                setLayers(prev => {
                                    const next = [...prev];
                                    next[activeLayer] = DEFAULT_KEYMAPS[activeLayer].map(r => [...r]);
                                    return next;
                                });
                                setHasChanges(prev => {
                                    const next = [...prev];
                                    next[activeLayer] = true;
                                    return next;
                                });
                                setIsMenuOpen(false);
                            }}>
                                <svg viewBox="0 0 24 24" width="18" height="18" fill="currentColor">
                                    <path d="M13 3c-4.97 0-9 4.03-9 9H1l3.89 3.89.07.14L9 12H6c0-3.87 3.13-7 7-7s7 3.13 7 7-3.13 7-7 7c-1.93 0-3.68-.79-4.94-2.06l-1.42 1.42C8.27 19.99 10.51 21 13 21c4.97 0 9-4.03 9-9s-4.03-9-9-9zm-1 5v5l4.28 2.54.72-1.21-3.5-2.08V8H12z" />
                                </svg>
                                Restore defaults
                            </button>
                        </div>
                    )}
                </div>
            </div>

            {/* KLE Import panel */}
            {showKleImport && (
                <div style={{ background: 'rgba(255,255,255,0.05)', borderRadius: '8px', padding: '1rem', marginBottom: '1rem', border: '1px solid var(--border-color)' }}>
                    <p style={{ margin: '0 0 0.5rem', fontSize: '0.85rem', opacity: 0.7 }}>
                        Paste KLE JSON here (from keyboard-layout-editor.com → Raw Data tab):
                    </p>
                    <textarea
                        value={kleInput}
                        onChange={(e) => { setKleInput(e.target.value); setKleError(null); }}
                        placeholder={'[\n  ["Esc","1","2",...,{"w":2},"Backspace"],\n  [{"w":1.5},"Tab","Q","W",...],\n  ...\n]'}
                        style={{
                            width: '100%', minHeight: '120px', fontFamily: 'monospace', fontSize: '0.8rem',
                            background: 'var(--bg-color)', color: 'var(--text-primary)', border: '1px solid var(--border-color)',
                            borderRadius: '4px', padding: '0.5rem', resize: 'vertical', boxSizing: 'border-box'
                        }}
                    />
                    {kleError && <p style={{ color: '#ff6b6b', margin: '0.5rem 0 0', fontSize: '0.85rem' }}>⚠ {kleError}</p>}
                    <div style={{ display: 'flex', gap: '0.5rem', marginTop: '0.5rem' }}>
                        <button
                            className="btn btn-success"
                            disabled={!kleInput.trim() || !isConnected}
                            onClick={async () => {
                                const parsed = parseKleJson(kleInput);
                                if (!parsed) {
                                    setKleError('Failed to parse KLE JSON. Make sure you copied the raw JSON data (array format).');
                                    return;
                                }

                                // Update local state
                                setPhysicalLayout(parsed);
                                setPhysLayoutStatus('loaded');

                                // Save to device
                                const jsonStr = serializePhysicalLayout(parsed, 6, 18);
                                const jsonBytes = new TextEncoder().encode(jsonStr);
                                console.log(`[LayoutEditor] KLE Apply: sending SET PHYSICAL_LAYOUT, json len=${jsonStr.length}, json preview:`, jsonStr.substring(0, 150));
                                const payload = buildConfigPayload(CFG_CMD_SET, CFG_KEY_PHYSICAL_LAYOUT, jsonBytes);
                                console.log(`[LayoutEditor] KLE Apply: payload total=${payload.length} bytes, header: ${Array.from(payload.slice(0, 4)).map(b => b.toString(16).padStart(2, '0')).join(' ')}`);

                                const resp = await hidService.sendCommand(payload);
                                console.log('[LayoutEditor] KLE Apply SET response:', resp ? { status: resp.status, statusHex: '0x' + resp.status.toString(16), cmd: resp.cmd, keyId: resp.keyId, jsonLen: resp.jsonText.length } : 'NULL (timeout)');

                                if (resp && resp.status === 0) {
                                    onLogRef.current(`KLE layout saved to device: ${parsed.length} rows, ${parsed.reduce((s, r) => s + r.length, 0)} keys (${jsonBytes.length} bytes)`);
                                } else {
                                    onLogRef.current(`KLE layout save failed (status: ${resp?.status ?? 'timeout'})`);
                                }
                                setShowKleImport(false);
                                setKleInput('');
                                setKleError(null);
                            }}
                        >
                            Apply Layout
                        </button>
                        <button className="btn" onClick={() => { setShowKleImport(false); setKleInput(''); setKleError(null); }}>
                            Cancel
                        </button>
                    </div>
                </div>
            )}

            {/* Layer tabs */}
            <div className="layer-tabs">
                {LAYER_NAMES.map((name, i) => (
                    <button
                        key={i}
                        className={`layer-tab ${activeLayer === i ? 'layer-tab-active' : ''} ${hasChanges[i] ? 'layer-tab-changed' : ''}`}
                        onClick={() => { setActiveLayer(i); setSelectedKey(null); }}
                    >
                        {name}
                        {hasChanges[i] && (
                            <span className="change-indicator" title="Unsaved changes">
                                <svg viewBox="0 0 24 24" width="12" height="12" fill="currentColor">
                                    <path d="M15 9H9v6h6V9zm-2 4h-2v-2h2v2zm8-2V9h-2V7c0-1.1-.9-2-2-2h-2V3h-2v2h-2V3H9v2H7c-1.1 0-2 .9-2 2v2H3v2h2v2H3v2h2v2c0 1.1.9 2 2 2h2v2h2v-2h2v2h2v-2h2c1.1 0 2-.9 2-2v-2h2v-2h-2v-2h2zm-4 6H7V7h10v10z" />
                                </svg>
                            </span>
                        )}
                    </button>
                ))}
            </div>

            {/* Physical layout status */}
            {physLayoutStatus === 'error' && (
                <div className="layout-placeholder" style={{ background: 'rgba(255,180,0,0.1)', color: '#ffb400', marginBottom: '0.5rem', fontSize: '0.85rem' }}>
                    ℹ No physical layout stored on this device yet — using built-in default. Click <strong>"📋 Import KLE Layout"</strong> above to set one.
                </div>
            )}

            {/* Keyboard visual */}
            {!currentLayer ? (
                <div className="layout-placeholder">
                    {!isConnected
                        ? 'Connect device to load layout.'
                        : layerStatus[activeLayer] === 'loading'
                            ? `Loading ${LAYER_NAMES[activeLayer]} layer from device...`
                            : layerStatus[activeLayer] === 'error'
                                ? `⚠ Failed to load ${LAYER_NAMES[activeLayer]} layer. Device may not support layout config yet. Try "Reset to Default" then "Save" to initialize.`
                                : `No data for ${LAYER_NAMES[activeLayer]} layer.`
                    }
                </div>
            ) : (
                <>
                    <div style={{ width: '100%', overflowX: 'auto', padding: '1rem 0' }}>
                        {(() => {
                            const layout = (physicalLayout || DEFAULT_PHYSICAL_LAYOUT);
                            let maxKeyX = 15;
                            let maxKeyY = 5;

                            layout.forEach(row => {
                                row.forEach(pk => {
                                    maxKeyX = Math.max(maxKeyX, pk.x + pk.w);
                                    maxKeyY = Math.max(maxKeyY, pk.y + pk.h);
                                });
                            });

                            return (
                                <div className="keyboard-grid" style={{
                                    position: 'relative',
                                    width: `${maxKeyX * 3.2}rem`,
                                    height: `${maxKeyY * 3.2}rem`,
                                    margin: '0 auto',
                                    padding: '0',
                                    boxSizing: 'content-box',
                                    minWidth: 'min-content'
                                }}>
                                    {layout.map((physRow: PhysKey[], ri: number) => (
                                        <div key={ri} className="keyboard-row">
                                            {physRow.map((pk: PhysKey) => {
                                                // Robust access to layer data with fallback for large physical layouts
                                                const rowData = currentLayer?.[pk.row];
                                                const code = (rowData && pk.col < rowData.length) ? rowData[pk.col] : 0;
                                                const isPressed = pressedCodes.has(code);

                                                return (
                                                    <button
                                                        key={`${pk.row}-${pk.col}`}
                                                        className={`keyboard-key ${getKeyClass(code)} ${selectedKey?.row === pk.row && selectedKey?.col === pk.col ? 'key-selected' : ''} ${isPressed ? 'key-pressed' : ''}`}
                                                        style={{
                                                            left: `${pk.x * 3.2}rem`,
                                                            top: `${pk.y * 3.2}rem`,
                                                            width: `${pk.w * 3.2 - 0.25}rem`,
                                                            height: `${pk.h * 3.2 - 0.25}rem`,
                                                            position: 'absolute'
                                                        }}
                                                        onClick={() => {
                                                            setSelectedKey({ row: pk.row, col: pk.col });
                                                            setIsModalOpen(true);
                                                        }}
                                                        title={`[${pk.row},${pk.col}] = 0x${code.toString(16).toUpperCase().padStart(4, '0')}`}
                                                    >
                                                        <span className="key-label">{getKeyName(code, macros)}</span>
                                                    </button>
                                                );
                                            })}
                                        </div>
                                    ))}
                                </div>
                            );
                        })()}
                    </div>

                    {/* Key Search Modal */}
                    {
                        isModalOpen && selectedKey && (
                            <SearchableKeyModal
                                currentValue={currentLayer[selectedKey.row][selectedKey.col]}
                                macros={macros}
                                onSelect={(newValue: number) => {
                                    handleKeyChange(selectedKey.row, selectedKey.col, newValue);
                                    setIsModalOpen(false);
                                    setSelectedKey(null);
                                }}
                                onClose={() => {
                                    setIsModalOpen(false);
                                    setSelectedKey(null);
                                }}
                            />
                        )
                    }


                    {/* Action buttons */}
                    <div className="layout-actions">
                        <input
                            type="file"
                            ref={fileInputRef}
                            style={{ display: 'none' }}
                            accept=".json"
                            onChange={importLayout}
                        />


                        {/* Universal Apply button */}
                        <div style={{ marginLeft: 'auto' }}>
                            <button
                                className={`btn ${hasChanges.some(c => c) ? 'btn-success btn-apply-active' : 'btn-apply-idle'}`}
                                disabled={!hasChanges.some(c => c) || isSaving}
                                onClick={saveAllModifiedLayers}
                                title="Apply all pending changes to device"
                            >
                                <svg viewBox="0 0 24 24" width="20" height="20" fill="currentColor">
                                    <path d="M15 9H9v6h6V9zm-2 4h-2v-2h2v2zm8-2V9h-2V7c0-1.1-.9-2-2-2h-2V3h-2v2h-2V3H9v2H7c-1.1 0-2 .9-2 2v2H3v2h2v2H3v2h2v2c0 1.1.9 2 2 2h2v2h2v-2h2v2h2v-2h2c1.1 0 2-.9 2-2v-2h2v-2h-2v-2h2zm-4 6H7V7h10v10z" />
                                </svg>
                                {isSaving ? 'Applying...' : 'Apply'}
                            </button>
                        </div>
                    </div>
                </>
            )}
        </div>
    );
}
