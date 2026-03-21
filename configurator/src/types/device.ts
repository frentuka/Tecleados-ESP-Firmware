/**
 * Device-level types shared across the configurator.
 */

/** Response from a completed sendCommand call */
export interface CommandResponse {
    module: number;
    cmd: number;
    keyId: number;
    status: number; // esp_err_t (0 = ESP_OK)
    jsonText: string; // fully reassembled JSON payload
}

/** BLE/USB device status pushed from the ESP32 */
export interface DeviceStatus {
    mode: number;
    profile: number;
    pairing: number;
    bitmap: number;
}

/** A single entry in the communication log */
export interface LogMessage {
    id: number;
    timestamp: Date;
    data: Uint8Array;
    text: string;
}

/** Physical key position in the keyboard layout */
export interface PhysKey {
    row: number;
    col: number;
    w: number;
    h: number;
    x: number;
    y: number;
}

/** Layer data: ROWS × COLS matrix of action codes */
export type LayerData = number[][];

// ── Callback types ──────────────────────────────────────────────────────
export type LogCallback = (logData: Uint8Array) => void;
export type RawPacketCallback = (data: Uint8Array, direction: 'rx' | 'tx') => void;
export type ConnectionCallback = (connected: boolean) => void;
export type StatusUpdateCallback = (status: DeviceStatus) => void;
