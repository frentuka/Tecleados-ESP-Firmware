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
import type { ITransport } from './ITransport';
import { useNotificationStore } from '../stores/notificationStore';
import {
    MODULE_CONFIG,
    MODULE_SYSTEM,
    MODULE_STATUS,
    MODULE_SPLIT,
    CFG_CMD_GET,
    CFG_CMD_SET,
    CFG_KEY_PHYSICAL_LAYOUT,
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
import { BinarySchema } from './BinarySchema';

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
    transparent_stack_fallback: boolean;
}

// Re-export transport for backward compatibility
export { HIDTransport };

export class DeviceController {
    public transport: ITransport;

    private connectionCallbacks: Set<(connected: boolean) => void> = new Set();
    private statusUpdateCallbacks: Set<(status: DeviceStatus) => void> = new Set();
    private logCallbacks: Set<(data: Uint8Array) => void> = new Set();
    private rawPacketCallbacks: Set<(data: Uint8Array, dir: 'rx' | 'tx') => void> = new Set();

    constructor(transport?: ITransport) {
        this.transport = transport || new HIDTransport();
    }

    public setTransport(newTransport: ITransport) {
        // Remove callbacks from old transport
        this.connectionCallbacks.forEach(cb => this.transport.offConnectionChange(cb));
        this.statusUpdateCallbacks.forEach(cb => this.transport.offStatusUpdate(cb));
        this.logCallbacks.forEach(cb => this.transport.offLogReceived(cb));
        this.rawPacketCallbacks.forEach(cb => this.transport.offRawPacket(cb));

        // Set new transport
        this.transport = newTransport;

        // Apply callbacks to new transport
        this.connectionCallbacks.forEach(cb => this.transport.onConnectionChange(cb));
        this.statusUpdateCallbacks.forEach(cb => this.transport.onStatusUpdate(cb));
        this.logCallbacks.forEach(cb => this.transport.onLogReceived(cb));
        this.rawPacketCallbacks.forEach(cb => this.transport.onRawPacket(cb));
    }

    // ── Delegate connection methods ─────────────────────────────────────

    public requestDevice() { return this.transport.requestDevice(); }
    public disconnect(forceReset?: boolean) { return this.transport.disconnect(forceReset); }
    public isConnected() { return this.transport.isConnected(); }
    public getDeviceName() { return this.transport.getDeviceName(); }

    // Connection observers
    public onConnectionChange(cb: (connected: boolean) => void) { 
        this.connectionCallbacks.add(cb);
        this.transport.onConnectionChange(cb); 
    }
    public offConnectionChange(cb: (connected: boolean) => void) { 
        this.connectionCallbacks.delete(cb);
        this.transport.offConnectionChange(cb); 
    }
    
    public onStatusUpdate(cb: (status: DeviceStatus) => void) { 
        this.statusUpdateCallbacks.add(cb);
        this.transport.onStatusUpdate(cb); 
    }
    public offStatusUpdate(cb: (status: DeviceStatus) => void) { 
        this.statusUpdateCallbacks.delete(cb);
        this.transport.offStatusUpdate(cb); 
    }
    
    public onLogReceived(cb: (data: Uint8Array) => void) { 
        this.logCallbacks.add(cb);
        this.transport.onLogReceived(cb); 
    }
    public offLogReceived(cb: (data: Uint8Array) => void) { 
        this.logCallbacks.delete(cb);
        this.transport.offLogReceived(cb); 
    }
    
    public onRawPacket(cb: (data: Uint8Array, dir: 'rx' | 'tx') => void) { 
        this.rawPacketCallbacks.add(cb);
        this.transport.onRawPacket(cb); 
    }
    public offRawPacket(cb: (data: Uint8Array, dir: 'rx' | 'tx') => void) { 
        this.rawPacketCallbacks.delete(cb);
        this.transport.offRawPacket(cb); 
    }

    // ── Low-level command ───────────────────────────────────────────────

    public async sendCommand(payload: Uint8Array, timeoutMs?: number): Promise<CommandResponse | null> {
        let fetchName: string | null = null;
        if (payload.length >= 8 && payload[0] === MODULE_CONFIG && payload[1] === CFG_CMD_GET) {
            const keyId = payload[2];
            const itemId = payload[3] | (payload[4] << 8);
            switch (keyId) {
                case CFG_KEY_PHYSICAL_LAYOUT: fetchName = 'physicalLayout'; break;
                case CFG_KEY_LAYOUTS: fetchName = 'layouts'; break;
                case CFG_KEY_LAYOUT_LIMITS: fetchName = 'layoutLimits'; break;
                case CFG_KEY_LAYOUT_SINGLE: fetchName = `layer_${itemId}`; break;
                case CFG_KEY_MACROS: fetchName = 'macros'; break;
                case CFG_KEY_MACRO_LIMITS: fetchName = 'macroLimits'; break;
                case CFG_KEY_MACRO_SINGLE: fetchName = `macro_${itemId}`; break;
                case CFG_KEY_CKEYS: fetchName = 'customKeys'; break;
                case CFG_KEY_CKEY_SINGLE: fetchName = `customKey_${itemId}`; break;
                case CFG_KEY_COMBOS: fetchName = 'combos'; break;
                case CFG_KEY_COMBO_LIMITS: fetchName = 'comboLimits'; break;
                case CFG_KEY_COMBO_SINGLE: fetchName = `combo_${itemId}`; break;
                case CFG_KEY_SYSTEM: fetchName = 'system'; break;
            }
        }

        if (fetchName) {
            // using dynamic import or accessing getState directly requires import
            useNotificationStore.getState().showNotification(`Fetching ${fetchName}...`, 'info');
        }

        const resp = await this.transport.sendCommand(payload, timeoutMs);

        if (fetchName) {
            if (resp && resp.status === 0) {
                // Delay clearing slightly so the user can actually see it flashed,
                // but only clear if we are still showing THIS fetch's notification.
                setTimeout(() => {
                    const current = useNotificationStore.getState().notification;
                    if (current && current.message === `Fetching ${fetchName}...`) {
                        useNotificationStore.getState().clearNotification();
                    }
                }, 300);
            } else {
                useNotificationStore.getState().showNotification(`Error fetching ${fetchName}`, 'error');
            }
        }
        return resp;
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

    private buildConfigPayload(cmd: number, keyId: number, itemId: number = 0, data?: Uint8Array): Uint8Array {
        const dataLen = data ? data.length : 0;
        const buf = new Uint8Array(8 + dataLen);
        buf[0] = MODULE_CONFIG;
        buf[1] = cmd;
        buf[2] = keyId;
        buf[3] = itemId & 0xFF;
        buf[4] = (itemId >> 8) & 0xFF;
        buf[5] = 0;
        buf[6] = 0;
        buf[7] = 0;
        if (data) buf.set(data, 8);
        return buf;
    }

    // ── Layouts ──────────────────────────────────────────────────────────

    public async fetchLayoutLimits(): Promise<{ maxLayouts: number } | null> {
        if (!this.isConnected()) return null;
        const buf = this.buildConfigPayload(CFG_CMD_GET, CFG_KEY_LAYOUT_LIMITS);
        const resp = await this.sendCommand(buf, 5000);
        if (resp && resp.status === 0 && resp.data.length >= 1) {
            const dv = new DataView(resp.data.buffer, resp.data.byteOffset, resp.data.byteLength);
            return BinarySchema.parseLayoutLimits(dv);
        }
        return null;
    }

    public async fetchLayouts(): Promise<{ order: number[], count: number } | null> {
        if (!this.isConnected()) return null;
        const buf = this.buildConfigPayload(CFG_CMD_GET, CFG_KEY_LAYOUTS);
        const resp = await this.sendCommand(buf, 5000);
        if (resp && resp.status === 0 && resp.data.length >= 404) {
            const dv = new DataView(resp.data.buffer, resp.data.byteOffset, resp.data.byteLength);
            return BinarySchema.parseLayoutIndex(dv);
        }
        return null;
    }

    public async reorderLayouts(order: number[]): Promise<boolean> {
        if (!this.isConnected()) return false;
        const data = new Uint8Array(16);
        for (let i = 0; i < Math.min(16, order.length); i++) data[i] = order[i];
        const buf = this.buildConfigPayload(CFG_CMD_SET, CFG_KEY_LAYOUTS, 0, data);
        const resp = await this.sendCommand(buf, 5000);
        return resp !== null && resp.status === 0;
    }

    public async fetchLayoutSingle(id: number): Promise<{ id: number; keys: number[][] } | null> {
        if (!this.isConnected()) return null;
        const buf = this.buildConfigPayload(CFG_CMD_GET, CFG_KEY_LAYOUT_SINGLE, id);
        const resp = await this.sendCommand(buf, 5000);
        if (resp && resp.status === 0 && resp.data.length >= 216) {
            const dv = new DataView(resp.data.buffer, resp.data.byteOffset, resp.data.byteLength);
            const keys = BinarySchema.parseLayer(dv, 6, 18);
            return { id, keys };
        }
        return null;
    }

    public async createLayout(name: string): Promise<number | null> {
        if (!this.isConnected()) return null;
        const data = new Uint8Array(24);
        const bytes = new TextEncoder().encode(name);
        data.set(bytes.slice(0, 23));
        const buf = this.buildConfigPayload(CFG_CMD_SET, CFG_KEY_LAYOUT_SINGLE, 0xFFFF, data);
        const resp = await this.sendCommand(buf, 5000);
        if (resp && resp.status === 0 && resp.data.length >= 2) {
            return new DataView(resp.data.buffer, resp.data.byteOffset, resp.data.byteLength).getUint16(0, true);
        }
        return null;
    }

    public async renameLayout(id: number, newName: string): Promise<boolean> {
        if (!this.isConnected()) return false;
        const data = new Uint8Array(24);
        const bytes = new TextEncoder().encode(newName);
        data.set(bytes.slice(0, 23));
        const buf = this.buildConfigPayload(CFG_CMD_SET, CFG_KEY_LAYOUT_SINGLE, id | 0x8000, data);
        const resp = await this.sendCommand(buf, 5000);
        return resp !== null && resp.status === 0;
    }

    public async deleteLayout(id: number): Promise<boolean> {
        if (!this.isConnected()) return false;
        const buf = this.buildConfigPayload(CFG_CMD_SET, CFG_KEY_LAYOUT_SINGLE, id, new Uint8Array(0));
        const resp = await this.sendCommand(buf, 5000);
        return resp !== null && resp.status === 0;
    }

    public async saveLayout(id: number, keys: number[][]): Promise<boolean> {
        if (!this.isConnected()) return false;
        const data = new Uint8Array(216);
        const dv = new DataView(data.buffer);
        BinarySchema.serializeLayer(keys, dv);
        const buf = this.buildConfigPayload(CFG_CMD_SET, CFG_KEY_LAYOUT_SINGLE, id, data);
        const resp = await this.sendCommand(buf, 10000);
        return resp !== null && resp.status === 0;
    }

    // ── Status ──────────────────────────────────────────────────────────

    public async fetchStatus(): Promise<DeviceStatus | null> {
        if (!this.isConnected()) return null;
        const resp = await this.sendCommand(new Uint8Array([MODULE_STATUS]), 2000);
        if (resp && resp.status === 0 && resp.data.length >= 10) {
            const dv = new DataView(resp.data.buffer, resp.data.byteOffset, resp.data.byteLength);
            let pairing = dv.getUint8(2);
            if (pairing === 255) pairing = -1;
            return {
                mode: dv.getUint8(0),
                profile: dv.getUint8(1),
                pairing,
                split_state: dv.getUint8(3),
                split_role: dv.getUint8(4),
                bitmap: dv.getUint16(8, true),
            };
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
        if (resp && resp.status === 0 && resp.data.length > 0) {
            return resp.data;
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
        await this.sendCommand(
            new Uint8Array([MODULE_SPLIT, SPLIT_CMD_GET_BENCH]), 1000
        );
        // splitGetBench is not migrated to binary yet, returning null for now
        // or we can implement the binary decoding if it was migrated. The plan doesn't mention migrating benchmark to binary.
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
        if (resp && resp.status === 0 && resp.data.length >= 95) {
            const dv = new DataView(resp.data.buffer, resp.data.byteOffset, resp.data.byteLength);
            return BinarySchema.parseDeviceIdentity(dv);
        }
        return null;
    }

    public async saveDeviceIdentity(identity: DeviceIdentity): Promise<boolean> {
        if (!this.isConnected()) return false;
        const data = new Uint8Array(96);
        const dv = new DataView(data.buffer);
        BinarySchema.serializeDeviceIdentity(identity, dv);
        const buf = this.buildConfigPayload(CFG_CMD_SET, CFG_KEY_SYSTEM, 0, data);
        const resp = await this.sendCommand(buf);
        return resp !== null && resp.status === 0;
    }

    // ── Macros ──────────────────────────────────────────────────────────

    public async fetchMacroLimits(): Promise<MacroLimits | null> {
        if (!this.isConnected()) return null;
        const buf = this.buildConfigPayload(CFG_CMD_GET, CFG_KEY_MACRO_LIMITS);
        const resp = await this.sendCommand(buf);
        if (resp && resp.status === 0 && resp.data.length >= 4) {
            const dv = new DataView(resp.data.buffer, resp.data.byteOffset, resp.data.byteLength);
            return { maxEvents: dv.getUint16(2, true), maxMacros: dv.getUint16(0, true) };
        }
        return null;
    }

    public async fetchMacroOutline(): Promise<number[]> {
        if (!this.isConnected()) return [];
        const buf = this.buildConfigPayload(CFG_CMD_GET, CFG_KEY_MACROS);
        const resp = await this.sendCommand(buf);
        if (resp && resp.status === 0 && resp.data.length >= 8) {
            const dv = new DataView(resp.data.buffer, resp.data.byteOffset, resp.data.byteLength);
            return BinarySchema.parseBitmask64(dv, 0);
        }
        return [];
    }

    public async fetchSingleMacro(id: number): Promise<Macro | null> {
        if (!this.isConnected()) return null;
        const buf = this.buildConfigPayload(CFG_CMD_GET, CFG_KEY_MACRO_SINGLE, id);
        const resp = await this.sendCommand(buf);
        if (resp && resp.status === 0 && resp.data.length >= 40) {
            const dv = new DataView(resp.data.buffer, resp.data.byteOffset, resp.data.byteLength);
            return BinarySchema.parseMacro(dv);
        }
        return null;
    }

    public async saveMacro(macro: Macro): Promise<boolean> {
        if (!this.isConnected()) return false;
        const data = new Uint8Array(4136);
        const dv = new DataView(data.buffer);
        BinarySchema.serializeMacro(macro, dv);
        const buf = this.buildConfigPayload(CFG_CMD_SET, CFG_KEY_MACRO_SINGLE, macro.id, data);
        const resp = await this.sendCommand(buf);
        return resp !== null && resp.status === 0;
    }

    public async deleteMacro(id: number): Promise<boolean> {
        if (!this.isConnected()) return false;
        const buf = this.buildConfigPayload(CFG_CMD_SET, CFG_KEY_MACRO_SINGLE, id, new Uint8Array(0));
        const resp = await this.sendCommand(buf);
        return resp !== null && resp.status === 0;
    }

    // ── Custom Keys ─────────────────────────────────────────────────────

    public async fetchCustomKeys(): Promise<number[]> {
        if (!this.isConnected()) return [];
        const buf = this.buildConfigPayload(CFG_CMD_GET, CFG_KEY_CKEYS);
        const resp = await this.sendCommand(buf, 5000);
        if (resp && resp.status === 0 && resp.data.length >= 15) {
            const dv = new DataView(resp.data.buffer, resp.data.byteOffset, resp.data.byteLength);
            return BinarySchema.parseBitmask120(dv, 0);
        }
        return [];
    }

    public async fetchCustomKeySingle(id: number): Promise<CustomKey | null> {
        if (!this.isConnected()) return null;
        const buf = this.buildConfigPayload(CFG_CMD_GET, CFG_KEY_CKEY_SINGLE, id);
        const resp = await this.sendCommand(buf, 5000);
        if (resp && resp.status === 0 && resp.data.length >= 72) {
            const dv = new DataView(resp.data.buffer, resp.data.byteOffset, resp.data.byteLength);
            return BinarySchema.parseCustomKey(dv);
        }
        return null;
    }

    public async saveCustomKey(ckey: CustomKey): Promise<boolean> {
        if (!this.isConnected()) return false;
        const data = new Uint8Array(72);
        const dv = new DataView(data.buffer);
        BinarySchema.serializeCustomKey(ckey, dv);
        const buf = this.buildConfigPayload(CFG_CMD_SET, CFG_KEY_CKEY_SINGLE, ckey.id, data);
        const resp = await this.sendCommand(buf, 10000);
        return resp !== null && resp.status === 0;
    }

    public async deleteCustomKey(id: number): Promise<boolean> {
        if (!this.isConnected()) return false;
        const buf = this.buildConfigPayload(CFG_CMD_SET, CFG_KEY_CKEY_SINGLE, id, new Uint8Array(0));
        const resp = await this.sendCommand(buf, 5000);
        return resp !== null && resp.status === 0;
    }

    // ── Combos ──────────────────────────────────────────────────────────

    public async fetchComboLimits(): Promise<{ maxCombos: number; maxKeys: number } | null> {
        if (!this.isConnected()) return null;
        const buf = this.buildConfigPayload(CFG_CMD_GET, CFG_KEY_COMBO_LIMITS);
        const resp = await this.sendCommand(buf, 5000);
        if (resp && resp.status === 0 && resp.data.length >= 2) {
            const dv = new DataView(resp.data.buffer, resp.data.byteOffset, resp.data.byteLength);
            return { maxCombos: dv.getUint8(0), maxKeys: dv.getUint8(1) };
        }
        return null;
    }

    public async fetchCombos(): Promise<number[]> {
        if (!this.isConnected()) return [];
        const buf = this.buildConfigPayload(CFG_CMD_GET, CFG_KEY_COMBOS);
        const resp = await this.sendCommand(buf, 5000);
        if (resp && resp.status === 0 && resp.data.length >= 4) {
            const dv = new DataView(resp.data.buffer, resp.data.byteOffset, resp.data.byteLength);
            return BinarySchema.parseBitmask32(dv, 0);
        }
        return [];
    }

    public async fetchSingleCombo(id: number): Promise<Combo | null> {
        if (!this.isConnected()) return null;
        const buf = this.buildConfigPayload(CFG_CMD_GET, CFG_KEY_COMBO_SINGLE, id);
        const resp = await this.sendCommand(buf, 5000);
        if (resp && resp.status === 0 && resp.data.length >= 64) {
            const dv = new DataView(resp.data.buffer, resp.data.byteOffset, resp.data.byteLength);
            return BinarySchema.parseCombo(dv);
        }
        return null;
    }

    public async saveCombo(combo: Combo): Promise<boolean> {
        if (!this.isConnected()) return false;
        const data = new Uint8Array(64);
        const dv = new DataView(data.buffer);
        BinarySchema.serializeCombo(combo, dv);
        const buf = this.buildConfigPayload(CFG_CMD_SET, CFG_KEY_COMBO_SINGLE, combo.id, data);
        const resp = await this.sendCommand(buf, 10000);
        return resp !== null && resp.status === 0;
    }

    public async deleteCombo(id: number): Promise<boolean> {
        if (!this.isConnected()) return false;
        const buf = this.buildConfigPayload(CFG_CMD_SET, CFG_KEY_COMBO_SINGLE, id, new Uint8Array(0));
        const resp = await this.sendCommand(buf, 5000);
        return resp !== null && resp.status === 0;
    }
}
