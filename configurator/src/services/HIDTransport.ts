/**
 * HIDTransport — Low-level WebHID transport layer.
 *
 * Handles:
 * - WebHID device discovery, open, close, reconnect
 * - CRC-8 computation and packet building
 * - Blast + Reconcile TX/RX state machines
 * - Serial task queue to prevent protocol overlap
 * - Connection state observer pattern
 */

import {
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
} from '../types/protocol';

import type {
    LogCallback,
    RawPacketCallback,
    ConnectionCallback,
    StatusUpdateCallback,
    DeviceStatus,
    CommandResponse,
} from '../types/device';

import { MODULE_STATUS } from '../types/protocol';

// ── CRC-8 Table (polynomial 0x07, matches firmware usb_crc.c) ──────────
const CRC8_TABLE = new Uint8Array([
    0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15, 0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d,
    0x70, 0x77, 0x7e, 0x79, 0x6c, 0x6b, 0x62, 0x65, 0x48, 0x4f, 0x46, 0x41, 0x54, 0x53, 0x5a, 0x5d,
    0xe0, 0xe7, 0xee, 0xe9, 0xfc, 0xfb, 0xf2, 0xf5, 0xd8, 0xdf, 0xd6, 0xd1, 0xc4, 0xc3, 0xca, 0xcd,
    0x90, 0x97, 0x9e, 0x99, 0x8c, 0x8b, 0x82, 0x85, 0xa8, 0xaf, 0xa6, 0xa1, 0xb4, 0xb3, 0xba, 0xbd,
    0xc7, 0xc0, 0xc9, 0xce, 0xdb, 0xdc, 0xd5, 0xd2, 0xff, 0xf8, 0xf1, 0xf6, 0xe3, 0xe4, 0xed, 0xea,
    0xb7, 0xb0, 0xb9, 0xbe, 0xab, 0xac, 0xa5, 0xa2, 0x8f, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9d, 0x9a,
    0x27, 0x20, 0x29, 0x2e, 0x3b, 0x3c, 0x35, 0x32, 0x1f, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0d, 0x0a,
    0x57, 0x50, 0x59, 0x5e, 0x4b, 0x4c, 0x45, 0x42, 0x6f, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7d, 0x7a,
    0x89, 0x8e, 0x87, 0x80, 0x95, 0x92, 0x9b, 0x9c, 0xb1, 0xb6, 0xbf, 0xb8, 0xad, 0xaa, 0xa3, 0xa4,
    0xf9, 0xfe, 0xf7, 0xf0, 0xe5, 0xe2, 0xeb, 0xec, 0xc1, 0xc6, 0xcf, 0xc8, 0xdd, 0xda, 0xd3, 0xd4,
    0x69, 0x6e, 0x67, 0x60, 0x75, 0x72, 0x7b, 0x7c, 0x51, 0x56, 0x5f, 0x58, 0x4d, 0x4a, 0x43, 0x44,
    0x19, 0x1e, 0x17, 0x10, 0x05, 0x02, 0x0b, 0x0c, 0x21, 0x26, 0x2f, 0x28, 0x3d, 0x3a, 0x33, 0x34,
    0x4e, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5c, 0x5b, 0x76, 0x71, 0x78, 0x7f, 0x6a, 0x6d, 0x64, 0x63,
    0x3e, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2c, 0x2b, 0x06, 0x01, 0x08, 0x0f, 0x1a, 0x1d, 0x14, 0x13,
    0xae, 0xa9, 0xa0, 0xa7, 0xb2, 0xb5, 0xbc, 0xbb, 0x96, 0x91, 0x98, 0x9f, 0x8a, 0x8d, 0x84, 0x83,
    0xde, 0xd9, 0xd0, 0xd7, 0xc2, 0xc5, 0xcc, 0xcb, 0xe6, 0xe1, 0xe8, 0xef, 0xfa, 0xfd, 0xf4, 0xf3,
]);

export function computeCrc8(data: Uint8Array): number {
    let crc = 0;
    for (let i = 0; i < data.length; i++) {
        crc = CRC8_TABLE[crc ^ data[i]];
    }
    return crc;
}

// ── HID device mock interface (for when dom.hid types are missing) ──────
interface HIDDeviceLike {
    opened: boolean;
    productName: string;
    vendorId?: number;
    productId?: number;
    collections?: Array<{ usagePage: number; usage: number }>;
    open(): Promise<void>;
    close(): Promise<void>;
    addEventListener(type: string, listener: EventListener): void;
    removeEventListener(type: string, listener: EventListener): void;
    sendReport(reportId: number, data: Uint8Array): Promise<void>;
}

interface ParsedPacket {
    flags: number;
    remaining: number;
    payloadLen: number;
    payload: Uint8Array;
}

export class HIDTransport {
    private device: HIDDeviceLike | null = null;
    private logCallbacks: Set<LogCallback> = new Set();
    private rawPacketCallbacks: Set<RawPacketCallback> = new Set();
    private connectionCallbacks: Set<ConnectionCallback> = new Set();
    private statusUpdateCallbacks: Set<StatusUpdateCallback> = new Set();
    private reconnectTimer: ReturnType<typeof setInterval> | null = null;
    private wantConnection = false;

    // ── Multi-packet response state ──
    private pendingResponse: CommandResponse | null = null;
    private pendingResolve: ((resp: CommandResponse | null) => void) | null = null;

    // ── Task Queue ──
    private taskQueue: Array<{ task: () => Promise<any>; resolve: (val: any) => void; reject: (err: any) => void }> = [];
    private isProcessingQueue = false;
    private flagWaiters: Map<number, (msg: ParsedPacket) => void> = new Map();

    // ── Blast RX state ──
    private blastRx = {
        active: false,
        totalPackets: 0,
        buffer: null as Uint8Array | null,
        bitmap: null as Uint8Array | null,
        payloadLens: null as Uint8Array | null,
    };

    constructor() {
        this.handleInputReport = this.handleInputReport.bind(this);
        this.handleDisconnect = this.handleDisconnect.bind(this);
        this.handleGlobalConnect = this.handleGlobalConnect.bind(this);

        const nav = navigator as any;
        if ('hid' in nav) {
            nav.hid.addEventListener('disconnect', this.handleDisconnect);
            nav.hid.addEventListener('connect', this.handleGlobalConnect);
        }
    }

    // ══════════════════════════════════════════════════════════
    // ── Device Discovery & Connection ──
    // ══════════════════════════════════════════════════════════

    public async requestDevice(): Promise<boolean> {
        try {
            const nav = navigator as any;
            if (!('hid' in nav)) {
                console.error('WebHID is not supported in this browser.');
                return false;
            }

            const devices = await nav.hid.requestDevice({
                filters: [{ vendorId: VENDOR_ID, productId: PRODUCT_ID }],
            });

            if (devices.length > 0) {
                const target = devices.find((d: any) => this.isCommInterface(d));
                if (!target) {
                    console.error('[HIDTransport] No valid Comm interface found.',
                        devices.map((d: any) => ({ name: d.productName, collections: d.collections })));
                    return false;
                }

                console.log(`[HIDTransport] Found Comm interface: ${target.productName}`);
                this.wantConnection = true;
                const ok = await this.openDevice(target);
                if (!ok) {
                    console.warn('[HIDTransport] Initial open failed, starting reconnect polling');
                    this.startReconnectPolling();
                }
                return ok;
            }
            return false;
        } catch (error) {
            console.error('Error requesting HID device:', error);
            return false;
        }
    }

    private async openDevice(dev: HIDDeviceLike): Promise<boolean> {
        try {
            this.device = dev;
            if (!dev.opened) {
                await dev.open();
            }
            dev.addEventListener('inputreport', this.handleInputReport as EventListener);
            console.log(`Connected to HID device: ${dev.productName}`);
            this.notifyConnectionChange(true);
            this.stopReconnectPolling();
            return true;
        } catch (error) {
            console.error('Error opening HID device:', error);
            this.device = null;
            return false;
        }
    }

    public async disconnect(forceReset: boolean = false): Promise<void> {
        if (forceReset) {
            this.wantConnection = false;
        }
        this.stopReconnectPolling();
        if (this.device) {
            this.device.removeEventListener('inputreport', this.handleInputReport as EventListener);
            try { await this.device.close(); } catch { /* may already be gone */ }
            console.log('Disconnected from HID device.');
            this.device = null;
        }
        this.notifyConnectionChange(false);
    }

    public isConnected(): boolean {
        return this.device !== null && this.device.opened;
    }

    public getDeviceName(): string {
        return this.device?.productName || 'DF-ONE Full Layout';
    }

    // ── Connection Observers ──

    public onConnectionChange(cb: ConnectionCallback): void { this.connectionCallbacks.add(cb); }
    public offConnectionChange(cb: ConnectionCallback): void { this.connectionCallbacks.delete(cb); }
    private notifyConnectionChange(connected: boolean): void {
        this.connectionCallbacks.forEach(cb => cb(connected));
    }

    public onStatusUpdate(cb: StatusUpdateCallback): void { this.statusUpdateCallbacks.add(cb); }
    public offStatusUpdate(cb: StatusUpdateCallback): void { this.statusUpdateCallbacks.delete(cb); }

    public onLogReceived(cb: LogCallback): void { this.logCallbacks.add(cb); }
    public offLogReceived(cb: LogCallback): void { this.logCallbacks.delete(cb); }
    public onRawPacket(cb: RawPacketCallback): void { this.rawPacketCallbacks.add(cb); }
    public offRawPacket(cb: RawPacketCallback): void { this.rawPacketCallbacks.delete(cb); }

    // ── Disconnect / Reconnect ──

    private handleDisconnect(event: any): void {
        const disconnected = event.device;
        if (this.device && disconnected === this.device) {
            console.log('HID device disconnected');
            this.device.removeEventListener('inputreport', this.handleInputReport as EventListener);
            this.device = null;
            this.notifyConnectionChange(false);

            if (this.pendingResolve) {
                this.pendingResolve(null);
                this.pendingResolve = null;
                this.pendingResponse = null;
            }

            while (this.taskQueue.length > 0) {
                this.taskQueue.shift()!.resolve(null);
            }
            this.isProcessingQueue = false;

            if (this.wantConnection) {
                this.startReconnectPolling();
            }
        }
    }

    private handleGlobalConnect(event: any): void {
        if (!this.wantConnection || this.device) return;
        const dev = event.device;
        if (dev.vendorId === VENDOR_ID && dev.productId === PRODUCT_ID) {
            console.log('Device reappeared, reconnecting...');
            setTimeout(() => this.tryReconnect(), 1000);
        }
    }

    private startReconnectPolling(): void {
        if (this.reconnectTimer) return;
        console.log('Starting auto-reconnect polling...');
        this.reconnectTimer = setInterval(() => this.tryReconnect(), 2000);
    }

    private stopReconnectPolling(): void {
        if (this.reconnectTimer) {
            clearInterval(this.reconnectTimer);
            this.reconnectTimer = null;
        }
    }

    private async tryReconnect(): Promise<void> {
        if (this.device || !this.wantConnection) {
            this.stopReconnectPolling();
            return;
        }
        try {
            const nav = navigator as any;
            const devices = await nav.hid.getDevices();
            const target = devices.find(
                (d: any) => d.vendorId === VENDOR_ID && d.productId === PRODUCT_ID && this.isCommInterface(d)
            );
            if (target) {
                console.log('Found previously authorized device, reopening...');
                await this.openDevice(target);
            }
        } catch { /* ignore */ }
    }

    private isCommInterface(device: any): boolean {
        if (!device?.collections?.length) return false;
        const hasComm = device.collections.some((c: any) => c.usagePage === 0xffff);
        if (!hasComm) {
            console.debug(`[HIDTransport] Interface "${device.productName}" collections:`,
                device.collections.map((c: any) => `UP: 0x${c.usagePage.toString(16).toUpperCase()}, U: 0x${c.usage.toString(16).toUpperCase()}`));
        }
        return hasComm;
    }

    // ══════════════════════════════════════════════════════════
    // ── Packet Building ──
    // ══════════════════════════════════════════════════════════

    public buildCommPacket(flags: number, remaining: number, data: Uint8Array): Uint8Array {
        const packet = new Uint8Array(COMM_REPORT_SIZE);
        const payloadLen = Math.min(data.length, MAX_PAYLOAD_LENGTH);

        packet[0] = flags;
        packet[1] = remaining & 0xff;
        packet[2] = (remaining >> 8) & 0xff;
        packet[3] = payloadLen;
        packet.set(data.slice(0, payloadLen), 4);

        const crcValue = computeCrc8(packet.slice(0, 62));
        packet[62] = crcValue;

        return packet;
    }

    // ══════════════════════════════════════════════════════════
    // ── Low-level Send ──
    // ══════════════════════════════════════════════════════════

    private async safeSendReport(reportId: number, data: Uint8Array): Promise<boolean> {
        if (!this.device || !this.device.opened) return false;
        try {
            await this.device.sendReport(reportId, data);
            return true;
        } catch (error: any) {
            console.error(`[HIDTransport] Fatal send error: ${error.name}: ${error.message}`);
            if (error.name === 'NotAllowedError' || error.name === 'InvalidStateError' || error.name === 'NotFoundError') {
                this.handleFatalError(error);
            }
            return false;
        }
    }

    private handleFatalError(_error: any): void {
        console.warn('[HIDTransport] Handling fatal error, forcing cleanup...');
        this.disconnect(false);
    }

    public async sendResponse(flags: number, data?: Uint8Array): Promise<boolean> {
        if (!this.isConnected()) return false;
        const reportData = this.buildCommPacket(flags, 0, data || new Uint8Array(0));
        return await this.safeSendReport(COMM_REPORT_ID, reportData);
    }

    // ══════════════════════════════════════════════════════════
    // ── Blast + Reconcile TX Helpers ──
    // ══════════════════════════════════════════════════════════

    private waitForFlag(flag: number, timeoutMs: number = 2000): Promise<ParsedPacket | null> {
        return new Promise((resolve) => {
            const timer = setTimeout(() => {
                this.flagWaiters.delete(flag);
                resolve(null);
            }, timeoutMs);

            this.flagWaiters.set(flag, (msg) => {
                clearTimeout(timer);
                this.flagWaiters.delete(flag);
                resolve(msg);
            });
        });
    }

    private async sendPacketByIndex(data: Uint8Array, index: number, totalPackets: number): Promise<boolean> {
        const offset = index * MAX_PAYLOAD_LENGTH;
        const chunk = data.slice(offset, offset + MAX_PAYLOAD_LENGTH);
        const remaining = totalPackets - 1 - index;

        let flags = 0;
        if (index === 0) flags = PAYLOAD_FLAG_FIRST;
        else if (index === totalPackets - 1) flags = PAYLOAD_FLAG_LAST;
        else flags = PAYLOAD_FLAG_MID;

        const reportData = this.buildCommPacket(flags, remaining, chunk);
        return await this.safeSendReport(COMM_REPORT_ID, reportData);
    }

    private findMissingFromBitmap(bitmap: Uint8Array, totalPackets: number, skipFirst: boolean, skipLast: boolean): number[] {
        const missing: number[] = [];
        for (let i = 0; i < totalPackets; i++) {
            if (skipFirst && i === 0) continue;
            if (skipLast && i === totalPackets - 1) continue;
            const byteIdx = Math.floor(i / 8);
            const bitIdx = i % 8;
            if (byteIdx >= bitmap.length || !((bitmap[byteIdx] >> bitIdx) & 1)) {
                missing.push(i);
            }
        }
        return missing;
    }

    // ── Blast RX Helpers ──

    private blastRxReset(): void {
        this.blastRx = { active: false, totalPackets: 0, buffer: null, bitmap: null, payloadLens: null };
    }

    private blastRxReceivePacket(index: number, payload: Uint8Array, payloadLen: number): void {
        if (!this.blastRx.active || !this.blastRx.buffer || !this.blastRx.bitmap || !this.blastRx.payloadLens) return;
        const offset = index * MAX_PAYLOAD_LENGTH;
        this.blastRx.buffer.set(payload.slice(0, payloadLen), offset);
        this.blastRx.payloadLens[index] = payloadLen;
        const byteIdx = Math.floor(index / 8);
        const bitIdx = index % 8;
        this.blastRx.bitmap[byteIdx] |= (1 << bitIdx);
    }

    private blastRxBuildBitmapPacket(): Uint8Array {
        return this.buildCommPacket(PAYLOAD_FLAG_BITMAP, 0, this.blastRx.bitmap || new Uint8Array(0));
    }

    private blastRxAssemblePayload(): Uint8Array {
        if (!this.blastRx.buffer || !this.blastRx.payloadLens) return new Uint8Array(0);
        let totalLen = 0;
        for (let i = 0; i < this.blastRx.totalPackets; i++) {
            totalLen += this.blastRx.payloadLens[i];
        }
        const result = new Uint8Array(totalLen);
        let writePos = 0;
        for (let i = 0; i < this.blastRx.totalPackets; i++) {
            const len = this.blastRx.payloadLens[i];
            const offset = i * MAX_PAYLOAD_LENGTH;
            result.set(this.blastRx.buffer.slice(offset, offset + len), writePos);
            writePos += len;
        }
        return result;
    }

    // ══════════════════════════════════════════════════════════
    // ── Blast + Reconcile TX ──
    // ══════════════════════════════════════════════════════════

    public async sendCustomCommReport(data: Uint8Array): Promise<boolean> {
        if (!this.isConnected()) return false;
        try {
            const totalPackets = Math.ceil(data.length / MAX_PAYLOAD_LENGTH) || 1;

            // Single packet path
            if (totalPackets === 1) {
                const flags = PAYLOAD_FLAG_FIRST | PAYLOAD_FLAG_LAST;
                const reportData = this.buildCommPacket(flags, 0, data);
                return await this.safeSendReport(COMM_REPORT_ID, reportData);
            }

            console.log(`[Blast TX] Starting: ${totalPackets} packets for ${data.length} bytes`);

            // Phase 1: Handshake
            const ackPromise = this.waitForFlag(PAYLOAD_FLAG_ACK, 3000);
            if (!await this.sendPacketByIndex(data, 0, totalPackets)) {
                console.error('[Blast TX] Failed to send FIRST packet');
                return false;
            }
            const ackResp = await ackPromise;
            if (!ackResp) {
                console.error('[Blast TX] Handshake ACK timeout');
                return false;
            }
            console.log('[Blast TX] Handshake ACK received');

            // Phase 2: Blast MID packets
            for (let i = 1; i < totalPackets - 1; i++) {
                if (!await this.sendPacketByIndex(data, i, totalPackets)) {
                    console.error(`[Blast TX] Failed to send MID packet ${i}`);
                    return false;
                }
            }
            console.log(`[Blast TX] Blasted ${totalPackets - 2} MID packets`);

            // Phase 3: Reconcile
            const MAX_RECONCILE_ROUNDS = 5;
            for (let round = 0; round < MAX_RECONCILE_ROUNDS; round++) {
                const bitmapPromise = this.waitForFlag(PAYLOAD_FLAG_BITMAP, 3000);
                const statusPacket = this.buildCommPacket(PAYLOAD_FLAG_STATUS_REQ, 0, new Uint8Array(0));
                if (!await this.safeSendReport(COMM_REPORT_ID, statusPacket)) {
                    console.error(`[Blast TX] Failed to send STATUS_REQ (round ${round})`);
                    if (round === MAX_RECONCILE_ROUNDS - 1) return false;
                    continue;
                }

                const bitmapResp = await bitmapPromise;
                if (!bitmapResp) {
                    console.error(`[Blast TX] Bitmap response timeout (round ${round})`);
                    if (round === MAX_RECONCILE_ROUNDS - 1) return false;
                    continue;
                }

                const missing = this.findMissingFromBitmap(bitmapResp.payload, totalPackets, true, true);
                if (missing.length === 0) {
                    console.log(`[Blast TX] All MID packets confirmed (round ${round})`);
                    break;
                }

                console.log(`[Blast TX] Round ${round}: retransmitting ${missing.length} packets: [${missing.join(',')}]`);
                for (const idx of missing) {
                    if (!await this.sendPacketByIndex(data, idx, totalPackets)) {
                        console.error(`[Blast TX] Failed to retransmit packet ${idx}`);
                        return false;
                    }
                }

                if (round === MAX_RECONCILE_ROUNDS - 1) {
                    console.error('[Blast TX] Max reconcile rounds reached');
                    return false;
                }
            }

            // Phase 4: Commit
            console.log('[Blast TX] Sending LAST packet (commit)');
            if (!await this.sendPacketByIndex(data, totalPackets - 1, totalPackets)) {
                console.error('[Blast TX] Failed to send LAST packet');
                return false;
            }

            console.log('[Blast TX] Complete');
            return true;
        } catch (error) {
            console.error('Error in blast send:', error);
            return false;
        }
    }

    // ══════════════════════════════════════════════════════════
    // ── Serial Task Queue ──
    // ══════════════════════════════════════════════════════════

    public enqueueTask<T>(task: () => Promise<T>): Promise<T> {
        return new Promise<T>((resolve, reject) => {
            this.taskQueue.push({ task, resolve, reject });
            this.processNextTask();
        });
    }

    private async processNextTask(): Promise<void> {
        if (this.isProcessingQueue || this.taskQueue.length === 0) return;

        this.isProcessingQueue = true;
        const { task, resolve, reject } = this.taskQueue.shift()!;

        try {
            const result = await task();
            resolve(result);
        } catch (err) {
            console.error('[HID Queue] Task failed:', err);
            reject(err);
        } finally {
            setTimeout(() => {
                this.isProcessingQueue = false;
                this.processNextTask();
            }, 50);
        }
    }

    // ══════════════════════════════════════════════════════════
    // ── sendCommand (request/response) ──
    // ══════════════════════════════════════════════════════════

    public sendCommand(payload: Uint8Array, timeoutMs?: number): Promise<CommandResponse | null> {
        if (!this.isConnected()) return Promise.resolve(null);

        const effectiveTimeout = timeoutMs ?? Math.max(5000, 5000 + payload.length * 5);
        const cmdHex = Array.from(payload.slice(0, Math.min(4, payload.length)))
            .map(b => b.toString(16).padStart(2, '0')).join(' ');

        return this.enqueueTask(async () => {
            console.log(`[HID Queue] Sending command: ${cmdHex} (${payload.length} bytes)`);

            let timer: any;
            const promise = new Promise<CommandResponse | null>((resolve) => {
                timer = setTimeout(() => {
                    if (this.pendingResolve) {
                        console.warn(`[HID Queue] Command timed out after ${effectiveTimeout}ms: ${cmdHex}`);
                        this.pendingResolve = null;
                        this.pendingResponse = null;
                        this.blastRxReset();
                        resolve(null);
                    }
                }, effectiveTimeout);

                this.pendingResolve = (resp) => {
                    clearTimeout(timer);
                    this.pendingResolve = null;
                    resolve(resp);
                };
            });

            try {
                const success = await this.sendCustomCommReport(payload);
                if (!success) {
                    clearTimeout(timer);
                    this.pendingResolve = null;
                    this.pendingResponse = null;
                    this.blastRxReset();
                    return null;
                }
                return await promise;
            } catch (err) {
                clearTimeout(timer);
                this.pendingResolve = null;
                this.pendingResponse = null;
                this.blastRxReset();
                throw err;
            }
        });
    }

    // ══════════════════════════════════════════════════════════
    // ── Incoming Packet Handler ──
    // ══════════════════════════════════════════════════════════

    private async handleInputReport(event: any): Promise<void> {
        if (event.reportId !== COMM_REPORT_ID) return;
        const data = new Uint8Array(event.data.buffer);
        if (data.length < 63) return;

        const flags = data[0];
        const remaining = data[1] | (data[2] << 8);
        const safeLen = Math.min(data[3], MAX_PAYLOAD_LENGTH);
        const payloadBytes = data.slice(4, 4 + safeLen);

        const isBlastPacket = (flags & PAYLOAD_FLAG_MID) || (flags & PAYLOAD_FLAG_LAST);
        const isHandshake = (flags & PAYLOAD_FLAG_FIRST) && remaining > 0;

        if (!this.blastRx.active || isHandshake || !isBlastPacket) {
            this.logCallbacks.forEach(cb => cb(data));
            this.rawPacketCallbacks.forEach(cb => cb(data, 'rx'));
        }

        // Flag waiters (for Blast TX reconcile)
        if (this.flagWaiters.has(flags)) {
            this.flagWaiters.get(flags)!({ flags, remaining, payloadLen: safeLen, payload: payloadBytes });
            return;
        }

        if ((flags & PAYLOAD_FLAG_ACK) && this.flagWaiters.has(PAYLOAD_FLAG_ACK)) {
            this.flagWaiters.get(PAYLOAD_FLAG_ACK)!({ flags, remaining, payloadLen: safeLen, payload: payloadBytes });
            return;
        }

        // STATUS_REQ during blast RX → respond with bitmap
        if (flags === PAYLOAD_FLAG_STATUS_REQ && this.blastRx.active) {
            const bitmapPacket = this.blastRxBuildBitmapPacket();
            this.safeSendReport(COMM_REPORT_ID, bitmapPacket);
            return;
        }

        // Skip pure response/process packets
        const isResponsePacket = (flags & PAYLOAD_FLAG_ACK) || (flags & PAYLOAD_FLAG_NAK) ||
            (flags & PAYLOAD_FLAG_OK) || (flags & PAYLOAD_FLAG_ERR) || (flags & PAYLOAD_FLAG_ABORT);
        if (isResponsePacket) return;

        // Blast RX: FIRST with remaining > 0 → handshake
        if ((flags & PAYLOAD_FLAG_FIRST) && remaining > 0) {
            const totalPackets = remaining + 1;
            if (totalPackets > 5000) {
                console.error(`[Blast RX] ABORTING: Ridiculous packet count (${totalPackets})`);
                this.sendResponse(PAYLOAD_FLAG_ABORT);
                return;
            }
            this.blastRx.active = true;
            this.blastRx.totalPackets = totalPackets;
            this.blastRx.buffer = new Uint8Array(totalPackets * MAX_PAYLOAD_LENGTH);
            this.blastRx.bitmap = new Uint8Array(Math.ceil(totalPackets / 8));
            this.blastRx.payloadLens = new Uint8Array(totalPackets);
            this.blastRxReceivePacket(0, payloadBytes, safeLen);
            this.sendResponse(PAYLOAD_FLAG_ACK);
            return;
        }

        // Blast RX: MID packet
        if (this.blastRx.active && (flags & PAYLOAD_FLAG_MID)) {
            const index = this.blastRx.totalPackets - 1 - remaining;
            if (index < 0 || index >= this.blastRx.totalPackets) {
                console.error(`[Blast RX] Index OOB: ${index}`);
                this.blastRxReset();
                return;
            }
            this.blastRxReceivePacket(index, payloadBytes, safeLen);
            return;
        }

        // Blast RX: LAST → assemble
        if (this.blastRx.active && (flags & PAYLOAD_FLAG_LAST)) {
            const lastIndex = this.blastRx.totalPackets - 1;
            this.blastRxReceivePacket(lastIndex, payloadBytes, safeLen);
            const fullPayload = this.blastRxAssemblePayload();
            if (fullPayload.length >= 7) {
                const module = fullPayload[0];
                const cmd = fullPayload[1];
                const keyId = fullPayload[2];
                const status = (fullPayload[3] | (fullPayload[4] << 8) | (fullPayload[5] << 16) | (fullPayload[6] << 24));
                const jsonText = new TextDecoder().decode(fullPayload.slice(7)).replace(/\0/g, '');
                this.pendingResponse = { module, cmd, keyId, status, jsonText };
                await this.sendAckAndFinish(true);
            }
            this.blastRxReset();
            return;
        }

        // Single packet response (FIRST|LAST)
        if ((flags & PAYLOAD_FLAG_FIRST) && (flags & PAYLOAD_FLAG_LAST) && safeLen >= 4) {
            const module = payloadBytes[0];
            const cmd = payloadBytes[1];
            const keyId = payloadBytes[2];
            const status = (payloadBytes[3] | (payloadBytes[4] << 8) | (payloadBytes[5] << 16) | (payloadBytes[6] << 24));
            const jsonText = new TextDecoder().decode(payloadBytes.slice(7)).replace(/\0/g, '');
            this.pendingResponse = { module, cmd, keyId, status, jsonText };
            await this.sendAckAndFinish(true);
        }
    }

    private async sendAckAndFinish(isLast: boolean): Promise<void> {
        if (isLast) await this.sendResponse(PAYLOAD_FLAG_ACK | PAYLOAD_FLAG_OK);
        else await this.sendResponse(PAYLOAD_FLAG_ACK);
        this.finishResponse();
    }

    private finishResponse(): void {
        if (this.pendingResponse) {
            const { module, jsonText } = this.pendingResponse;

            // Handle pushed status updates
            if (module === MODULE_STATUS && jsonText) {
                try {
                    const data = JSON.parse(jsonText);
                    const normalizedStatus: DeviceStatus = {
                        mode: data.mode,
                        profile: data.profile,
                        pairing: data.pairing ?? -1,
                        bitmap: data.bitmap,
                    };
                    this.statusUpdateCallbacks.forEach(cb => cb(normalizedStatus));
                } catch (e) {
                    console.error('Failed to parse push status JSON:', e);
                }
            }

            if (this.pendingResolve) {
                this.pendingResolve(this.pendingResponse);
                this.pendingResolve = null;
            }
            this.pendingResponse = null;
        }
    }
}
