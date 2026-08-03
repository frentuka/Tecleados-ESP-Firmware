import type {
    CommandResponse,
    ConnectionCallback,
    StatusUpdateCallback,
    LogCallback,
    RawPacketCallback,
    ConnectionNotification,
} from '../types/device';

export interface ITransport {
    /** 
     * Request the user to select a device (used mainly by WebHID/WebBluetooth UI flow). 
     */
    requestDevice(): Promise<{ ok: boolean; notification?: ConnectionNotification }>;
    
    /**
     * Connect to the device if already selected or authorized.
     */
    connect(): Promise<void>;
    
    /**
     * Disconnect from the current device.
     */
    disconnect(forceReset?: boolean): Promise<void>;
    
    /**
     * Returns true if a device is currently connected.
     */
    isConnected(): boolean;
    
    /**
     * Returns the name of the connected device.
     */
    getDeviceName(): string;
    
    /**
     * Returns the name of the transport ("USB" or "Bluetooth").
     */
    getTransportName(): string;

    // ── Protocol operations ──────────────────────────────────────────────
    
    sendCommand(payload: Uint8Array, timeoutMs?: number): Promise<CommandResponse | null>;
    sendCustomCommReport(data: Uint8Array): Promise<boolean>;
    sendResponse(flags: number, data?: Uint8Array): Promise<boolean>;
    buildCommPacket(flags: number, remaining: number, data: Uint8Array): Uint8Array;

    // ── Observers ────────────────────────────────────────────────────────
    
    onStatusUpdate(callback: StatusUpdateCallback): void;
    offStatusUpdate(callback: StatusUpdateCallback): void;
    
    onConnectionChange(callback: ConnectionCallback): void;
    offConnectionChange(callback: ConnectionCallback): void;
    
    onLogReceived(callback: LogCallback): void;
    offLogReceived(callback: LogCallback): void;
    
    onRawPacket(callback: RawPacketCallback): void;
    offRawPacket(callback: RawPacketCallback): void;
}
