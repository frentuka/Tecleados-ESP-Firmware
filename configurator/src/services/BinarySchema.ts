import type { DeviceIdentity } from './DeviceController';
import type { Macro, MacroElement } from '../types/macros';
import type { CustomKey } from '../types/customKeys';
import type { Combo } from '../types/combos';
import type { LayerData } from '../types/device';

/**
 * Utility to safely decode a null-terminated UTF-8 C-string from a DataView.
 */
function decodeCString(dv: DataView, offset: number, maxLength: number): string {
    let length = 0;
    while (length < maxLength && dv.getUint8(offset + length) !== 0) {
        length++;
    }
    const bytes = new Uint8Array(dv.buffer, dv.byteOffset + offset, length);
    return new TextDecoder().decode(bytes);
}

/**
 * Utility to safely encode a string to a null-terminated UTF-8 C-string into a DataView.
 */
function encodeCString(str: string, dv: DataView, offset: number, maxLength: number): void {
    const encoder = new TextEncoder();
    const bytes = encoder.encode(str);
    const writeLen = Math.min(bytes.length, maxLength - 1);
    for (let i = 0; i < writeLen; i++) {
        dv.setUint8(offset + i, bytes[i]);
    }
    dv.setUint8(offset + writeLen, 0); // Null terminator
}

export class BinarySchema {

    // ── SYSTEM ─────────────────────────────────────────────────────────────

    public static parseDeviceIdentity(dv: DataView): DeviceIdentity {
        return {
            device_name: decodeCString(dv, 0, 32),
            // sleep_timeout_ms: dv.getUint32(32, true),
            // rgb_brightness: dv.getUint8(36),
            // bluetooth_enabled: dv.getUint8(37) !== 0,
            is_split: dv.getUint8(38) !== 0,
            split_mirror_cols: dv.getUint8(39) !== 0,
            split_variant: decodeCString(dv, 40, 16),
            ble_shared_name: decodeCString(dv, 56, 32),
            ble_shared_addr: Array.from({ length: 6 }, (_, i) => dv.getUint8(88 + i).toString(16).padStart(2, '0')).join(':'),
            transparent_stack_fallback: dv.getUint8(94) !== 0,
        };
    }

    public static serializeDeviceIdentity(identity: DeviceIdentity, dv: DataView): void {
        encodeCString(identity.device_name, dv, 0, 32);
        dv.setUint8(38, identity.is_split ? 1 : 0);
        dv.setUint8(39, identity.split_mirror_cols ? 1 : 0);
        encodeCString(identity.split_variant, dv, 40, 16);
        encodeCString(identity.ble_shared_name, dv, 56, 32);
        
        const addrParts = identity.ble_shared_addr.split(':');
        for (let i = 0; i < 6; i++) {
            dv.setUint8(88 + i, parseInt(addrParts[i] || '0', 16));
        }
        dv.setUint8(94, identity.transparent_stack_fallback ? 1 : 0);
    }

    // ── LAYOUTS ────────────────────────────────────────────────────────────

    public static parseLayer(dv: DataView, rows: number, cols: number): LayerData {
        const layer: LayerData = [];
        let offset = 0;
        for (let r = 0; r < rows; r++) {
            const rowArr: number[] = [];
            for (let c = 0; c < cols; c++) {
                rowArr.push(dv.getUint16(offset, true));
                offset += 2;
            }
            layer.push(rowArr);
        }
        return layer;
    }

    public static serializeLayer(layer: LayerData, dv: DataView): void {
        let offset = 0;
        for (let r = 0; r < layer.length; r++) {
            for (let c = 0; c < layer[r].length; c++) {
                dv.setUint16(offset, layer[r][c], true);
                offset += 2;
            }
        }
    }

    public static parseLayoutLimits(dv: DataView): { maxLayouts: number } {
        // Assume returns uint8 for maxLayouts at offset 0
        return { maxLayouts: dv.getUint8(0) };
    }

    public static parseLayoutIndex(dv: DataView): { order: number[], count: number } {
        // cfg_layout_index_t: active_mask (uint16_t, offset 0), order (uint8_t[16], offset 2), names (char[16][24], offset 18)
        const activeMask = dv.getUint16(0, true);
        const order: number[] = [];
        let count = 0;
        for (let i = 0; i < 16; i++) {
            if ((activeMask & (1 << i)) !== 0) {
                count++;
            }
            order.push(dv.getUint8(2 + i));
        }
        return { order, count };
    }

    // ── MACROS ─────────────────────────────────────────────────────────────

    public static parseMacro(dv: DataView): Macro {
        const id = dv.getUint16(0, true);
        const eventCount = dv.getUint16(2, true);
        const execMode = dv.getUint8(4);
        const stackMax = dv.getUint8(5);
        const repeatCount = dv.getUint8(6);
        const name = decodeCString(dv, 8, 32);

        const elements: MacroElement[] = [];
        let offset = 40;
        for (let i = 0; i < eventCount && i < 256; i++) {
            const value = dv.getUint32(offset, true);
            const delay_ms = dv.getUint32(offset + 4, true);
            const press_duration_ms = dv.getUint32(offset + 8, true);
            const type = dv.getUint32(offset + 12, true);
            
            if (type === 1) {
                elements.push({ type: 'sleep', duration: delay_ms });
            } else {
                elements.push({ type: 'key', key: value, inlineSleep: delay_ms, pressTime: press_duration_ms });
            }
            offset += 16;
        }
        
        return {
            id,
            name,
            execMode,
            stackMax,
            repeatCount,
            elements
        };
    }

    public static serializeMacro(macro: Macro, dv: DataView) {
        dv.setUint16(0, macro.id, true);
        const eventCount = Math.min(macro.elements.length, 256);
        dv.setUint16(2, eventCount, true);
        dv.setUint8(4, macro.execMode ?? 0);
        dv.setUint8(5, macro.stackMax ?? 0);
        dv.setUint8(6, macro.repeatCount ?? 0);
        encodeCString(macro.name, dv, 8, 32);

        let offset = 40;
        for (let i = 0; i < eventCount; i++) {
            const el = macro.elements[i];
            if (el.type === 'sleep') {
                dv.setUint32(offset, 0, true);
                dv.setUint32(offset + 4, el.duration, true);
                dv.setUint32(offset + 8, 0, true);
                dv.setUint32(offset + 12, 1, true);
            } else {
                dv.setUint32(offset, el.key, true);
                dv.setUint32(offset + 4, el.inlineSleep || 0, true);
                dv.setUint32(offset + 8, el.pressTime || 0, true);
                dv.setUint32(offset + 12, 0, true);
            }
            offset += 16;
        }
    }

    // ── CUSTOM KEYS ────────────────────────────────────────────────────────

    public static parseCustomKey(dv: DataView): CustomKey {
        const id = dv.getUint16(36, true);
        const mode = dv.getUint8(38);
        const name = decodeCString(dv, 40, 32);
        const ck: CustomKey = { id, mode, name };

        if (mode === 0) {
            ck.pr = {
                pressAction: dv.getUint32(0, true),
                releaseAction: dv.getUint32(4, true),
                pressDuration: dv.getUint32(8, true),
                releaseDuration: dv.getUint32(12, true),
                waitForFinish: dv.getUint8(16) === 1,
                pressSustain: dv.getUint8(17) === 1,
            };
        } else if (mode === 1) {
            ck.ma = {
                tapAction: dv.getUint32(0, true),
                doubleTapAction: dv.getUint32(4, true),
                holdAction: dv.getUint32(8, true),
                doubleTapThreshold: dv.getUint32(12, true),
                holdThreshold: dv.getUint32(16, true),
                tapDuration: dv.getUint32(20, true),
                doubleTapDuration: dv.getUint32(24, true),
                holdDuration: dv.getUint32(28, true),
                holdSustain: dv.getUint8(32) === 1,
            };
        }
        return ck;
    }

    public static serializeCustomKey(ck: CustomKey, dv: DataView): void {
        dv.setUint16(36, ck.id, true);
        dv.setUint8(38, ck.mode);
        encodeCString(ck.name, dv, 40, 32);
        dv.setUint8(43, ck.mode);
        if (ck.mode === 0 && ck.pr) {
            dv.setUint32(0, ck.pr.pressAction ?? 0, true);
            dv.setUint32(4, ck.pr.releaseAction ?? 0, true);
            dv.setUint32(8, ck.pr.pressDuration ?? 0, true);
            dv.setUint32(12, ck.pr.releaseDuration ?? 0, true);
            dv.setUint8(16, ck.pr.waitForFinish ? 1 : 0);
            dv.setUint8(17, ck.pr.pressSustain ? 1 : 0);
        } else if (ck.mode === 1 && ck.ma) {
            dv.setUint32(0, ck.ma.tapAction ?? 0, true);
            dv.setUint32(4, ck.ma.doubleTapAction ?? 0, true);
            dv.setUint32(8, ck.ma.holdAction ?? 0, true);
            dv.setUint32(12, ck.ma.doubleTapThreshold ?? 0, true);
            dv.setUint32(16, ck.ma.holdThreshold ?? 0, true);
            dv.setUint32(20, ck.ma.tapDuration ?? 0, true);
            dv.setUint32(24, ck.ma.doubleTapDuration ?? 0, true);
            dv.setUint32(28, ck.ma.holdDuration ?? 0, true);
            dv.setUint8(32, ck.ma.holdSustain ? 1 : 0);
        }
    }

    // ── COMBOS ─────────────────────────────────────────────────────────────

    public static parseCombo(dv: DataView): Combo {
        const id = dv.getUint16(0, true);
        const delayMs = dv.getUint16(2, true);
        const action = dv.getUint32(4, true);
        
        const activeLayers = dv.getUint8(8);
        const strictOrder = dv.getUint8(12) === 1;
        const cancelKeys = dv.getUint8(13) === 1;
        const delayedPress = dv.getUint8(14) === 1;
        const releaseOnFirstKey = dv.getUint8(15) === 1;

        const keys: { row: number; col: number }[] = [];
        for (let i = 0; i < 4; i++) {
            const code = dv.getUint16(16 + i * 2, true);
            if (code !== 0) keys.push({ row: (code >> 8) & 0xFF, col: code & 0xFF });
        }

        return {
            id, name: '', action, delayMs, activeLayers: [activeLayers], strictOrder, cancelKeys, delayedPress, releaseOnFirstKey, keys
        };
    }

    public static serializeCombo(combo: Combo, dv: DataView): void {
        dv.setUint16(0, combo.id, true);
        dv.setUint16(2, combo.delayMs, true);
        dv.setUint32(4, combo.action, true);
        
        dv.setUint32(8, combo.activeLayers[0] ?? 0, true);
        dv.setUint8(12, combo.strictOrder ? 1 : 0);
        dv.setUint8(13, combo.cancelKeys ? 1 : 0);
        dv.setUint8(14, combo.delayedPress ? 1 : 0);
        dv.setUint8(15, combo.releaseOnFirstKey ? 1 : 0);
        encodeCString(combo.name, dv, 12, 32);

        let offset = 16;
        for (let i = 0; i < 4; i++) {
            if (i < combo.keys.length) {
                const code = ((combo.keys[i].row & 0xFF) << 8) | (combo.keys[i].col & 0xFF);
                dv.setUint16(offset, code, true);
            } else {
                dv.setUint16(offset, 0, true);
            }
            offset += 2;
        }
    }

    // ── BITMASKS ───────────────────────────────────────────────────────────

    public static parseBitmask64(dv: DataView, offset: number = 0): number[] {
        const mask = dv.getBigUint64(offset, true);
        const ids: number[] = [];
        for (let i = 0n; i < 64n; i++) {
            if ((mask & (1n << i)) !== 0n) {
                ids.push(Number(i));
            }
        }
        return ids;
    }
    
    public static parseBitmask32(dv: DataView, offset: number = 0): number[] {
        const mask = dv.getUint32(offset, true);
        const ids: number[] = [];
        for (let i = 0; i < 32; i++) {
            if ((mask & (1 << i)) !== 0) {
                ids.push(i);
            }
        }
        return ids;
    }

    public static parseBitmask120(dv: DataView, offset: number = 0): number[] {
        const ids: number[] = [];
        for (let i = 0; i < 15; i++) {
            const byte = dv.getUint8(offset + i);
            for (let bit = 0; bit < 8; bit++) {
                if ((byte & (1 << bit)) !== 0) {
                    ids.push(i * 8 + bit);
                }
            }
        }
        return ids;
    }
}
