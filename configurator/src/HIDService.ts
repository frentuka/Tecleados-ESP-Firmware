export const VENDOR_ID = 0x303A;
export const PRODUCT_ID = 0x1324;
export const COMM_REPORT_ID = 3;
export const COMM_REPORT_SIZE = 63;

// Transport flags
export const PAYLOAD_FLAG_FIRST = 0x80;
export const PAYLOAD_FLAG_MID = 0x40;
export const PAYLOAD_FLAG_LAST = 0x20;
export const PAYLOAD_FLAG_ACK = 0x10;
export const PAYLOAD_FLAG_NAK = 0x08;

// Blast reconcile (combined flag values unused in normal flow)
export const PAYLOAD_FLAG_STATUS_REQ = 0x50; // MID|ACK
export const PAYLOAD_FLAG_BITMAP = 0x48;     // MID|NAK

// Process flags
export const PAYLOAD_FLAG_OK = 0x04;
export const PAYLOAD_FLAG_ERR = 0x02;
export const PAYLOAD_FLAG_ABORT = 0x01;

// Module IDs
export const MODULE_CONFIG = 0x00;
export const MODULE_SYSTEM = 0x01;

// Config Commands
export const CFG_CMD_GET = 0x00;
export const CFG_CMD_SET = 0x01;

// Config Key IDs (must match cfgmod_key_id_t in cfgmod.h)
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

// System Commands
export const SYS_CMD_INJECT_KEY = 0x01;
export const SYS_CMD_CLEAR_INJECTED = 0x02;

export const MACRO_CODE_BASE = 0x4000;

export type LogCallback = (logData: Uint8Array) => void;
export type RawPacketCallback = (data: Uint8Array, direction: 'rx' | 'tx') => void;

// Response from a completed sendCommand call
export interface CommandResponse {
    module: number;
    cmd: number;
    keyId: number;
    status: number;     // esp_err_t (0 = ESP_OK)
    jsonText: string;    // fully reassembled JSON payload
}

// Define simple interface mocks in case the dom.hid lib is missing
interface HIDDeviceMock {
    opened: boolean;
    productName: string;
    open(): Promise<void>;
    close(): Promise<void>;
    addEventListener(type: string, listener: EventListener): void;
    removeEventListener(type: string, listener: EventListener): void;
    sendReport(reportId: number, data: Uint8Array): Promise<void>;
}

// Precomputed CRC-8 table for polynomial 0x07 (reflected) matches ESP32 usb_crc.c
const CRC8_TABLE = new Uint8Array([
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15, 0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65, 0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5, 0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85, 0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2, 0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2, 0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32, 0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42, 0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C, 0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC, 0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C, 0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C, 0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B, 0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B, 0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB, 0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB, 0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
]);

export function computeCrc8(data: Uint8Array): number {
    let crc = 0;
    for (let i = 0; i < data.length; i++) {
        crc = CRC8_TABLE[crc ^ data[i]];
    }
    return crc;
}

export type ConnectionCallback = (connected: boolean) => void;

class HIDService {
    private device: HIDDeviceMock | null = null;
    private logCallbacks: Set<LogCallback> = new Set();
    private rawPacketCallbacks: Set<RawPacketCallback> = new Set();
    private connectionCallbacks: Set<ConnectionCallback> = new Set();
    private reconnectTimer: ReturnType<typeof setInterval> | null = null;
    private wantConnection = false; // true after user clicks Connect

    // ── Multi-packet reassembly state ──
    private pendingResponse: {
        module: number;
        cmd: number;
        keyId: number;
        status: number;
        jsonText: string;
    } | null = null;
    private pendingResolve: ((resp: CommandResponse | null) => void) | null = null;

    // ── Generic flag-based response waiting ──
    private flagWaiters: Map<number, (msg: { flags: number; remaining: number; payloadLen: number; payload: Uint8Array }) => void> = new Map();

    // ── Blast receive state (for ESP→Website direction) ──
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

        // Listen for global HID connect/disconnect events
        const nav = navigator as any;
        if ('hid' in nav) {
            nav.hid.addEventListener('disconnect', this.handleDisconnect);
            nav.hid.addEventListener('connect', this.handleGlobalConnect);
        }
    }

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
                this.wantConnection = true;
                return await this.openDevice(devices[0]);
            }
            return false;
        } catch (error) {
            console.error('Error requesting HID device:', error);
            return false;
        }
    }

    private async openDevice(dev: HIDDeviceMock): Promise<boolean> {
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

    public async disconnect(): Promise<void> {
        this.wantConnection = false;
        this.stopReconnectPolling();
        if (this.device) {
            this.device.removeEventListener('inputreport', this.handleInputReport as EventListener);
            try { await this.device.close(); } catch { /* device may already be gone */ }
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

    // ── Connection state observers ──
    public onConnectionChange(callback: ConnectionCallback): void {
        this.connectionCallbacks.add(callback);
    }

    public offConnectionChange(callback: ConnectionCallback): void {
        this.connectionCallbacks.delete(callback);
    }

    private notifyConnectionChange(connected: boolean): void {
        this.connectionCallbacks.forEach(cb => cb(connected));
    }

    // ── Disconnect handler ──
    private handleDisconnect(event: any): void {
        const disconnectedDevice = event.device;
        if (this.device && disconnectedDevice === this.device) {
            console.log('HID device disconnected (flash/reset?)');
            this.device.removeEventListener('inputreport', this.handleInputReport as EventListener);
            this.device = null;
            this.notifyConnectionChange(false);

            // Reject any pending command
            if (this.pendingResolve) {
                this.pendingResolve(null);
                this.pendingResolve = null;
                this.pendingResponse = null;
            }

            // Flush command queue
            while (this.commandQueue.length > 0) {
                const cmd = this.commandQueue.shift()!;
                cmd.resolve(null);
            }
            this.isProcessingQueue = false;

            // Start polling for reconnection if user hasn't explicitly disconnected
            if (this.wantConnection) {
                this.startReconnectPolling();
            }
        }
    }

    // ── Auto-reconnect via global connect event + polling ──
    private handleGlobalConnect(event: any): void {
        if (!this.wantConnection || this.device) return;
        const dev = event.device;
        if (dev.vendorId === VENDOR_ID && dev.productId === PRODUCT_ID) {
            console.log('Device reappeared, reconnecting...');
            // Small delay to let the device fully initialize
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
            // getDevices() returns previously authorized devices without user prompt
            const devices = await nav.hid.getDevices();
            const target = devices.find(
                (d: any) => d.vendorId === VENDOR_ID && d.productId === PRODUCT_ID
            );
            if (target) {
                console.log('Found previously authorized device, reopening...');
                await this.openDevice(target);
            }
        } catch (error) {
            // to-do
        }
    }

    // ══════════════════════════════════════════════════════════
    // ── Packet building ──
    // ══════════════════════════════════════════════════════════

    public buildCommPacket(flags: number, remaining: number, data: Uint8Array): Uint8Array {
        const packet = new Uint8Array(COMM_REPORT_SIZE);

        // Ensure data doesn't exceed 58 bytes max payload length
        const payloadLen = Math.min(data.length, 58);

        // Map to usb_packet_msg_t byte layout
        packet[0] = flags;                   // flags
        packet[1] = remaining & 0xFF;        // remaining_packets (LSB)
        packet[2] = (remaining >> 8) & 0xFF; // remaining_packets (MSB)
        packet[3] = payloadLen;              // payload_len

        // Copy payload into bytes 4-61
        packet.set(data.slice(0, payloadLen), 4);

        // Compute CRC-8 over the first 62 bytes and set as the 63rd byte
        const crcValue = computeCrc8(packet.slice(0, 62));
        packet[62] = crcValue;

        return packet;
    }

    // ══════════════════════════════════════════════════════════
    // ── Low-level send helpers ──
    // ══════════════════════════════════════════════════════════

    public async sendResponse(flags: number, data?: Uint8Array): Promise<boolean> {
        if (!this.isConnected()) return false;
        try {
            const reportData = this.buildCommPacket(flags, 0, data || new Uint8Array(0));
            await this.device!.sendReport(COMM_REPORT_ID, reportData);
            return true;
        } catch (error) {
            console.error('Error sending response COMM report:', error);
            return false;
        }
    }

    // ══════════════════════════════════════════════════════════
    // ── Blast + Reconcile helpers ──
    // ══════════════════════════════════════════════════════════

    /** Wait for a packet with a specific flag value. Returns the parsed packet. */
    private waitForFlag(flag: number, timeoutMs: number = 2000): Promise<{ flags: number; remaining: number; payloadLen: number; payload: Uint8Array } | null> {
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

    /** Build and send a packet by index from a data buffer */
    private async sendPacketByIndex(data: Uint8Array, index: number, totalPackets: number): Promise<boolean> {
        const maxPayloadSize = 58;
        const offset = index * maxPayloadSize;
        const chunk = data.slice(offset, offset + maxPayloadSize);
        const remaining = totalPackets - 1 - index;

        let flags = 0;
        if (index === 0) flags = PAYLOAD_FLAG_FIRST;
        else if (index === totalPackets - 1) flags = PAYLOAD_FLAG_LAST;
        else flags = PAYLOAD_FLAG_MID;

        const reportData = this.buildCommPacket(flags, remaining, chunk);
        try {
            await this.device!.sendReport(COMM_REPORT_ID, reportData);
            return true;
        } catch {
            return false;
        }
    }

    /** Parse a bitmap response to find missing packet indices */
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

    // ══════════════════════════════════════════════════════════
    // ── Blast receive (ESP → Website) helpers ──
    // ══════════════════════════════════════════════════════════

    private blastRxReset(): void {
        this.blastRx = {
            active: false,
            totalPackets: 0,
            buffer: null,
            bitmap: null,
            payloadLens: null,
        };
    }

    private blastRxReceivePacket(index: number, payload: Uint8Array, payloadLen: number): void {
        if (!this.blastRx.active || !this.blastRx.buffer || !this.blastRx.bitmap || !this.blastRx.payloadLens) return;
        const offset = index * 58;
        this.blastRx.buffer.set(payload.slice(0, payloadLen), offset);
        this.blastRx.payloadLens[index] = payloadLen;
        // Set bitmap bit
        const byteIdx = Math.floor(index / 8);
        const bitIdx = index % 8;
        this.blastRx.bitmap[byteIdx] |= (1 << bitIdx);
    }

    private blastRxBuildBitmapPacket(): Uint8Array {
        const bitmapBytes = this.blastRx.bitmap || new Uint8Array(0);
        return this.buildCommPacket(PAYLOAD_FLAG_BITMAP, 0, bitmapBytes);
    }

    private blastRxAssemblePayload(): Uint8Array {
        if (!this.blastRx.buffer || !this.blastRx.payloadLens) return new Uint8Array(0);
        let totalLen = 0;
        for (let i = 0; i < this.blastRx.totalPackets; i++) {
            totalLen += this.blastRx.payloadLens[i];
        }
        // Data is already at correct offsets in buffer, but need to compact
        // since last packet may be shorter than 58
        const result = new Uint8Array(totalLen);
        let writePos = 0;
        for (let i = 0; i < this.blastRx.totalPackets; i++) {
            const len = this.blastRx.payloadLens[i];
            const offset = i * 58;
            result.set(this.blastRx.buffer.slice(offset, offset + len), writePos);
            writePos += len;
        }
        return result;
    }

    // ══════════════════════════════════════════════════════════
    // ── Send payload (Website → ESP) with Blast + Reconcile ──
    // ══════════════════════════════════════════════════════════

    /** Send a raw multi-packet payload using blast + reconcile protocol */
    public async sendCustomCommReport(data: Uint8Array): Promise<boolean> {
        if (!this.isConnected()) return false;
        try {
            const maxPayloadSize = 58;
            const totalPackets = Math.ceil(data.length / maxPayloadSize) || 1;

            // Single packet: legacy path (FIRST|LAST)
            if (totalPackets === 1) {
                const flags = PAYLOAD_FLAG_FIRST | PAYLOAD_FLAG_LAST;
                const reportData = this.buildCommPacket(flags, 0, data);
                await this.device!.sendReport(COMM_REPORT_ID, reportData);
                return true;
            }

            console.log(`[Blast TX] Starting: ${totalPackets} packets for ${data.length} bytes`);

            // Phase 1: Handshake — send FIRST, wait for ACK
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

            // Phase 2: Blast — send all MID packets (indices 1..N-2)
            for (let i = 1; i < totalPackets - 1; i++) {
                if (!await this.sendPacketByIndex(data, i, totalPackets)) {
                    console.error(`[Blast TX] Failed to send MID packet ${i}`);
                    return false;
                }
            }
            console.log(`[Blast TX] Blasted ${totalPackets - 2} MID packets`);

            // Phase 3: Reconcile — send STATUS_REQ, receive BITMAP, retransmit gaps
            const MAX_RECONCILE_ROUNDS = 5;
            for (let round = 0; round < MAX_RECONCILE_ROUNDS; round++) {
                // Send STATUS_REQ
                const bitmapPromise = this.waitForFlag(PAYLOAD_FLAG_BITMAP, 3000);
                const statusPacket = this.buildCommPacket(PAYLOAD_FLAG_STATUS_REQ, 0, new Uint8Array(0));
                await this.device!.sendReport(COMM_REPORT_ID, statusPacket);

                const bitmapResp = await bitmapPromise;
                if (!bitmapResp) {
                    console.error(`[Blast TX] Bitmap response timeout (round ${round})`);
                    if (round === MAX_RECONCILE_ROUNDS - 1) return false;
                    continue;
                }

                // Check missing packets (skip index 0 = FIRST, skip last = LAST)
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

            // Phase 4: Commit — send LAST packet
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
    // ── High-level API: sendCommand (serial queue) ──
    // ══════════════════════════════════════════════════════════

    // Command queue to prevent concurrent sendCommand calls from interfering
    private commandQueue: Array<{ payload: Uint8Array; timeoutMs: number; resolve: (resp: CommandResponse | null) => void }> = [];
    private isProcessingQueue = false;

    /**
     * Send a command and wait for the complete response.
     * Commands are queued and processed serially to avoid response mismatches.
     * @param payload Full payload bytes: [MODULE_ID, CMD, KEY_ID, PAYLOAD_LEN, ...data]
     * @param timeoutMs Timeout in milliseconds (default 5000)
     * @returns CommandResponse or null on timeout/error
     */
    public sendCommand(payload: Uint8Array, timeoutMs?: number): Promise<CommandResponse | null> {
        if (!this.isConnected()) return Promise.resolve(null);

        // Scale timeout based on payload size: base 5s + 5ms per byte for large payloads
        const effectiveTimeout = timeoutMs ?? Math.max(5000, 5000 + payload.length * 5);

        return new Promise<CommandResponse | null>((resolve) => {
            this.commandQueue.push({ payload, timeoutMs: effectiveTimeout, resolve });
            this.processNextCommand();
        });
    }

    private async processNextCommand(): Promise<void> {
        if (this.isProcessingQueue) return; // already processing
        if (this.commandQueue.length === 0) return;

        this.isProcessingQueue = true;
        const { payload, timeoutMs, resolve } = this.commandQueue.shift()!;

        // Log the command being processed
        const cmdHex = Array.from(payload.slice(0, Math.min(4, payload.length))).map(b => b.toString(16).padStart(2, '0')).join(' ');
        console.log(`[HID Queue] Processing command: ${cmdHex} (${payload.length} bytes, queue remaining: ${this.commandQueue.length})`);

        if (!this.isConnected()) {
            console.warn('[HID Queue] Not connected, skipping command');
            resolve(null);
            this.isProcessingQueue = false;
            this.processNextCommand();
            return;
        }

        // Set up response handler
        const result = await new Promise<CommandResponse | null>(async (innerResolve) => {
            this.pendingResolve = innerResolve;

            const timeout = setTimeout(() => {
                if (this.pendingResolve === innerResolve) {
                    console.warn(`[HID Queue] Command timed out after ${timeoutMs}ms: ${cmdHex}`);
                    this.pendingResolve = null;
                    this.pendingResponse = null;
                    innerResolve(null);
                }
            }, timeoutMs);

            // Wrap to clear timeout on success
            this.pendingResolve = (resp) => {
                clearTimeout(timeout);
                innerResolve(resp);
            };

            await this.sendCustomCommReport(payload);
        });

        console.log(`[HID Queue] Command ${cmdHex} completed:`, result ? `status=${result.status}, keyId=${result.keyId}, jsonLen=${result.jsonText.length}` : 'NULL');
        resolve(result);

        this.isProcessingQueue = false;
        this.processNextCommand(); // process next in queue
    }

    // ══════════════════════════════════════════════════════════
    // ── System API (Key Test Mode) ──
    // ══════════════════════════════════════════════════════════

    public async sendInjectKey(row: number, col: number, state: boolean): Promise<boolean> {
        if (!this.isConnected()) return false;

        // Payload: [CMD, row, col, state]
        const payload = new Uint8Array([MODULE_SYSTEM, SYS_CMD_INJECT_KEY, row, col, state ? 1 : 0]);
        return this.sendCustomCommReport(payload);
    }

    public async clearInjectedKeys(): Promise<boolean> {
        if (!this.isConnected()) return false;

        // Payload: [CMD]
        const payload = new Uint8Array([MODULE_SYSTEM, SYS_CMD_CLEAR_INJECTED]);
        return this.sendCustomCommReport(payload);
    }

    // ══════════════════════════════════════════════════════════
    // ── Incoming packet handler (auto-ACK + reassembly) ──
    // ══════════════════════════════════════════════════════════

    private handleInputReport(event: any): void {
        if (event.reportId !== COMM_REPORT_ID) return;
        const data = new Uint8Array(event.data.buffer);

        if (data.length < 63) return;

        const flags = data[0];
        const remaining = data[1] | (data[2] << 8);
        const safeLen = Math.min(data[3], 58);
        const payloadBytes = data.slice(4, 4 + safeLen);

        // --- Blast check ---
        const isBlastPacket = (flags & PAYLOAD_FLAG_MID) || (flags & PAYLOAD_FLAG_LAST);
        const isHandshake = (flags & PAYLOAD_FLAG_FIRST) && remaining > 0;

        // Broadcast raw data and logs ONLY for non-blast payload packets or handshakes
        // This avoids 500+ callbacks during a 258-packet blast
        if (!this.blastRx.active || isHandshake || !isBlastPacket) {
            this.logCallbacks.forEach(cb => cb(data));
            this.rawPacketCallbacks.forEach(cb => cb(data, 'rx'));
        }

        // ── Check flag waiters (blast protocol responses) ──
        // Check exact flag match first (for BITMAP, STATUS_REQ)
        if (this.flagWaiters.has(flags)) {
            this.flagWaiters.get(flags)!({ flags, remaining, payloadLen: safeLen, payload: payloadBytes });
            return;
        }
        // Check partial flag match for ACK (ACK may be combined with OK)
        if ((flags & PAYLOAD_FLAG_ACK) && this.flagWaiters.has(PAYLOAD_FLAG_ACK)) {
            this.flagWaiters.get(PAYLOAD_FLAG_ACK)!({ flags, remaining, payloadLen: safeLen, payload: payloadBytes });
            return;
        }

        // ── Blast reconcile: STATUS_REQ from ESP (ESP asking for our bitmap) ──
        if (flags === PAYLOAD_FLAG_STATUS_REQ && this.blastRx.active) {
            console.log('[Blast RX] STATUS_REQ received, sending bitmap');
            const bitmapPacket = this.blastRxBuildBitmapPacket();
            this.device?.sendReport(COMM_REPORT_ID, bitmapPacket);
            return;
        }

        // ── Skip pure ACK/NAK/OK/ERR/ABORT when not caught by flag waiters ──
        const isResponsePacket = (flags & PAYLOAD_FLAG_ACK) || (flags & PAYLOAD_FLAG_NAK) ||
            (flags & PAYLOAD_FLAG_OK) || (flags & PAYLOAD_FLAG_ERR) || (flags & PAYLOAD_FLAG_ABORT);
        if (isResponsePacket) return;

        // ── Blast receive mode: ESP → Website multi-packet ──
        if ((flags & PAYLOAD_FLAG_FIRST) && remaining > 0 && safeLen >= 7) {
            // Enter blast receive mode 
            const totalPackets = remaining + 1;
            console.log(`[Blast RX] Entering blast mode: ${totalPackets} packets`);
            this.blastRx.active = true;
            this.blastRx.totalPackets = totalPackets;
            this.blastRx.buffer = new Uint8Array(totalPackets * 58);
            this.blastRx.bitmap = new Uint8Array(Math.ceil(totalPackets / 8));
            this.blastRx.payloadLens = new Uint8Array(totalPackets);

            // Store FIRST packet
            this.blastRxReceivePacket(0, payloadBytes, safeLen);

            // Send ACK (handshake)
            this.sendResponse(PAYLOAD_FLAG_ACK);
            return;
        }

        // Blast RX: MID packets (silent absorb)
        if (this.blastRx.active && (flags & PAYLOAD_FLAG_MID)) {
            const index = this.blastRx.totalPackets - 1 - remaining;
            this.blastRxReceivePacket(index, payloadBytes, safeLen);
            return;
        }

        // Blast RX: LAST packet (commit)
        if (this.blastRx.active && (flags & PAYLOAD_FLAG_LAST)) {
            const lastIndex = this.blastRx.totalPackets - 1;
            this.blastRxReceivePacket(lastIndex, payloadBytes, safeLen);

            // Assemble full payload and process
            const fullPayload = this.blastRxAssemblePayload();
            console.log(`[Blast RX] Committed: ${this.blastRx.totalPackets} packets, ${fullPayload.length} bytes`);

            // Process assembled response (same as the original multi-packet handling)
            if (fullPayload.length >= 7) {
                const module = fullPayload[0];
                const cmd = fullPayload[1];
                const keyId = fullPayload[2];
                const status = (fullPayload[3] | (fullPayload[4] << 8) | (fullPayload[5] << 16) | (fullPayload[6] << 24));
                const jsonText = new TextDecoder().decode(fullPayload.slice(7)).replace(/\0/g, '');
                this.pendingResponse = { module, cmd, keyId, status, jsonText };
                this.sendAckAndFinish(true);
            }

            this.blastRxReset();
            return;
        }

        // ── Legacy single-packet response (FIRST|LAST with remaining=0) ──
        if ((flags & PAYLOAD_FLAG_FIRST) && (flags & PAYLOAD_FLAG_LAST) && safeLen >= 7) {
            const module = payloadBytes[0];
            const cmd = payloadBytes[1];
            const keyId = payloadBytes[2];
            const status = (payloadBytes[3] | (payloadBytes[4] << 8) | (payloadBytes[5] << 16) | (payloadBytes[6] << 24));
            const jsonText = new TextDecoder().decode(payloadBytes.slice(7)).replace(/\0/g, '');
            this.pendingResponse = { module, cmd, keyId, status, jsonText };
            this.sendAckAndFinish(true);
        }
    }

    /**
     * Send ACK (with optional OK) and resolve the pending command immediately.
     * Fire-and-forget: don't await the sendReport — let the OS queue it.
     */
    private sendAckAndFinish(isLast: boolean): void {
        if (isLast) {
            this.sendResponse(PAYLOAD_FLAG_ACK | PAYLOAD_FLAG_OK);
        } else {
            this.sendResponse(PAYLOAD_FLAG_ACK);
        }

        this.finishResponse();
    }

    private finishResponse(): void {
        if (!this.pendingResponse) return;

        const response: CommandResponse = { ...this.pendingResponse };

        if (this.pendingResolve) {
            this.pendingResolve(response);
            this.pendingResolve = null;
        }

        this.pendingResponse = null;
    }

    // ══════════════════════════════════════════════════════════
    // ── Callback registration ──
    // ══════════════════════════════════════════════════════════

    /** Register for raw packet data (legacy — prefer sendCommand) */
    public onLogReceived(callback: LogCallback): void {
        this.logCallbacks.add(callback);
    }

    public offLogReceived(callback: LogCallback): void {
        this.logCallbacks.delete(callback);
    }

    /** Register for raw packet events with direction info (for debug UI) */
    public onRawPacket(callback: RawPacketCallback): void {
        this.rawPacketCallbacks.add(callback);
    }

    public offRawPacket(callback: RawPacketCallback): void {
        this.rawPacketCallbacks.delete(callback);
    }
}

export const hidService = new HIDService();

