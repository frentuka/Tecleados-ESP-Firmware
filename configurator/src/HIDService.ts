export const VENDOR_ID = 0x303A;
export const PRODUCT_ID = 0x1324;
export const COMM_REPORT_ID = 3;
export const COMM_REPORT_SIZE = 48;

// Transport flags
export const PAYLOAD_FLAG_FIRST = 0x80;
export const PAYLOAD_FLAG_MID = 0x40;
export const PAYLOAD_FLAG_LAST = 0x20;
export const PAYLOAD_FLAG_ACK = 0x10;
export const PAYLOAD_FLAG_NAK = 0x08;

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
            setTimeout(() => this.tryReconnect(), 500);
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
            // Silently retry
        }
    }

    // ══════════════════════════════════════════════════════════
    // ── Packet building ──
    // ══════════════════════════════════════════════════════════

    public buildCommPacket(flags: number, remaining: number, data: Uint8Array): Uint8Array {
        const packet = new Uint8Array(COMM_REPORT_SIZE);

        // Ensure data doesn't exceed 43 bytes max payload length
        const payloadLen = Math.min(data.length, 43);

        // Map to usb_packet_msg_t byte layout
        packet[0] = flags;                   // flags
        packet[1] = remaining & 0xFF;        // remaining_packets (LSB)
        packet[2] = (remaining >> 8) & 0xFF; // remaining_packets (MSB)
        packet[3] = payloadLen;              // payload_len

        // Copy payload into bytes 4-46
        packet.set(data.slice(0, payloadLen), 4);

        // Compute CRC-8 over the first 47 bytes and set as the 48th byte
        const crcValue = computeCrc8(packet.slice(0, 47));
        packet[47] = crcValue;

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

    /** Send a raw multi-packet payload (splits into 43-byte chunks with FIRST/MID/LAST flags) */
    public async sendCustomCommReport(data: Uint8Array): Promise<boolean> {
        if (!this.isConnected()) return false;
        try {
            const maxPayloadSize = 43;
            const totalPackets = Math.ceil(data.length / maxPayloadSize) || 1;

            for (let i = 0; i < totalPackets; i++) {
                let flags = 0;
                if (i === 0) flags |= PAYLOAD_FLAG_FIRST;
                if (i === totalPackets - 1) flags |= PAYLOAD_FLAG_LAST;
                if (i > 0 && i < totalPackets - 1) flags |= PAYLOAD_FLAG_MID;

                const remaining = totalPackets - 1 - i;
                const chunk = data.slice(i * maxPayloadSize, (i + 1) * maxPayloadSize);

                const reportData = this.buildCommPacket(flags, remaining, chunk);
                await this.device!.sendReport(COMM_REPORT_ID, reportData);

                if (remaining > 0) {
                    await new Promise(resolve => setTimeout(resolve, 20));
                }
            }
            return true;
        } catch (error) {
            console.error('Error sending custom COMM report:', error);
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
    public sendCommand(payload: Uint8Array, timeoutMs = 5000): Promise<CommandResponse | null> {
        if (!this.isConnected()) return Promise.resolve(null);

        return new Promise<CommandResponse | null>((resolve) => {
            this.commandQueue.push({ payload, timeoutMs, resolve });
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

        // Small delay between commands to let firmware settle
        await new Promise(r => setTimeout(r, 50));

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

        // Broadcast raw data to legacy log callbacks
        this.logCallbacks.forEach(cb => cb(data));
        this.rawPacketCallbacks.forEach(cb => cb(data, 'rx'));

        if (data.length < 48) return;

        const flags = data[0];
        const safeLen = Math.min(data[3], 43);
        const payloadBytes = data.slice(4, 4 + safeLen);

        // Skip ACK/NAK/OK/ERR/ABORT response packets (those are protocol handshakes)
        const isResponsePacket = (flags & PAYLOAD_FLAG_ACK) || (flags & PAYLOAD_FLAG_NAK) ||
            (flags & PAYLOAD_FLAG_OK) || (flags & PAYLOAD_FLAG_ERR) || (flags & PAYLOAD_FLAG_ABORT);
        if (isResponsePacket) return;

        // ── Multi-packet reassembly ──
        // Response wire format: [module(1), cmd(1), keyId(1), status(4), json...]
        // No payload_len field — transport provides total length
        if ((flags & PAYLOAD_FLAG_FIRST) && safeLen >= 7) {
            // First packet: parse header
            const module = payloadBytes[0];
            const cmd = payloadBytes[1];
            const keyId = payloadBytes[2];
            // payloadBytes[3..6] = esp_err_t status (4 bytes, little-endian)
            const status = (payloadBytes[3] | (payloadBytes[4] << 8) | (payloadBytes[5] << 16) | (payloadBytes[6] << 24));
            const jsonText = new TextDecoder().decode(payloadBytes.slice(7)).replace(/\0/g, '');

            this.pendingResponse = { module, cmd, keyId, status, jsonText };

            if (flags & PAYLOAD_FLAG_LAST) {
                // Single-packet response: send ACK|OK then resolve
                this.sendAckAndFinish(true);
            } else {
                // More packets coming: send ACK
                this.sendResponse(PAYLOAD_FLAG_ACK);
            }
        } else if (this.pendingResponse && !(flags & PAYLOAD_FLAG_FIRST)) {
            // Continuation packet: append text
            const jsonText = new TextDecoder().decode(payloadBytes).replace(/\0/g, '');
            this.pendingResponse.jsonText += jsonText;

            if (flags & PAYLOAD_FLAG_LAST) {
                // Last packet: send ACK|OK then resolve
                this.sendAckAndFinish(true);
            } else {
                // More packets coming: send ACK
                this.sendResponse(PAYLOAD_FLAG_ACK);
            }
        }
    }

    /**
     * Send ACK (with optional OK) and THEN resolve the pending command.
     * This ensures the firmware receives the ACK|OK before we send the next command.
     */
    private async sendAckAndFinish(isLast: boolean): Promise<void> {
        if (isLast) {
            await this.sendResponse(PAYLOAD_FLAG_ACK | PAYLOAD_FLAG_OK);
        } else {
            await this.sendResponse(PAYLOAD_FLAG_ACK);
        }

        // Small delay so firmware can process the ACK|OK and clear tx_awaiting_response
        await new Promise(r => setTimeout(r, 30));

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

