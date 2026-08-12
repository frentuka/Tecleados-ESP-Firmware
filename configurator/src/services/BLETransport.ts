import type { ITransport } from './ITransport';
import { CommProtocol } from './CommProtocol';
import type {
    CommandResponse,
    ConnectionCallback,
    StatusUpdateCallback,
    LogCallback,
    RawPacketCallback,
    ConnectionNotification,
} from '../types/device';

// Service and characteristic UUIDs (matching firmware)
const COMM_SERVICE_UUID    = '4d544546-0001-4b42-4254-455f434f4d4d';
const COMM_RX_CHAR_UUID    = '4d544546-0002-4b42-4254-455f434f4d4d';
const COMM_TX_CHAR_UUID    = '4d544546-0003-4b42-4254-455f434f4d4d';
const COMM_MTU_CHAR_UUID   = '4d544546-0004-4b42-4254-455f434f4d4d';

export class BLETransport implements ITransport {
    private device: BluetoothDevice | null = null;
    private server: BluetoothRemoteGATTServer | null = null;
    private rxChar: BluetoothRemoteGATTCharacteristic | null = null;
    private txChar: BluetoothRemoteGATTCharacteristic | null = null;
    private mtuChar: BluetoothRemoteGATTCharacteristic | null = null;
    private onRawReceivedCallback: ((data: Uint8Array) => void) | null = null;

    private handleTxCharValue = (event: any) => {
        if (!this.txChar) return; // Prevent zombie listeners from processing events
        if (this.onRawReceivedCallback && event.target && event.target.value) {
            // IMPORTANT: Use byteOffset + byteLength to avoid reading garbage from a shared
            // ArrayBuffer. Web Bluetooth DataViews may have a non-zero byteOffset on Chrome
            // (Linux/Android), so new Uint8Array(dv.buffer) would read from the wrong position.
            const dv: DataView = event.target.value;
            this.onRawReceivedCallback(new Uint8Array(dv.buffer, dv.byteOffset, dv.byteLength));
        }
    };

    private handleMtuCharValue = (event: any) => {
        if (!this.mtuChar) return;
        if (event.target && event.target.value) {
            const dv: DataView = event.target.value;
            if (dv.byteLength < 2) return; // Must have at least 2 bytes for a uint16
            // DataView.getUint16() already operates relative to the DataView's own byteOffset,
            // so this is safe even if the underlying ArrayBuffer has a non-zero offset.
            const mtuVal = dv.getUint16(0, true);
            if (this.protocol) {
                this.protocol.setMaxPacketSize(mtuVal);
            }
        }
    };
    
    private protocol: CommProtocol | null = null;
    private connectionCallbacks: Set<ConnectionCallback> = new Set();
    private statusUpdateCallbacks: Set<StatusUpdateCallback> = new Set();
    private logCallbacks: Set<LogCallback> = new Set();
    private rawPacketCallbacks: Set<RawPacketCallback> = new Set();
    
    // For storing reconnect attempts logic, if applicable
    private wantConnection = false;

    constructor() {
        this.handleDisconnect = this.handleDisconnect.bind(this);
    }

    public async requestDevice(): Promise<{ ok: boolean; notification?: ConnectionNotification }> {
        try {
            const nav = navigator as any;
            if (!('bluetooth' in nav)) {
                return {
                    ok: false,
                    notification: { type: 'error', message: 'Web Bluetooth is not supported in this browser.' }
                };
            }

            this.device = await nav.bluetooth.requestDevice({
                filters: [{ services: [COMM_SERVICE_UUID] }],
                optionalServices: [COMM_SERVICE_UUID],
            });

            this.wantConnection = true;
            await this.connect();
            return { ok: true };
        } catch (error: any) {
            if (error.name === 'NotFoundError') {
                return {
                    ok: false,
                    notification: { type: 'info', message: 'Connection cancelled: No device selected.' }
                };
            }
            console.error('Error requesting BLE device:', error);
            return {
                ok: false,
                notification: { type: 'error', message: error.message || 'Error requesting device' }
            };
        }
    }

    public async connect(): Promise<void> {
        if (!this.device || !this.device.gatt) {
            throw new Error('No device selected or GATT not available');
        }

        this.device.addEventListener('gattserverdisconnected', this.handleDisconnect);
        
        try {
            this.server = await this.device.gatt.connect();
            const service = await this.server.getPrimaryService(COMM_SERVICE_UUID);
            this.rxChar = await service.getCharacteristic(COMM_RX_CHAR_UUID);
            this.txChar = await service.getCharacteristic(COMM_TX_CHAR_UUID);
            
            // Initialize protocol
            this.protocol = new CommProtocol({
                sendRaw: async (data) => {
                    if (this.rxChar) {
                        await this.rxChar.writeValueWithoutResponse(data as unknown as BufferSource);
                    }
                },
                onRawReceived: (cb) => {
                    this.onRawReceivedCallback = cb;
                },
            });

            this.txChar.addEventListener('characteristicvaluechanged', this.handleTxCharValue);

            // Register existing observers
            this.statusUpdateCallbacks.forEach(cb => this.protocol!.onStatusUpdate(cb));
            this.logCallbacks.forEach(cb => this.protocol!.onLogReceived(cb));
            this.rawPacketCallbacks.forEach(cb => this.protocol!.onRawPacket(cb));

            // Subscribe to notifications (TX: device → configurator)
            await this.txChar.startNotifications();

            // Subscribe to MTU updates (Handle async BLE_GAP_EVENT_MTU race condition)
            this.mtuChar = await service.getCharacteristic(COMM_MTU_CHAR_UUID);
            await this.mtuChar.startNotifications();
            this.mtuChar.addEventListener('characteristicvaluechanged', this.handleMtuCharValue);
            
            // Read the initial MTU *after* subscribing to guarantee no updates are missed
            const mtuVal = await this.mtuChar.readValue();
            this.protocol.setMaxPacketSize(mtuVal.getUint16(0, true)); // little-endian

            this.notifyConnectionChange(true);
        } catch (error) {
            console.error('BLE connection failed:', error);
            if (this.device) {
                this.device.removeEventListener('gattserverdisconnected', this.handleDisconnect);
            }
            this.device = null;
            this.server = null;
            this.rxChar = null;
            this.txChar = null;
            this.mtuChar = null;
            throw error;
        }
    }

    private handleDisconnect = () => {
        console.log('BLE device disconnected');
        this.server = null;
        this.rxChar = null;
        this.txChar = null;
        if (this.protocol) {
            this.protocol.cleanup();
            this.protocol = null;
        }
        this.notifyConnectionChange(false);

        if (this.wantConnection) {
            this.tryReconnect();
        }
    }

    private async tryReconnect() {
        if (!this.wantConnection || !this.device || !this.device.gatt) return;
        
        let attempts = 0;
        const maxAttempts = 5;
        
        const attemptConnection = async () => {
            if (!this.wantConnection) return;
            try {
                console.log(`BLE reconnect attempt ${attempts + 1}...`);
                await this.connect();
                console.log('BLE reconnected successfully');
            } catch (err) {
                attempts++;
                if (attempts < maxAttempts) {
                    const delay = Math.min(1000 * Math.pow(2, attempts), 10000);
                    setTimeout(attemptConnection, delay);
                } else {
                    console.error('BLE reconnect failed after max attempts');
                }
            }
        };

        setTimeout(attemptConnection, 2000);
    }

    public async disconnect(forceReset: boolean = false): Promise<void> {
        if (forceReset) {
            this.wantConnection = false;
        }
        if (this.txChar) {
            this.txChar.removeEventListener('characteristicvaluechanged', this.handleTxCharValue);
            try { await this.txChar.stopNotifications(); } catch (e) { console.warn('Failed to stop txChar notifications', e); }
        }
        if (this.mtuChar) {
            this.mtuChar.removeEventListener('characteristicvaluechanged', this.handleMtuCharValue);
            try { await this.mtuChar.stopNotifications(); } catch (e) { console.warn('Failed to stop mtuChar notifications', e); }
        }
        if (this.device && this.device.gatt) {
            this.device.removeEventListener('gattserverdisconnected', this.handleDisconnect);
            if (this.device.gatt.connected) {
                this.device.gatt.disconnect();
            }
        }
        this.server = null;
        this.rxChar = null;
        this.txChar = null;
        this.device = null;
        if (this.protocol) {
            this.protocol.cleanup();
            this.protocol = null;
        }
        this.notifyConnectionChange(false);
    }

    public isConnected(): boolean {
        return this.server !== null && this.device !== null && this.device.gatt !== undefined && this.device.gatt.connected;
    }

    public getDeviceName(): string {
        return this.device?.name || 'TEF BLE Keyboard';
    }

    public getTransportName(): string {
        return 'Bluetooth';
    }

    public getDeviceId(): string {
        return this.device ? this.device.id : 'unknown';
    }

    // ── Protocol operations ──────────────────────────────────────────────

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

    // ── Observers ────────────────────────────────────────────────────────

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
}
