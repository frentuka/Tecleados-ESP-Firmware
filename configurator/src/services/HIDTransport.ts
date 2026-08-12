import {
    VENDOR_ID,
    PRODUCT_ID,
    COMM_REPORT_ID,
    COMM_REPORT_SIZE,
} from '../types/protocol';

import type {
    LogCallback,
    RawPacketCallback,
    ConnectionCallback,
    StatusUpdateCallback,
    CommandResponse,
    ConnectionNotification,
} from '../types/device';

import type { ITransport } from './ITransport';
import { CommProtocol } from './CommProtocol';

// ── Transport timing & limits ───────────────────────────────────────────
const RECONNECT_INTERVAL_MS = 2000; // How often to poll for device reconnection

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

export class HIDTransport implements ITransport {
    private device: HIDDeviceLike | null = null;
    private logCallbacks: Set<LogCallback> = new Set();
    private rawPacketCallbacks: Set<RawPacketCallback> = new Set();
    private connectionCallbacks: Set<ConnectionCallback> = new Set();
    private statusUpdateCallbacks: Set<StatusUpdateCallback> = new Set();
    private reconnectTimer: ReturnType<typeof setInterval> | null = null;
    private wantConnection = false;
    
    private protocol: CommProtocol | null = null;
    private selectedDevice: HIDDeviceLike | null = null;

    constructor() {
        this.handleDisconnect = this.handleDisconnect.bind(this);
        this.handleGlobalConnect = this.handleGlobalConnect.bind(this);

        const nav = navigator as any;
        if ('hid' in nav) {
            nav.hid.addEventListener('disconnect', this.handleDisconnect);
            nav.hid.addEventListener('connect', this.handleGlobalConnect);
        }
    }

    public static isLinux(): boolean {
        return /Linux/i.test(navigator.userAgent) || /Linux/i.test((navigator as any).platform || '');
    }

    // ══════════════════════════════════════════════════════════
    // ── Device Discovery & Connection ──
    // ══════════════════════════════════════════════════════════

    public async requestDevice(): Promise<{ ok: boolean; notification?: ConnectionNotification }> {
        try {
            const nav = navigator as any;
            if (!('hid' in nav)) {
                return {
                    ok: false,
                    notification: { type: 'error', message: 'WebHID is not supported in this browser.' }
                };
            }

            const devices = await nav.hid.requestDevice({
                filters: [{ vendorId: VENDOR_ID, productId: PRODUCT_ID }],
            });

            if (devices.length > 0) {
                const candidates = devices.filter((d: any) => this.isCommInterface(d));
                
                if (candidates.length === 0) {
                    return {
                        ok: false,
                        notification: { type: 'warning', message: 'The selected device does not have the required communication interface.' }
                    };
                }

                this.wantConnection = true;
                let lastError: any = null;

                for (const target of candidates) {
                    console.log(`[HIDTransport] Attempting to open interface: ${target.productName}`);
                    try {
                        this.selectedDevice = target;
                        await this.connect();
                        return { ok: true };
                    } catch (e: any) {
                        console.warn(`[HIDTransport] Failed to open interface "${target.productName}":`, e);
                        lastError = e;
                    }
                }

                // If we reach here, all candidates failed
                const msg = lastError?.message || 'Unknown error during open';
                
                if (msg.toLowerCase().includes('failed to open') && HIDTransport.isLinux()) {
                    return {
                        ok: false,
                        notification: { 
                            type: 'error', 
                            message: 'System lock: The device is busy. Please try unplugging and plugging it back in.' 
                        }
                    };
                }

                if (lastError?.name === 'SecurityError' || msg.toLowerCase().includes('access denied')) {
                    return {
                        ok: false,
                        notification: { type: 'error', message: 'PERMISSION_DENIED' }
                    };
                }

                return {
                    ok: false,
                    notification: { type: 'error', message: msg }
                };
            }
            return {
                ok: false,
                notification: { type: 'info', message: 'Connection cancelled: No device selected.' }
            };
        } catch (error: any) {
            if (error.name === 'NotFoundError') {
                return {
                    ok: false,
                    notification: { type: 'info', message: 'Connection cancelled: No device selected.' }
                };
            }
            console.error('Error requesting HID device:', error);
            return {
                ok: false,
                notification: { type: 'error', message: error.message || 'Error requesting device' }
            };
        }
    }

    public async connect(): Promise<void> {
        if (!this.selectedDevice) {
            throw new Error('No device selected');
        }
        await this.openDevice(this.selectedDevice);
    }

    private async openDevice(dev: HIDDeviceLike): Promise<void> {
        try {
            // Check for any "ghost" connections that might be hanging
            const nav = navigator as any;
            if ('hid' in nav) {
                const existing = await nav.hid.getDevices();
                for (const d of existing) {
                    if (d !== dev && d.opened && d.vendorId === VENDOR_ID && d.productId === PRODUCT_ID) {
                        console.log('[HIDTransport] Closing ghost connection to existing device...');
                        try { await d.close(); } catch { /* ignore */ }
                    }
                }
            }

            if (!dev.opened) {
                // Mandatory small pre-delay (Chromium bug workaround for quick reconnections)
                await new Promise(r => setTimeout(r, 300));
                
                try {
                    await dev.open();
                } catch (e) {
                    // Second attempt with longer delay
                    console.warn('[HIDTransport] First open attempt failed, retrying...', e);
                    await new Promise(r => setTimeout(r, 800));
                    await dev.open();
                }
            }

            this.device = dev;
            
            // Initialize protocol
            this.protocol = new CommProtocol({
                sendRaw: async (data) => {
                    if (this.device && this.device.opened) {
                        // Pad with zeroes up to COMM_REPORT_SIZE for USB
                        const padded = new Uint8Array(COMM_REPORT_SIZE);
                        padded.set(data);
                        await this.device.sendReport(COMM_REPORT_ID, padded);
                    }
                },
                onRawReceived: (cb) => {
                    this.device!.addEventListener('inputreport', (e: any) => {
                        if (e.reportId === COMM_REPORT_ID) {
                            cb(new Uint8Array(e.data.buffer));
                        }
                    });
                },
            });
            this.protocol.setMaxPacketSize(63); // 64 minus report ID

            // Register existing observers
            this.statusUpdateCallbacks.forEach(cb => this.protocol!.onStatusUpdate(cb));
            this.logCallbacks.forEach(cb => this.protocol!.onLogReceived(cb));
            this.rawPacketCallbacks.forEach(cb => this.protocol!.onRawPacket(cb));

            console.log(`Connected to HID device: ${dev.productName}`);
            this.notifyConnectionChange(true);
            this.stopReconnectPolling();
        } catch (e) {
            console.error('[HIDTransport] Failed to open device:', e);
            this.device = null;
            this.protocol = null;
            throw e;
        }
    }

    public async disconnect(forceReset: boolean = false): Promise<void> {
        if (forceReset) {
            this.wantConnection = false;
        }
        this.stopReconnectPolling();
        if (this.device) {
            try { await this.device.close(); } catch { /* may already be gone */ }
            console.log('Disconnected from HID device.');
            this.device = null;
        }
        if (this.protocol) {
            this.protocol.cleanup();
            this.protocol = null;
        }
        this.notifyConnectionChange(false);
    }

    public isConnected(): boolean {
        return this.device !== null && this.device.opened;
    }

    public getDeviceName(): string {
        return this.device?.productName || 'TEF Full Layout';
    }
    
    public getTransportName(): string {
        return 'USB';
    }

    public getDeviceId(): string {
        if (!this.selectedDevice) return 'unknown';
        // Use a combination of vendorId, productId, and productName for USB devices.
        // WebHID does not expose serial numbers for privacy/fingerprinting reasons.
        return `usb_${this.selectedDevice.vendorId}_${this.selectedDevice.productId}_${this.selectedDevice.productName.replace(/\s+/g, '_')}`;
    }

    // ── Connection Observers ──

    public onConnectionChange(cb: ConnectionCallback): void { this.connectionCallbacks.add(cb); }
    public offConnectionChange(cb: ConnectionCallback): void { this.connectionCallbacks.delete(cb); }
    private notifyConnectionChange(connected: boolean): void {
        this.connectionCallbacks.forEach(cb => cb(connected));
    }

    public onStatusUpdate(callback: StatusUpdateCallback): void {
        this.statusUpdateCallbacks.add(callback);
        if (this.protocol) this.protocol.onStatusUpdate(callback);
    }
    public offStatusUpdate(callback: StatusUpdateCallback): void {
        this.statusUpdateCallbacks.delete(callback);
        if (this.protocol) this.protocol.offStatusUpdate(callback);
    }
    
    public onLogReceived(callback: LogCallback): void {
        this.logCallbacks.add(callback);
        if (this.protocol) this.protocol.onLogReceived(callback);
    }
    public offLogReceived(callback: LogCallback): void {
        this.logCallbacks.delete(callback);
        if (this.protocol) this.protocol.offLogReceived(callback);
    }
    
    public onRawPacket(callback: RawPacketCallback): void {
        this.rawPacketCallbacks.add(callback);
        if (this.protocol) this.protocol.onRawPacket(callback);
    }
    public offRawPacket(callback: RawPacketCallback): void {
        this.rawPacketCallbacks.delete(callback);
        if (this.protocol) this.protocol.offRawPacket(callback);
    }

    // ── Disconnect / Reconnect ──

    private handleDisconnect(event: any): void {
        const disconnected = event.device;
        if (this.device && disconnected === this.device) {
            console.log('HID device disconnected');
            this.device = null;
            if (this.protocol) {
                this.protocol.cleanup();
                this.protocol = null;
            }
            this.notifyConnectionChange(false);

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
        this.reconnectTimer = setInterval(() => this.tryReconnect(), RECONNECT_INTERVAL_MS);
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
                this.selectedDevice = target;
                await this.connect();
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
    // ── Protocol operations ──
    // ══════════════════════════════════════════════════════════

    public sendCommand(payload: Uint8Array, timeoutMs?: number): Promise<CommandResponse | null> {
        if (!this.protocol) return Promise.resolve(null);
        return this.protocol.sendCommand(payload, timeoutMs);
    }

    public sendCustomCommReport(data: Uint8Array): Promise<boolean> {
        if (!this.protocol) return Promise.resolve(false);
        return this.protocol.sendCustomCommReport(data);
    }

    public sendResponse(flags: number, data?: Uint8Array): Promise<boolean> {
        if (!this.protocol) return Promise.resolve(false);
        return this.protocol.sendResponse(flags, data);
    }

    public buildCommPacket(flags: number, remaining: number, data: Uint8Array): Uint8Array {
        if (!this.protocol) return new Uint8Array(0);
        return this.protocol.buildCommPacket(flags, remaining, data);
    }
}
