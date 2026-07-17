/**
 * DeviceController — High-level business logic for device communication.
 *
 * Wraps HIDTransport with typed command methods for:
 * - Config read/write (layers, physical layout, etc.)
 * - Macro management (fetch, save, delete)
 * - Custom key management
 * - Key injection (test mode)
 * - Status polling
 */

import { HIDTransport } from './HIDTransport';
import {
    MODULE_CONFIG,
    MODULE_SYSTEM,
    MODULE_STATUS,
    MODULE_SPLIT,
    CFG_CMD_GET,
    CFG_CMD_SET,
    CFG_KEY_LAYOUTS,
    CFG_KEY_LAYOUT_LIMITS,
    CFG_KEY_LAYOUT_SINGLE,
    CFG_KEY_MACROS,
    CFG_KEY_MACRO_LIMITS,
    CFG_KEY_MACRO_SINGLE,
    CFG_KEY_CKEYS,
    CFG_KEY_CKEY_SINGLE,
    CFG_KEY_COMBOS,
    CFG_KEY_COMBO_SINGLE,
    CFG_KEY_COMBO_LIMITS,
    SYS_CMD_INJECT_KEY,
    SYS_CMD_CLEAR_INJECTED,
    SPLIT_CMD_START_PAIRING,
    SPLIT_CMD_CANCEL_PAIRING,
    SPLIT_CMD_UNPAIR,
    SPLIT_CMD_GET_REMOTE_MATRIX,
    SPLIT_CMD_ROLE_SWAP,
    SPLIT_CMD_RUN_BENCH,
    SPLIT_CMD_GET_BENCH,
    CFG_KEY_SYSTEM,
    MODULE_BLE,
    BLE_CMD_TOGGLE_ROUTING,
    BLE_CMD_PAIR,
    BLE_CMD_CONNECT,
    BLE_CMD_TOGGLE_CONN,
} from '../types/protocol';

import type { CommandResponse, DeviceStatus } from '../types/device';
import type { Macro, MacroLimits } from '../types/macros';
import type { CustomKey } from '../types/customKeys';
import type { Combo } from '../types/combos';

// ── Device Identity ─────────────────────────────────────────────────────────
export interface DeviceIdentity {
    device_name: string;       // BLE advertised name (per-device fallback)
    is_split: boolean;          // Whether this unit is part of a split keyboard
    split_mirror_cols: boolean; // When true, column N maps to (COL_COUNT-1-N) — for mirrored right halves
    split_variant: string;      // e.g. "Left", "Right", "Numpad"
    // Shared BLE identity — set the same values on both halves so they present
    // as one device to the host and can hand off BLE connections on role swap.
    ble_shared_name: string;   // BLE advertised name override (empty = use device_name)
    ble_shared_addr: string;   // Shared static random BLE address "AA:BB:CC:DD:EE:FF" (empty = auto)
}

// Re-export transport for backward compatibility
export { HIDTransport };

export class DeviceController {
    public readonly transport: HIDTransport;

    constructor(transport?: HIDTransport) {
        this.transport = transport || new HIDTransport();
    }

    // ── Delegate connection methods ─────────────────────────────────────

    public requestDevice() { return this.transport.requestDevice(); }
    public disconnect(forceReset?: boolean) { return this.transport.disconnect(forceReset); }
    public isConnected() { return this.transport.isConnected(); }
    public getDeviceName() { return this.transport.getDeviceName(); }

    // Connection observers
    public onConnectionChange(cb: (connected: boolean) => void) { this.transport.onConnectionChange(cb); }
    public offConnectionChange(cb: (connected: boolean) => void) { this.transport.offConnectionChange(cb); }
    public onStatusUpdate(cb: (status: DeviceStatus) => void) { this.transport.onStatusUpdate(cb); }
    public offStatusUpdate(cb: (status: DeviceStatus) => void) { this.transport.offStatusUpdate(cb); }
    public onLogReceived(cb: (data: Uint8Array) => void) { this.transport.onLogReceived(cb); }
    public offLogReceived(cb: (data: Uint8Array) => void) { this.transport.offLogReceived(cb); }
    public onRawPacket(cb: (data: Uint8Array, dir: 'rx' | 'tx') => void) { this.transport.onRawPacket(cb); }
    public offRawPacket(cb: (data: Uint8Array, dir: 'rx' | 'tx') => void) { this.transport.offRawPacket(cb); }

    // ── Low-level command ───────────────────────────────────────────────

    public sendCommand(payload: Uint8Array, timeoutMs?: number): Promise<CommandResponse | null> {
        return this.transport.sendCommand(payload, timeoutMs);
    }

    public sendCustomCommReport(data: Uint8Array): Promise<boolean> {
        return this.transport.sendCustomCommReport(data);
    }

    public sendResponse(flags: number, data?: Uint8Array): Promise<boolean> {
        return this.transport.sendResponse(flags, data);
    }

    public buildCommPacket(flags: number, remaining: number, data: Uint8Array): Uint8Array {
        return this.transport.buildCommPacket(flags, remaining, data);
    }

    // ── Config helpers ──────────────────────────────────────────────────

    private buildConfigPayload(cmd: number, keyId: number, data?: Uint8Array): Uint8Array {
        const dataLen = data ? data.length : 0;
        const buf = new Uint8Array(3 + dataLen);
        buf[0] = MODULE_CONFIG;
        buf[1] = cmd;
        buf[2] = keyId;
        if (data) buf.set(data, 3);
        return buf;
    }

    // ── Layouts ──────────────────────────────────────────────────────────

    public async fetchLayoutLimits(): Promise<{ maxLayouts: number } | null> {
        if (!this.isConnected()) return null;
        const buf = this.buildConfigPayload(CFG_CMD_GET, CFG_KEY_LAYOUT_LIMITS);
        const resp = await this.sendCommand(buf, 5000);
        if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
            try {
                return JSON.parse(resp.jsonText);
            } catch (e) {
                console.error('fetchLayoutLimits parse error:', e);
            }
        }
        return null;
    }

    public async fetchLayouts(): Promise<{ id: number; name: string }[]> {
        if (!this.isConnected()) return [];
        const buf = this.buildConfigPayload(CFG_CMD_GET, CFG_KEY_LAYOUTS);
        const resp = await this.sendCommand(buf, 5000);
        if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
            try {
                const parsed = JSON.parse(resp.jsonText);
                return parsed.layouts || [];
            } catch (e) {
                console.error('fetchLayouts parse error:', e);
            }
        }
        return [];
    }

    public async fetchLayoutSingle(id: number): Promise<{ id: number; name: string; keys: number[][] } | null> {
        if (!this.isConnected()) return null;
        const requestJson = JSON.stringify({ id });
        const jsonBytes = new TextEncoder().encode(requestJson);
        const buf = this.buildConfigPayload(CFG_CMD_GET, CFG_KEY_LAYOUT_SINGLE, jsonBytes);
        const resp = await this.sendCommand(buf, 5000);
        if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
            try {
                return JSON.parse(resp.jsonText);
            } catch (e) {
                console.error('fetchLayoutSingle parse error:', e);
            }
        }
        return null;
    }

    public async createLayout(name: string): Promise<number | null> {
        if (!this.isConnected()) return null;
        const jsonBytes = new TextEncoder().encode(JSON.stringify({ create: name }));
        const buf = this.buildConfigPayload(CFG_CMD_SET, CFG_KEY_LAYOUT_SINGLE, jsonBytes);
        const resp = await this.sendCommand(buf, 5000);
        if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
            try {
                const parsed = JSON.parse(resp.jsonText);
                return parsed.id;
            } catch (e) {
                console.error('createLayout parse error:', e);
            }
        }
        return null;
    }

    public async renameLayout(id: number, newName: string): Promise<boolean> {
        if (!this.isConnected()) return false;
        const jsonBytes = new TextEncoder().encode(JSON.stringify({ id, rename: newName }));
        const buf = this.buildConfigPayload(CFG_CMD_SET, CFG_KEY_LAYOUT_SINGLE, jsonBytes);
        const resp = await this.sendCommand(buf, 5000);
        return resp !== null && resp.status === 0;
    }

    public async deleteLayout(id: number): Promise<boolean> {
        if (!this.isConnected()) return false;
        const jsonBytes = new TextEncoder().encode(JSON.stringify({ delete: id }));
        const buf = this.buildConfigPayload(CFG_CMD_SET, CFG_KEY_LAYOUT_SINGLE, jsonBytes);
        const resp = await this.sendCommand(buf, 5000);
        return resp !== null && resp.status === 0;
    }

    public async saveLayout(id: number, keys: number[][]): Promise<boolean> {
        if (!this.isConnected()) return false;
        const jsonBytes = new TextEncoder().encode(JSON.stringify({ id, keys }));
        const buf = this.buildConfigPayload(CFG_CMD_SET, CFG_KEY_LAYOUT_SINGLE, jsonBytes);
        const resp = await this.sendCommand(buf, 10000);
        return resp !== null && resp.status === 0;
    }

    // ── Status ──────────────────────────────────────────────────────────

    public async fetchStatus(): Promise<DeviceStatus | null> {
        if (!this.isConnected()) return null;
        const resp = await this.sendCommand(new Uint8Array([MODULE_STATUS]), 2000);
        if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
            try {
                const data = JSON.parse(resp.jsonText);
                return {
                    mode: data.mode,
                    profile: data.profile,
                    pairing: data.pairing ?? -1,
                    bitmap: data.bitmap,
                    split_state: data.split_state ?? 0,
                    split_role: data.split_role ?? 0,
                };
            } catch (e) {
                console.error('Failed to parse status JSON:', e);
            }
        }
        return null;
    }

    // ── Key Injection (Test Mode) ───────────────────────────────────────

    public async sendInjectKey(row: number, col: number, state: boolean): Promise<boolean> {
        if (!this.isConnected()) return false;
        const payload = new Uint8Array([MODULE_SYSTEM, SYS_CMD_INJECT_KEY, row, col, state ? 1 : 0]);
        return this.transport.sendCustomCommReport(payload);
    }

    public async clearInjectedKeys(): Promise<boolean> {
        if (!this.isConnected()) return false;
        const payload = new Uint8Array([MODULE_SYSTEM, SYS_CMD_CLEAR_INJECTED]);
        return this.transport.sendCustomCommReport(payload);
    }

    // ── Split keyboard commands ─────────────────────────────────────────

    public async splitStartPairing(timeoutMs = 0): Promise<boolean> {
        if (!this.isConnected()) return false;
        const payload = new Uint8Array(6);
        payload[0] = MODULE_SPLIT;
        payload[1] = SPLIT_CMD_START_PAIRING;
        // Encode 4-byte LE timeout
        payload[2] = (timeoutMs >>> 0) & 0xFF;
        payload[3] = (timeoutMs >>> 8) & 0xFF;
        payload[4] = (timeoutMs >>> 16) & 0xFF;
        payload[5] = (timeoutMs >>> 24) & 0xFF;
        return this.transport.sendCustomCommReport(payload);
    }

    public async splitCancelPairing(): Promise<boolean> {
        if (!this.isConnected()) return false;
        return this.transport.sendCustomCommReport(
            new Uint8Array([MODULE_SPLIT, SPLIT_CMD_CANCEL_PAIRING])
        );
    }

    public async splitUnpair(): Promise<boolean> {
        if (!this.isConnected()) return false;
        return this.transport.sendCustomCommReport(
            new Uint8Array([MODULE_SPLIT, SPLIT_CMD_UNPAIR])
        );
    }

    public async splitGetRemoteMatrix(): Promise<Uint8Array | null> {
        if (!this.isConnected()) return null;
        const resp = await this.sendCommand(
            new Uint8Array([MODULE_SPLIT, SPLIT_CMD_GET_REMOTE_MATRIX])
        );
        if (resp && resp.status === 0 && resp.jsonText.length > 0) {
            try {
                const parsed = JSON.parse(resp.jsonText) as number[];
                return new Uint8Array(parsed);
            } catch { /* fall through */ }
        }
        return null;
    }

    public async splitRoleSwap(): Promise<boolean> {
        if (!this.isConnected()) return false;
        return this.transport.sendCustomCommReport(
            new Uint8Array([MODULE_SPLIT, SPLIT_CMD_ROLE_SWAP])
        );
    }

    public async splitRunBenchmark(): Promise<boolean> {
        if (!this.isConnected()) return false;
        return this.transport.sendCustomCommReport(
            new Uint8Array([MODULE_SPLIT, SPLIT_CMD_RUN_BENCH])
        );
    }

    public async splitGetBench(): Promise<{
        active: boolean;
        min: number;
        avg: number;
        max: number;
        lost: number;
        sent?: number;
        local_scan_hz?: number;
        local_floor_hz?: number;
        local_peak_hz?: number;
        remote_scan_hz?: number;
        remote_floor_hz?: number;
        remote_peak_hz?: number;
    } | null> {
        if (!this.isConnected()) return null;
        const resp = await this.sendCommand(
            new Uint8Array([MODULE_SPLIT, SPLIT_CMD_GET_BENCH]), 1000
        );
        if (resp && resp.status === 0 && resp.jsonText.length > 0) {
            try {
                return JSON.parse(resp.jsonText);
            } catch { /* fall through */ }
        }
        return null;
    }

    // ── BLE commands ───────────────────────────────────────────────────────
    // These work regardless of which half is USB-connected: if the connected
    // device is the slave it forwards the command to master over the split link.

    public async bleToggleRouting(): Promise<boolean> {
        if (!this.isConnected()) return false;
        return this.transport.sendCustomCommReport(
            new Uint8Array([MODULE_BLE, BLE_CMD_TOGGLE_ROUTING])
        );
    }

    public async blePair(profileId: number): Promise<boolean> {
        if (!this.isConnected()) return false;
        return this.transport.sendCustomCommReport(
            new Uint8Array([MODULE_BLE, BLE_CMD_PAIR, profileId])
        );
    }

    public async bleConnect(profileId: number): Promise<boolean> {
        if (!this.isConnected()) return false;
        return this.transport.sendCustomCommReport(
            new Uint8Array([MODULE_BLE, BLE_CMD_CONNECT, profileId])
        );
    }

    public async bleToggleConn(profileId: number): Promise<boolean> {
        if (!this.isConnected()) return false;
        return this.transport.sendCustomCommReport(
            new Uint8Array([MODULE_BLE, BLE_CMD_TOGGLE_CONN, profileId])
        );
    }

    // ── Device Identity ─────────────────────────────────────────────────────

    public async fetchDeviceIdentity(): Promise<DeviceIdentity | null> {
        if (!this.isConnected()) return null;
        const buf = this.buildConfigPayload(CFG_CMD_GET, CFG_KEY_SYSTEM);
        const resp = await this.sendCommand(buf);
        if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
            try {
                const d = JSON.parse(resp.jsonText);
                return {
                    device_name:      d.name             ?? 'Antigravity KB',
                    is_split:          d.is_split           ?? false,
                    split_mirror_cols: d.split_mirror_cols ?? false,
                    split_variant:     d.split_variant     ?? '',
                    ble_shared_name:  d.ble_shared_name   ?? '',
                    ble_shared_addr:  d.ble_shared_addr   ?? '',
                };
            } catch (e) {
                console.error('fetchDeviceIdentity parse error:', e);
            }
        }
        return null;
    }

    public async saveDeviceIdentity(identity: DeviceIdentity): Promise<boolean> {
        if (!this.isConnected()) return false;
        const payload = {
            name:             identity.device_name,
            is_split:          identity.is_split,
            split_mirror_cols: identity.split_mirror_cols,
            split_variant:     identity.split_variant,
            ble_shared_name:  identity.ble_shared_name,
            ble_shared_addr:  identity.ble_shared_addr.toUpperCase(),
        };
        const jsonBytes = new TextEncoder().encode(JSON.stringify(payload));
        const buf = new Uint8Array(3 + jsonBytes.length);
        buf[0] = MODULE_CONFIG;
        buf[1] = CFG_CMD_SET;
        buf[2] = CFG_KEY_SYSTEM;
        buf.set(jsonBytes, 3);
        const resp = await this.sendCommand(buf);
        return resp !== null && resp.status === 0;
    }

    // ── Macros ──────────────────────────────────────────────────────────

    public async fetchMacroLimits(): Promise<MacroLimits | null> {
        if (!this.isConnected()) return null;
        const buf = this.buildConfigPayload(CFG_CMD_GET, CFG_KEY_MACRO_LIMITS);
        const resp = await this.sendCommand(buf);
        if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
            try {
                const parsed = JSON.parse(resp.jsonText);
                if (parsed.maxEvents && parsed.maxMacros) {
                    return { maxEvents: parsed.maxEvents, maxMacros: parsed.maxMacros };
                }
            } catch (e) {
                console.error('Macro limits parse error:', e);
            }
        }
        return null;
    }

    public async fetchMacroOutline(): Promise<Macro[]> {
        if (!this.isConnected()) return [];
        const buf = this.buildConfigPayload(CFG_CMD_GET, CFG_KEY_MACROS);
        const resp = await this.sendCommand(buf);
        if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
            try {
                const parsed = JSON.parse(resp.jsonText);
                if (Array.isArray(parsed)) return parsed;
                if (parsed.macros && Array.isArray(parsed.macros)) return parsed.macros;
            } catch (e) {
                console.error('Macros parse error:', e);
            }
        }
        return [];
    }

    public async fetchSingleMacro(id: number): Promise<Macro | null> {
        if (!this.isConnected()) return null;
        const jsonStr = JSON.stringify({ id });
        const jsonBytes = new TextEncoder().encode(jsonStr);
        const buf = new Uint8Array(3 + jsonBytes.length);
        buf[0] = MODULE_CONFIG;
        buf[1] = CFG_CMD_GET;
        buf[2] = CFG_KEY_MACRO_SINGLE;
        buf.set(jsonBytes, 3);

        const resp = await this.sendCommand(buf);
        if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
            try {
                return JSON.parse(resp.jsonText) as Macro;
            } catch (e) {
                console.error('Single macro parse error:', e);
            }
        }
        return null;
    }

    public async saveMacro(macro: Macro): Promise<boolean> {
        if (!this.isConnected()) return false;
        const jsonBytes = new TextEncoder().encode(JSON.stringify(macro));
        const buf = new Uint8Array(3 + jsonBytes.length);
        buf[0] = MODULE_CONFIG;
        buf[1] = CFG_CMD_SET;
        buf[2] = CFG_KEY_MACRO_SINGLE;
        buf.set(jsonBytes, 3);

        const resp = await this.sendCommand(buf);
        return resp !== null && resp.status === 0;
    }

    public async deleteMacro(id: number): Promise<boolean> {
        if (!this.isConnected()) return false;
        const jsonBytes = new TextEncoder().encode(JSON.stringify({ delete: id }));
        const buf = new Uint8Array(3 + jsonBytes.length);
        buf[0] = MODULE_CONFIG;
        buf[1] = CFG_CMD_SET;
        buf[2] = CFG_KEY_MACRO_SINGLE;
        buf.set(jsonBytes, 3);

        const resp = await this.sendCommand(buf);
        return resp !== null && resp.status === 0;
    }

    // ── Custom Keys ─────────────────────────────────────────────────────

    public async fetchCustomKeys(): Promise<CustomKey[]> {
        if (!this.isConnected()) return [];
        const buf = this.buildConfigPayload(CFG_CMD_GET, CFG_KEY_CKEYS);
        const resp = await this.sendCommand(buf, 5000);
        if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
            try {
                const data = JSON.parse(resp.jsonText);
                return (data.customKeys ?? []) as CustomKey[];
            } catch (e) {
                console.error('fetchCustomKeys parse error:', e);
            }
        }
        return [];
    }

    public async fetchCustomKeySingle(id: number): Promise<CustomKey | null> {
        if (!this.isConnected()) return null;
        const requestJson = JSON.stringify({ id });
        const jsonBytes = new TextEncoder().encode(requestJson);
        const buf = new Uint8Array([MODULE_CONFIG, CFG_CMD_GET, CFG_KEY_CKEY_SINGLE, ...jsonBytes]);
        const resp = await this.sendCommand(buf, 5000);
        if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
            try {
                return JSON.parse(resp.jsonText) as CustomKey;
            } catch (e) {
                console.error('fetchCustomKeySingle parse error:', e);
            }
        }
        return null;
    }

    public async saveCustomKey(ckey: CustomKey): Promise<boolean> {
        if (!this.isConnected()) return false;
        const payload: Record<string, unknown> = {
            id: ckey.id,
            name: ckey.name,
            mode: ckey.mode,
        };
        if (ckey.mode === 0 && ckey.pr) {
            payload.pr = {
                pressAction: ckey.pr.pressAction,
                releaseAction: ckey.pr.releaseAction,
                pressDuration: ckey.pr.pressDuration,
                releaseDuration: ckey.pr.releaseDuration,
                waitForFinish: ckey.pr.waitForFinish,
                pressSustain: ckey.pr.pressSustain,
            };
        } else if (ckey.mode === 1 && ckey.ma) {
            payload.ma = {
                tapAction: ckey.ma.tapAction,
                doubleTapAction: ckey.ma.doubleTapAction,
                holdAction: ckey.ma.holdAction,
                doubleTapThreshold: ckey.ma.doubleTapThreshold,
                holdThreshold: ckey.ma.holdThreshold,
                tapDuration: ckey.ma.tapDuration,
                doubleTapDuration: ckey.ma.doubleTapDuration,
                holdDuration: ckey.ma.holdDuration,
                holdSustain: ckey.ma.holdSustain,
            };
        }
        const jsonBytes = new TextEncoder().encode(JSON.stringify(payload));
        const buf = new Uint8Array([MODULE_CONFIG, CFG_CMD_SET, CFG_KEY_CKEY_SINGLE, ...jsonBytes]);
        const resp = await this.sendCommand(buf, 10000);
        return resp !== null && resp.status === 0;
    }

    public async deleteCustomKey(id: number): Promise<boolean> {
        if (!this.isConnected()) return false;
        const jsonBytes = new TextEncoder().encode(JSON.stringify({ delete: id }));
        const buf = new Uint8Array([MODULE_CONFIG, CFG_CMD_SET, CFG_KEY_CKEY_SINGLE, ...jsonBytes]);
        const resp = await this.sendCommand(buf, 5000);
        return resp !== null && resp.status === 0;
    }

    // ── Combos ──────────────────────────────────────────────────────────

    public async fetchComboLimits(): Promise<{ maxCombos: number; maxKeys: number } | null> {
        if (!this.isConnected()) return null;
        const buf = this.buildConfigPayload(CFG_CMD_GET, CFG_KEY_COMBO_LIMITS);
        const resp = await this.sendCommand(buf, 5000);
        if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
            try {
                const parsed = JSON.parse(resp.jsonText);
                if (parsed.maxCombos && parsed.maxKeys) {
                    return { maxCombos: parsed.maxCombos, maxKeys: parsed.maxKeys };
                }
            } catch (e) {
                console.error('fetchComboLimits parse error:', e);
            }
        }
        return null;
    }

    public async fetchCombos(): Promise<Combo[]> {
        if (!this.isConnected()) return [];
        const buf = this.buildConfigPayload(CFG_CMD_GET, CFG_KEY_COMBOS);
        const resp = await this.sendCommand(buf, 5000);
        if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
            try {
                const data = JSON.parse(resp.jsonText);
                return (data.combos ?? []) as Combo[];
            } catch (e) {
                console.error('fetchCombos parse error:', e);
            }
        }
        return [];
    }

    public async fetchSingleCombo(id: number): Promise<Combo | null> {
        if (!this.isConnected()) return null;
        const requestJson = JSON.stringify({ id });
        const jsonBytes = new TextEncoder().encode(requestJson);
        const buf = new Uint8Array([MODULE_CONFIG, CFG_CMD_GET, CFG_KEY_COMBO_SINGLE, ...jsonBytes]);
        const resp = await this.sendCommand(buf, 5000);
        if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
            try {
                return JSON.parse(resp.jsonText) as Combo;
            } catch (e) {
                console.error('fetchSingleCombo parse error:', e);
            }
        }
        return null;
    }

    public async saveCombo(combo: Combo): Promise<boolean> {
        if (!this.isConnected()) return false;
        const jsonBytes = new TextEncoder().encode(JSON.stringify(combo));
        const buf = new Uint8Array([MODULE_CONFIG, CFG_CMD_SET, CFG_KEY_COMBO_SINGLE, ...jsonBytes]);
        const resp = await this.sendCommand(buf, 10000);
        return resp !== null && resp.status === 0;
    }

    public async deleteCombo(id: number): Promise<boolean> {
        if (!this.isConnected()) return false;
        const jsonBytes = new TextEncoder().encode(JSON.stringify({ delete: id }));
        const buf = new Uint8Array([MODULE_CONFIG, CFG_CMD_SET, CFG_KEY_COMBO_SINGLE, ...jsonBytes]);
        const resp = await this.sendCommand(buf, 5000);
        return resp !== null && resp.status === 0;
    }
}
