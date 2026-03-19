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
    CFG_CMD_GET,
    CFG_CMD_SET,
    CFG_KEY_MACROS,
    CFG_KEY_MACRO_LIMITS,
    CFG_KEY_MACRO_SINGLE,
    CFG_KEY_CKEYS,
    CFG_KEY_CKEY_SINGLE,
    SYS_CMD_INJECT_KEY,
    SYS_CMD_CLEAR_INJECTED,
} from '../types/protocol';

import type { CommandResponse, DeviceStatus } from '../types/device';
import type { Macro, MacroLimits } from '../types/macros';
import type { CustomKey } from '../types/customKeys';

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
        return this.transport.enqueueTask(() => this.transport.sendCustomCommReport(payload));
    }

    public async clearInjectedKeys(): Promise<boolean> {
        if (!this.isConnected()) return false;
        const payload = new Uint8Array([MODULE_SYSTEM, SYS_CMD_CLEAR_INJECTED]);
        return this.transport.enqueueTask(() => this.transport.sendCustomCommReport(payload));
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
}
