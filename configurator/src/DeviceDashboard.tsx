import React, { useState, useEffect, useCallback, useRef } from 'react';
import { hidService } from './HIDService';
import type { DeviceIdentity } from './services/DeviceController';
import {
    SPLIT_STATE_DISABLED,
    SPLIT_STATE_IDLE,
    SPLIT_STATE_PAIRING,
    SPLIT_STATE_CONNECTING,
    SPLIT_STATE_CONNECTED,
    SPLIT_STATE_DISCONNECTED,
    SPLIT_ROLE_NONE,
    SPLIT_ROLE_MASTER,
    SPLIT_ROLE_SLAVE,
} from './types/protocol';
import type { DeviceStatus } from './types/device';
import { useNotificationStore } from './stores/notificationStore';
import { withTimeout, TimeoutError } from './utils/withTimeout';
import './assets/css/device-dashboard.css';

// ── Split label helpers ───────────────────────────────────────────────────────

function stateLabel(s: number): string {
    switch (s) {
        case SPLIT_STATE_DISABLED:     return 'Disabled';
        case SPLIT_STATE_IDLE:         return 'Idle (unpaired)';
        case SPLIT_STATE_PAIRING:      return 'Pairing…';
        case SPLIT_STATE_CONNECTING:   return 'Connecting…';
        case SPLIT_STATE_CONNECTED:    return 'Connected';
        case SPLIT_STATE_DISCONNECTED: return 'Disconnected';
        default:                       return `Unknown (${s})`;
    }
}

function roleLabel(r: number): string {
    switch (r) {
        case SPLIT_ROLE_MASTER: return 'MASTER';
        case SPLIT_ROLE_SLAVE:  return 'SLAVE';
        default:                return '—';
    }
}

function stateColor(s: number): string {
    switch (s) {
        case SPLIT_STATE_CONNECTED:    return 'var(--success-color)';
        case SPLIT_STATE_PAIRING:
        case SPLIT_STATE_CONNECTING:   return '#f39c12';
        case SPLIT_STATE_DISCONNECTED: return 'var(--danger-color)';
        default:                       return 'rgba(255,255,255,0.3)';
    }
}

// ── Remote matrix visualiser (dev mode) ──────────────────────────────────────

const MATRIX_ROWS = 6;
const MATRIX_COLS = 18;

function MatrixVisualiser({ bitmap }: { bitmap: Uint8Array | null }) {
    if (!bitmap) return (
        <div className="dd-matrix-empty">No data yet</div>
    );

    const cells: React.ReactNode[] = [];
    for (let row = 0; row < MATRIX_ROWS; row++) {
        for (let col = 0; col < MATRIX_COLS; col++) {
            const bitIdx = row * MATRIX_COLS + col;
            const byteIdx = Math.floor(bitIdx / 8);
            const bitMask = 1 << (bitIdx % 8);
            const pressed = byteIdx < bitmap.length && (bitmap[byteIdx] & bitMask) !== 0;
            cells.push(
                <div
                    key={`${row}-${col}`}
                    title={`R${row} C${col}`}
                    className={`dd-matrix-cell ${pressed ? 'pressed' : ''}`}
                />
            );
        }
    }

    return (
        <div className="dd-matrix-grid">
            {cells}
        </div>
    );
}

// ── Defaults ──────────────────────────────────────────────────────────────────

const DEFAULT_IDENTITY: DeviceIdentity = {
    device_name: '',
    is_split: false,
    split_mirror_cols: false,
    split_variant: '',
    ble_shared_name: '',
    ble_shared_addr: '',
};

// ── Props ─────────────────────────────────────────────────────────────────────

interface DeviceDashboardProps {
    isConnected: boolean;
    isDeveloperMode: boolean;
    deviceStatus: DeviceStatus | null;
    onLog: (text: string) => void;
}

// ── Component ─────────────────────────────────────────────────────────────────

const DeviceDashboard: React.FC<DeviceDashboardProps> = ({
    isConnected,
    isDeveloperMode,
    deviceStatus,
    onLog,
}) => {
    const { showNotification } = useNotificationStore();

    // ── Identity state ────────────────────────────────────────────────────

    const [saved, setSaved] = useState<DeviceIdentity>(DEFAULT_IDENTITY);
    const [draft, setDraft] = useState<DeviceIdentity>(DEFAULT_IDENTITY);
    const [isSaving, setIsSaving] = useState(false);
    const [isLoading, setIsLoading] = useState(false);
    const [saveResult, setSaveResult] = useState<'ok' | 'err' | null>(null);

    const isDirty =
        draft.device_name      !== saved.device_name      ||
        draft.is_split          !== saved.is_split          ||
        draft.split_mirror_cols !== saved.split_mirror_cols ||
        draft.split_variant     !== saved.split_variant     ||
        draft.ble_shared_name  !== saved.ble_shared_name   ||
        draft.ble_shared_addr  !== saved.ble_shared_addr;

    // ── Split state ───────────────────────────────────────────────────────

    const [pairingTimeout, setPairingTimeout] = useState(30);
    const [testModeActive, setTestModeActive] = useState(false);
    const [remoteMatrix, setRemoteMatrix] = useState<Uint8Array | null>(null);
    const [isBenchmarking, setIsBenchmarking] = useState(false);
    const pollRef = useRef<ReturnType<typeof setInterval> | null>(null);

    const splitState = deviceStatus?.split_state ?? SPLIT_STATE_DISABLED;
    const splitRole  = deviceStatus?.split_role  ?? SPLIT_ROLE_NONE;

    const isPairing    = splitState === SPLIT_STATE_PAIRING;
    const isConnectedS = splitState === SPLIT_STATE_CONNECTED;

    // ── Fetch identity ────────────────────────────────────────────────────

    const fetchIdentity = useCallback(async () => {
        if (!isConnected) return;
        setIsLoading(true);
        const identity = await hidService.fetchDeviceIdentity();
        setIsLoading(false);
        if (identity) {
            setSaved(identity);
            setDraft(identity);
            onLog('Device Identity: loaded');
        } else {
            onLog('Device Identity: failed to load');
            showNotification('Failed to load device identity', 'error');
        }
    }, [isConnected, onLog, showNotification]);

    useEffect(() => {
        if (isConnected) fetchIdentity();
        else {
            setSaved(DEFAULT_IDENTITY);
            setDraft(DEFAULT_IDENTITY);
        }
    }, [isConnected, fetchIdentity]);

    // ── Save identity ─────────────────────────────────────────────────────

    const handleSave = async () => {
        if (!isConnected || isSaving) return;
        setIsSaving(true);
        setSaveResult(null);
        try {
            const ok = await withTimeout(hidService.saveDeviceIdentity(draft), 7000);
            setSaveResult(ok ? 'ok' : 'err');
            if (ok) {
                setSaved(draft);
                onLog(`Device Identity: saved (name="${draft.device_name}", ble_name="${draft.ble_shared_name}", split=${draft.is_split})`);
                showNotification('Device identity saved successfully', 'success');
            } else {
                onLog('Device Identity: save failed');
                showNotification('Failed to save device identity', 'error');
            }
        } catch (e) {
            setSaveResult('err');
            if (e instanceof TimeoutError) {
                onLog('Device Identity: save timed out');
                showNotification('Save timed out — please retry', 'error');
            } else {
                onLog('Device Identity: save failed');
                showNotification('Failed to save device identity', 'error');
            }
        } finally {
            setIsSaving(false);
            setTimeout(() => setSaveResult(null), 2000);
        }
    };

    const setField = <K extends keyof DeviceIdentity>(key: K, value: DeviceIdentity[K]) =>
        setDraft(prev => ({ ...prev, [key]: value }));

    const applyClass = `btn btn-sm ${
        saveResult === 'ok'  ? 'btn-success' :
        saveResult === 'err' ? 'btn-danger'  :
        isDirty ? 'btn-success btn-apply-active' : 'btn-secondary btn-apply-idle'
    }`;

    // ── Split remote matrix polling ───────────────────────────────────────

    useEffect(() => {
        if (testModeActive && isConnected && splitRole === SPLIT_ROLE_MASTER) {
            pollRef.current = setInterval(async () => {
                const mat = await hidService.splitGetRemoteMatrix();
                if (mat) setRemoteMatrix(mat);
            }, 50);
        } else {
            if (pollRef.current) {
                clearInterval(pollRef.current);
                pollRef.current = null;
            }
            if (!testModeActive) setRemoteMatrix(null);
        }
        return () => {
            if (pollRef.current) clearInterval(pollRef.current);
        };
    }, [testModeActive, isConnected, splitRole]);

    // Stop test mode when disconnected
    useEffect(() => {
        if (!isConnected || !isConnectedS) setTestModeActive(false);
    }, [isConnected, isConnectedS]);

    // ── Split actions ─────────────────────────────────────────────────────

    const handleStartPairing = useCallback(async () => {
        try {
            const ok = await withTimeout(hidService.splitStartPairing(pairingTimeout * 1000), 7000);
            if (ok) {
                onLog(`Split: pairing started (${pairingTimeout}s timeout)`);
                showNotification(`Split pairing started (${pairingTimeout}s timeout)`, 'success');
            } else {
                onLog('Split: start pairing failed');
                showNotification('Split pairing failed to start', 'error');
            }
        } catch (e) {
            const msg = e instanceof TimeoutError ? 'Pairing request timed out — please retry' : 'Split pairing failed to start';
            onLog(`Split: start pairing failed — ${e instanceof TimeoutError ? 'timeout' : 'error'}`);
            showNotification(msg, 'error');
        }
    }, [pairingTimeout, onLog, showNotification]);

    const handleCancelPairing = useCallback(async () => {
        try {
            const ok = await withTimeout(hidService.splitCancelPairing(), 7000);
            if (ok) {
                onLog('Split: pairing cancelled');
                showNotification('Split pairing cancelled', 'success');
            } else {
                onLog('Split: cancel pairing failed');
                showNotification('Failed to cancel split pairing', 'error');
            }
        } catch (e) {
            const msg = e instanceof TimeoutError ? 'Cancel request timed out — please retry' : 'Failed to cancel split pairing';
            onLog(`Split: cancel pairing failed — ${e instanceof TimeoutError ? 'timeout' : 'error'}`);
            showNotification(msg, 'error');
        }
    }, [onLog, showNotification]);

    const handleUnpair = useCallback(async () => {
        try {
            const ok = await withTimeout(hidService.splitUnpair(), 7000);
            if (ok) {
                onLog('Split: unpaired');
                showNotification('Split halves unpaired', 'success');
            } else {
                onLog('Split: unpair failed');
                showNotification('Failed to unpair split halves', 'error');
            }
        } catch (e) {
            const msg = e instanceof TimeoutError ? 'Unpair request timed out — please retry' : 'Failed to unpair split halves';
            onLog(`Split: unpair failed — ${e instanceof TimeoutError ? 'timeout' : 'error'}`);
            showNotification(msg, 'error');
        }
    }, [onLog, showNotification]);

    const handleRoleSwap = useCallback(async () => {
        try {
            const ok = await withTimeout(hidService.splitRoleSwap(), 7000);
            if (ok) {
                onLog('Split: role swap requested');
                showNotification('Split role swap requested', 'success');
            } else {
                onLog('Split: role swap failed');
                showNotification('Failed to request role swap', 'error');
            }
        } catch (e) {
            const msg = e instanceof TimeoutError ? 'Role swap timed out — please retry' : 'Failed to request role swap';
            onLog(`Split: role swap failed — ${e instanceof TimeoutError ? 'timeout' : 'error'}`);
            showNotification(msg, 'error');
        }
    }, [onLog, showNotification]);

    const handleRunBenchmark = useCallback(async () => {
        const ok = await hidService.splitRunBenchmark();
        if (ok) {
            setIsBenchmarking(true);
            showNotification('Running benchmark...', 'info');
            const bRef = setInterval(async () => {
                const res = await hidService.splitGetBench();
                if (res && (!res.active && res.min > 0)) {
                    clearInterval(bRef);
                    setIsBenchmarking(false);
                    const resultText = `Min: ${(res.min / 1000).toFixed(2)}ms  •  Avg: ${(res.avg / 1000).toFixed(2)}ms  •  Max: ${(res.max / 1000).toFixed(2)}ms  •  Lost: ${res.lost}`;
                    showNotification(resultText, 'success', 'Benchmark Results', 8000);
                    onLog(`Split: Benchmark Results -> ${resultText}`);
                }
            }, 500);
        } else {
            onLog('Split: Benchmark start failed');
            showNotification('Failed to start benchmark', 'error');
        }
    }, [onLog, showNotification]);

    // ── Render ────────────────────────────────────────────────────────────

    const stateCol = stateColor(splitState);

    return (
        <div className="dd-page">

            {/* ── Page header ── */}
            <div className="dd-page-header">
                <div className="dd-page-header-left">
                    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ opacity: 0.6 }}>
                        <rect x="2" y="5" width="20" height="14" rx="2"/>
                        <circle cx="8" cy="12" r="2"/>
                        <path d="M14 9h4M14 12h4M14 15h2"/>
                    </svg>
                    <span className="dd-page-title">Device</span>
                    {isLoading && <span className="dd-loading-hint">loading…</span>}
                </div>
                <button
                    id="dd-apply-btn"
                    className={applyClass}
                    onClick={handleSave}
                    disabled={!isConnected || isSaving || (!isDirty && saveResult === null)}
                >
                    {isSaving ? 'Saving…' : saveResult === 'ok' ? '✓ Saved' : saveResult === 'err' ? '✗ Error' : 'Apply'}
                </button>
            </div>

            {/* ── Sections ── */}
            <div className="dd-sections">

                {/* ── Device Name (always visible) ─────────────────── */}
                <DdSection label="Device Name">
                    <div className="dd-field-row">
                        <input
                            id="dd-device-name"
                            type="text"
                            maxLength={31}
                            value={draft.device_name}
                            onChange={e => setField('device_name', e.target.value)}
                            placeholder="Antigravity KB"
                            className="dd-input"
                        />
                        <span className="dd-char-count">{draft.device_name.length}/31</span>
                    </div>
                    <p className="dd-hint">
                        Bluetooth and USB device name shown to hosts on pairing.{' '}
                        <span className="dd-hint-warn">Name changes take effect after reconnect or restart.</span>
                    </p>
                </DdSection>

                {/* ── Split Link (always visible) ──────────────────── */}
                <DdSection label="Split Link">
                    {/* Status row */}
                    <div className="dd-split-status-row">
                        <div className="dd-split-status-left">
                            <span
                                className="dd-split-dot"
                                style={{
                                    background: stateCol,
                                    boxShadow: `0 0 8px ${stateCol}`,
                                    animation: isPairing ? 'dd-pulse 1.2s infinite ease-in-out' : 'none',
                                }}
                            />
                            <span className="dd-split-state-label">{stateLabel(splitState)}</span>
                            {splitRole !== SPLIT_ROLE_NONE && (
                                <span className="dd-split-role-badge">{roleLabel(splitRole)}</span>
                            )}
                        </div>

                        {/* Dev-only: Role Swap + Benchmark (shown only when connected) */}
                        {isDeveloperMode && isConnectedS && (
                            <div className="dd-split-actions">
                                <button
                                    className="btn-banner-action"
                                    onClick={handleRoleSwap}
                                    disabled={!isConnected}
                                    title="Switch Roles"
                                >
                                    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                                        <path d="M16 3l4 4-4 4"/><path d="M20 7H4"/><path d="M8 21l-4-4 4-4"/><path d="M4 17h16"/>
                                    </svg>
                                    <span>Switch Roles</span>
                                </button>

                                {splitRole === SPLIT_ROLE_MASTER && (
                                    <button
                                        className={`btn-banner-action ${isBenchmarking ? 'loading' : ''}`}
                                        onClick={handleRunBenchmark}
                                        disabled={!isConnected || isBenchmarking}
                                        title="Run Benchmark"
                                    >
                                        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                                            <polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/>
                                        </svg>
                                        <span>{isBenchmarking ? 'Benchmarking…' : 'Benchmark'}</span>
                                    </button>
                                )}
                            </div>
                        )}
                    </div>

                    {/* Pairing controls */}
                    <div className="dd-split-pairing-row">
                        {!isPairing ? (
                            <div className="dd-split-action-row">
                                {!isConnectedS ? (
                                    <button className="btn btn-success btn-sm" onClick={handleStartPairing} disabled={!isConnected}>
                                        Start Pairing
                                    </button>
                                ) : (
                                    <button className="btn btn-danger btn-sm" onClick={handleUnpair} disabled={!isConnected}>
                                        Unpair
                                    </button>
                                )}
                                {isDeveloperMode && !isConnectedS && (
                                    <label className="dd-timeout-label">
                                        Timeout
                                        <input
                                            id="dd-pairing-timeout"
                                            type="number"
                                            min={5} max={120} step={5}
                                            value={pairingTimeout}
                                            onChange={e => setPairingTimeout(Number(e.target.value))}
                                            className="dd-timeout-input"
                                        />
                                        <span style={{ fontSize: 11, opacity: 0.5 }}>s</span>
                                    </label>
                                )}
                            </div>
                        ) : (
                            <div className="dd-split-action-row">
                                <div className="dd-pairing-indicator">
                                    <span className="dd-pairing-dot" />
                                    Pairing in progress…
                                </div>
                                <button className="btn btn-secondary btn-sm" onClick={handleCancelPairing} disabled={!isConnected}>
                                    Cancel
                                </button>
                            </div>
                        )}
                    </div>
                </DdSection>

                {/* ── Split Configuration (dev mode only) ─────────── */}
                {isDeveloperMode && (
                    <DdSection label="Split Configuration">
                        <div className="dd-toggle-row">
                            <Toggle
                                id="dd-is-split"
                                checked={draft.is_split}
                                onChange={v => setField('is_split', v)}
                            />
                            <label htmlFor="dd-is-split" className="dd-toggle-label">
                                This device is part of a split keyboard
                            </label>
                        </div>

                        <div className={`dd-split-fields ${draft.is_split ? '' : 'disabled'}`}>
                            <FieldGroup label="Mirror Columns" hint="Column N maps to (MAX_COL−N). Enable on the mirrored half (e.g. right side).">
                                <div className="dd-toggle-row" style={{ marginTop: 4 }}>
                                    <Toggle
                                        id="dd-mirror-cols"
                                        checked={draft.split_mirror_cols}
                                        onChange={v => setField('split_mirror_cols', v)}
                                    />
                                    <label htmlFor="dd-mirror-cols" className="dd-toggle-label">
                                        {draft.split_mirror_cols ? 'Enabled' : 'Disabled'}
                                    </label>
                                </div>
                            </FieldGroup>

                            <FieldGroup label="Variant Name" hint={`e.g. "Left", "Right", "Numpad"`}>
                                <input
                                    id="dd-split-variant"
                                    type="text"
                                    maxLength={15}
                                    value={draft.split_variant}
                                    onChange={e => setField('split_variant', e.target.value)}
                                    placeholder="Left"
                                    className="dd-input"
                                />
                            </FieldGroup>
                        </div>
                    </DdSection>
                )}

                {/* ── BLE Identity (dev mode only) ─────────────────── */}
                {isDeveloperMode && (
                    <DdSection label="BLE Identity (Split)">
                        <p className="dd-hint" style={{ marginTop: 0, marginBottom: 16 }}>
                            Set these to the same values on both halves so they share one BLE identity.
                            The host will reconnect automatically when roles swap.
                        </p>
                        <div className="dd-ble-fields">
                            <FieldGroup
                                label="BLE Name"
                                hint="Overrides Device Name in BLE advertisements. Leave blank to use Device Name."
                            >
                                <div className="dd-field-row">
                                    <input
                                        id="dd-ble-shared-name"
                                        type="text"
                                        maxLength={31}
                                        value={draft.ble_shared_name}
                                        onChange={e => setField('ble_shared_name', e.target.value)}
                                        placeholder={draft.device_name || 'Antigravity KB'}
                                        className="dd-input"
                                    />
                                    <span className="dd-char-count">{draft.ble_shared_name.length}/31</span>
                                </div>
                            </FieldGroup>

                            <FieldGroup
                                label="BLE MAC Address"
                                hint='Shared static random address base, e.g. "C2:13:57:9B:EF:01". Leave blank for auto. Set bit 7+6 of first byte for Static Random type (e.g. C2:…).'
                            >
                                <input
                                    id="dd-ble-shared-addr"
                                    type="text"
                                    maxLength={17}
                                    value={draft.ble_shared_addr}
                                    onChange={e => setField('ble_shared_addr', e.target.value.toUpperCase())}
                                    placeholder="AA:BB:CC:DD:EE:FF"
                                    className="dd-input dd-input-mono"
                                />
                            </FieldGroup>
                        </div>
                        <p className="dd-hint" style={{ marginTop: 12 }}>
                            Changes take effect after restarting both halves.
                        </p>
                    </DdSection>
                )}

                {/* ── Remote Key Test (dev mode, master, split connected) ── */}
                {isDeveloperMode && isConnectedS && splitRole === SPLIT_ROLE_MASTER && (
                    <DdSection label="Remote Key Test">
                        <div className="dd-section-header-row">
                            <p className="dd-hint" style={{ margin: 0 }}>
                                Live-monitor the slave half's key matrix.
                            </p>
                            <button
                                id="dd-test-toggle"
                                className={`btn btn-sm ${testModeActive ? 'btn-danger' : 'btn-secondary'}`}
                                onClick={() => setTestModeActive(!testModeActive)}
                            >
                                {testModeActive ? 'Stop' : 'Start Test'}
                            </button>
                        </div>
                        {testModeActive && (
                            <div className="dd-matrix-wrapper">
                                <MatrixVisualiser bitmap={remoteMatrix} />
                            </div>
                        )}
                    </DdSection>
                )}

            </div>

            <style>{`
                @keyframes dd-pulse {
                    0%   { opacity: 1; }
                    50%  { opacity: 0.25; }
                    100% { opacity: 1; }
                }
            `}</style>
        </div>
    );
};

// ── Sub-components ────────────────────────────────────────────────────────────

function DdSection({ label, children }: { label: string; children: React.ReactNode }) {
    return (
        <div className="dd-section">
            <div className="dd-section-label">{label}</div>
            <div className="dd-section-body">{children}</div>
        </div>
    );
}

function FieldGroup({ label, hint, children }: { label: string; hint?: string; children: React.ReactNode }) {
    return (
        <div className="dd-field-group">
            <span className="dd-field-label">{label}</span>
            {children}
            {hint && <span className="dd-hint">{hint}</span>}
        </div>
    );
}

function Toggle({ id, checked, onChange }: { id: string; checked: boolean; onChange: (v: boolean) => void }) {
    return (
        <div
            id={id}
            role="switch"
            aria-checked={checked}
            onClick={() => onChange(!checked)}
            className={`dd-toggle ${checked ? 'active' : ''}`}
        >
            <div className="dd-toggle-thumb" />
        </div>
    );
}

export default DeviceDashboard;
