import React, { useState, useEffect, useCallback, useRef } from 'react';
import { hidService } from './HIDService';
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
import './assets/css/split-dashboard.css';

// ── Label helpers ─────────────────────────────────────────────────────────────

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

// ── Test mode: keyboard visualiser ───────────────────────────────────────────

const MATRIX_ROWS = 6;
const MATRIX_COLS = 18;

function MatrixVisualiser({ bitmap }: { bitmap: Uint8Array | null }) {
    if (!bitmap) return (
        <div className="split-matrix-empty">No data yet</div>
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
                    className={`split-matrix-cell ${pressed ? 'pressed' : ''}`}
                />
            );
        }
    }

    return (
        <div className="split-matrix-grid">
            {cells}
        </div>
    );
}

// ── Props ─────────────────────────────────────────────────────────────────────

interface SplitDashboardProps {
    isConnected: boolean;
    deviceStatus: DeviceStatus | null;
    isDeveloperMode: boolean;
    onLog: (text: string) => void;
}

// ── Component ─────────────────────────────────────────────────────────────────

const SplitDashboard: React.FC<SplitDashboardProps> = ({ isConnected, deviceStatus, isDeveloperMode, onLog }) => {
    const { showNotification } = useNotificationStore();
    const [pairingTimeout, setPairingTimeout] = useState(30);
    const [testModeActive, setTestModeActive] = useState(false);
    const [remoteMatrix, setRemoteMatrix] = useState<Uint8Array | null>(null);
    const [isBenchmarking, setIsBenchmarking] = useState(false);
    const pollRef = useRef<ReturnType<typeof setInterval> | null>(null);

    const splitState = deviceStatus?.split_state ?? SPLIT_STATE_DISABLED;
    const splitRole  = deviceStatus?.split_role  ?? SPLIT_ROLE_NONE;

    const isPairing    = splitState === SPLIT_STATE_PAIRING;
    const isConnectedS = splitState === SPLIT_STATE_CONNECTED;

    // ── Poll remote matrix in test mode ─────────────────────────────────

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
        if (!isConnected || !isConnectedS) {
            setTestModeActive(false);
        }
    }, [isConnected, isConnectedS]);

    // ── Actions ──────────────────────────────────────────────────────────

    const handleStartPairing = useCallback(async () => {
        try {
            const ok = await withTimeout(hidService.splitStartPairing(pairingTimeout * 1000), 7000);
            if (ok) {
                onLog(`Split: pairing started (${pairingTimeout}s timeout)`);
                showNotification(`Split pairing started (${pairingTimeout}s timeout)`, "success");
            } else {
                onLog('Split: start pairing failed');
                showNotification('Split pairing failed to start', "error");
            }
        } catch (e) {
            const msg = e instanceof TimeoutError ? 'Pairing request timed out — please retry' : 'Split pairing failed to start';
            onLog(`Split: start pairing failed — ${e instanceof TimeoutError ? 'timeout' : 'error'}`);
            showNotification(msg, "error");
        }
    }, [pairingTimeout, onLog, showNotification]);

    const handleCancelPairing = useCallback(async () => {
        try {
            const ok = await withTimeout(hidService.splitCancelPairing(), 7000);
            if (ok) {
                onLog('Split: pairing cancelled');
                showNotification('Split pairing cancelled', "success");
            } else {
                onLog('Split: cancel pairing failed');
                showNotification('Failed to cancel split pairing', "error");
            }
        } catch (e) {
            const msg = e instanceof TimeoutError ? 'Cancel request timed out — please retry' : 'Failed to cancel split pairing';
            onLog(`Split: cancel pairing failed — ${e instanceof TimeoutError ? 'timeout' : 'error'}`);
            showNotification(msg, "error");
        }
    }, [onLog, showNotification]);

    const handleUnpair = useCallback(async () => {
        try {
            const ok = await withTimeout(hidService.splitUnpair(), 7000);
            if (ok) {
                onLog('Split: unpaired');
                showNotification('Split halves unpaired', "success");
            } else {
                onLog('Split: unpair failed');
                showNotification('Failed to unpair split halves', "error");
            }
        } catch (e) {
            const msg = e instanceof TimeoutError ? 'Unpair request timed out — please retry' : 'Failed to unpair split halves';
            onLog(`Split: unpair failed — ${e instanceof TimeoutError ? 'timeout' : 'error'}`);
            showNotification(msg, "error");
        }
    }, [onLog, showNotification]);

    const handleRoleSwap = useCallback(async () => {
        try {
            const ok = await withTimeout(hidService.splitRoleSwap(), 7000);
            if (ok) {
                onLog('Split: role swap requested');
                showNotification('Split role swap requested', "success");
            } else {
                onLog('Split: role swap failed');
                showNotification('Failed to request role swap', "error");
            }
        } catch (e) {
            const msg = e instanceof TimeoutError ? 'Role swap timed out — please retry' : 'Failed to request role swap';
            onLog(`Split: role swap failed — ${e instanceof TimeoutError ? 'timeout' : 'error'}`);
            showNotification(msg, "error");
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
                    const resultText = `Min: ${(res.min/1000).toFixed(2)}ms  •  Avg: ${(res.avg/1000).toFixed(2)}ms  •  Max: ${(res.max/1000).toFixed(2)}ms  •  Lost: ${res.lost}`;
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
        <div className="split-page">

            {/* ── Status Banner ── */}
            <div className="split-status-banner">
                <div className="split-status-left">
                    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ opacity: 0.6 }}>
                        <path d="M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71"/>
                        <path d="M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71"/>
                    </svg>
                    <span className="split-status-title">Split Keyboard</span>
                    <div className="split-status-state">
                        <span
                            className="split-status-dot"
                            style={{
                                background: stateCol,
                                boxShadow: `0 0 8px ${stateCol}`,
                                animation: isPairing ? 'split-pulse 1.2s infinite ease-in-out' : 'none',
                            }}
                        />
                        <span className="split-status-label">{stateLabel(splitState)}</span>
                        {splitRole !== SPLIT_ROLE_NONE && (
                            <span className="split-role-badge">{roleLabel(splitRole)}</span>
                        )}
                    </div>
                </div>

                {isConnectedS && (
                    <div className="split-status-actions">
                        {isDeveloperMode && (
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
                        )}
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

            {/* ── Controls grid ── */}
            <div className="split-controls-grid">

                {/* Pairing Controls */}
                <div className="split-section">
                    <div className="split-section-label">Pairing</div>
                    <div className="split-section-content">
                        {!isPairing ? (
                            <div className="split-action-row">
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
                                    <label className="split-timeout-label">
                                        Timeout
                                        <input
                                            id="split-pairing-timeout"
                                            type="number"
                                            min={5} max={120} step={5}
                                            value={pairingTimeout}
                                            onChange={e => setPairingTimeout(Number(e.target.value))}
                                            className="split-timeout-input"
                                        />
                                        <span style={{ fontSize: 11, opacity: 0.5 }}>s</span>
                                    </label>
                                )}
                            </div>
                        ) : (
                            <div className="split-action-row">
                                <div className="split-pairing-indicator">
                                    <span className="split-pairing-dot" />
                                    Pairing in progress…
                                </div>
                                <button className="btn btn-secondary btn-sm" onClick={handleCancelPairing} disabled={!isConnected}>
                                    Cancel
                                </button>
                            </div>
                        )}
                    </div>
                </div>

                {/* Split Configuration Info */}
                <div className="split-section">
                    <div className="split-section-label">Configuration</div>
                    <div className="split-section-content">
                        <p className="split-empty-hint">
                            {isConnectedS
                                ? "Your split halves are connected and synced. Use the buttons above for quick actions."
                                : "No split connection detected. Pair both halves to enable advanced features."
                            }
                        </p>
                    </div>
                </div>

            </div>

            {/* ── Remote Key Test — only in developer mode, only when MASTER + connected ── */}
            {isDeveloperMode && isConnectedS && splitRole === SPLIT_ROLE_MASTER && (
                <div className="split-section split-test-section">
                    <div className="split-section-header-row">
                        <div className="split-section-label" style={{ marginBottom: 0 }}>Remote Key Test</div>
                        <button
                            id="split-test-toggle"
                            className={`btn btn-sm ${testModeActive ? 'btn-danger' : 'btn-secondary'}`}
                            onClick={() => setTestModeActive(!testModeActive)}
                        >
                            {testModeActive ? 'Stop' : 'Start Test'}
                        </button>
                    </div>
                    {testModeActive && (
                        <div className="split-matrix-wrapper">
                            <MatrixVisualiser bitmap={remoteMatrix} />
                        </div>
                    )}
                    {!testModeActive && (
                        <p className="split-empty-hint" style={{ marginTop: '0.5rem' }}>
                            Press Start Test to live-monitor the slave half's key matrix.
                        </p>
                    )}
                </div>
            )}

            <style>{`
                @keyframes split-pulse {
                    0%   { opacity: 1; }
                    50%  { opacity: 0.25; }
                    100% { opacity: 1; }
                }
            `}</style>
        </div>
    );
};

export default SplitDashboard;
