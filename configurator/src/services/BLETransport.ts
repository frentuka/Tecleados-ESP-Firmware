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
                filters: [
                    { services: [COMM_SERVICE_UUID] },
                    { namePrefix: 'Tecleados' }
                ],
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
                    this.txChar!.addEventListener('characteristicvaluechanged', (e: any) => {
                        cb(new Uint8Array(e.target.value.buffer));
                    });
                },
            });

            // Register existing observers
            this.statusUpdateCallbacks.forEach(cb => this.protocol!.onStatusUpdate(cb));
            this.logCallbacks.forEach(cb => this.protocol!.onLogReceived(cb));
            this.rawPacketCallbacks.forEach(cb => this.protocol!.onRawPacket(cb));

            // Subscribe to notifications (TX: device → configurator)
            await this.txChar.startNotifications();

            // Subscribe to MTU updates (Handle async BLE_GAP_EVENT_MTU race condition)
            const mtuChar = await service.getCharacteristic(COMM_MTU_CHAR_UUID);
            await mtuChar.startNotifications();
            mtuChar.addEventListener('characteristicvaluechanged', (event: any) => {
                const val = event.target.value;
                if (this.protocol && val) {
                    this.protocol.setMaxPacketSize(val.getUint16(0, true)); // little-endian
                }
            });
            
            // Read the initial MTU *after* subscribing to guarantee no updates are missed
            const mtuVal = await mtuChar.readValue();
            this.protocol.setMaxPacketSize(mtuVal.getUint16(0, true)); // little-endian

            this.notifyConnectionChange(true);
        } catch (error) {
            console.error('BLE connection failed:', error);
            this.device.removeEventListener('gattserverdisconnected', this.handleDisconnect);
            this.device = null;
            this.server = null;
            this.rxChar = null;
            this.txChar = null;
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
