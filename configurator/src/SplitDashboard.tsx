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
    const [benchResult, setBenchResult] = useState<{
        min: number; avg: number; max: number; lost: number; sent: number;
        local_scan_hz: number; local_floor_hz: number; local_peak_hz: number;
        remote_scan_hz: number; remote_floor_hz: number; remote_peak_hz: number;
    } | null>(null);
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
        if (!ok) {
            onLog('Split: Benchmark start failed');
            showNotification('Failed to start benchmark', 'error');
            return;
        }
        setIsBenchmarking(true);
        showNotification('Running benchmark...', 'info');

        /* Sequential poll — waits for each response before scheduling the next,
         * preventing concurrent HID requests that can confuse the transport. */
        const deadline = Date.now() + 8_000;  /* RTT 1s + 2s dwell + margin */
        const poll = async (): Promise<void> => {
            if (Date.now() > deadline) {
                setIsBenchmarking(false);
                showNotification('Benchmark timed out', 'error');
                onLog('Split: Benchmark timed out');
                return;
            }
            const res = await hidService.splitGetBench();
            if (res && !res.active && res.min > 0) {
                const finalResult = {
                    min:             res.min,
                    avg:             res.avg,
                    max:             res.max,
                    lost:            res.lost,
                    sent:            res.sent            ?? 20,
                    local_scan_hz:   res.local_scan_hz   ?? 0,
                    local_floor_hz:  res.local_floor_hz  ?? 0,
                    local_peak_hz:   res.local_peak_hz   ?? 0,
                    remote_scan_hz:  res.remote_scan_hz  ?? 0,
                    remote_floor_hz: res.remote_floor_hz ?? 0,
                    remote_peak_hz:  res.remote_peak_hz  ?? 0,
                };
                setIsBenchmarking(false);
                setBenchResult(finalResult);
                showNotification(<BenchResultCard result={finalResult} />, 'success', 'Benchmark Complete', 20000);
                const rttText = `Min: ${(res.min/1000).toFixed(2)}ms • Avg: ${(res.avg/1000).toFixed(2)}ms • Max: ${(res.max/1000).toFixed(2)}ms`;
                onLog(`Split: Benchmark -> ${rttText} | Lost: ${res.lost}/${res.sent ?? 20}`);
            } else {
                setTimeout(poll, 400);
            }
        };
        setTimeout(poll, 400);
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

            {/* ── Benchmark Results Card ── */}
            {benchResult && (
                <BenchResultCard result={benchResult} />
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

// ── BenchResultCard ───────────────────────────────────────────────────────────

interface BenchResultCardProps {
    result: {
        min: number; avg: number; max: number; lost: number; sent: number;
        local_scan_hz: number; local_floor_hz: number; local_peak_hz: number;
        remote_scan_hz: number; remote_floor_hz: number; remote_peak_hz: number;
    };
}

function BenchResultCard({ result }: BenchResultCardProps) {
    const ms  = (us: number) => (us / 1000).toFixed(2);
    const hz  = (v: number)  => v > 0 ? `${v} Hz` : '—';
    const lostOk = result.lost === 0;

    return (
        <div className="split-bench-card">
            <div className="split-bench-card-header">
                <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                    <polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/>
                </svg>
                <span>Benchmark Results</span>
            </div>

            <div className="split-bench-card-body">

                {/* ── Delay ─────────────────────────────────────────── */}
                <div className="split-bench-section">
                    <div className="split-bench-cat-title">
                        <svg width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5"><circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>
                        Delay (RTT/2)
                    </div>
                    <div className="split-bench-delay-grid">
                        <div className="split-bench-delay-item">
                            <div className="split-bench-delay-label">Min</div>
                            <div className="split-bench-delay-value accent">{ms(result.min)}<span className="split-bench-unit">ms</span></div>
                        </div>
                        <div className="split-bench-delay-item">
                            <div className="split-bench-delay-label">Avg</div>
                            <div className="split-bench-delay-value">{ms(result.avg)}<span className="split-bench-unit">ms</span></div>
                        </div>
                        <div className="split-bench-delay-item">
                            <div className="split-bench-delay-label">Max</div>
                            <div className="split-bench-delay-value">{ms(result.max)}<span className="split-bench-unit">ms</span></div>
                        </div>
                        <div className="split-bench-delay-item">
                            <div className="split-bench-delay-label">Lost</div>
                            <div className={`split-bench-delay-value ${lostOk ? 'ok' : 'warn'}`}>
                                {result.lost}<span className="split-bench-unit">/{result.sent}</span>
                            </div>
                        </div>
                    </div>
                </div>

                <div className="split-bench-divider" />

                {/* ── Polling Rate ───────────────────────────────────── */}
                <div className="split-bench-section">
                    <div className="split-bench-cat-title">
                        <svg width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5"><path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"/></svg>
                        Polling Rate
                    </div>
                    <div className="split-bench-poll-grid">
                        {/* Header row */}
                        <div className="split-bench-poll-header" />
                        <div className="split-bench-poll-header">Master</div>
                        <div className="split-bench-poll-header">Slave</div>
                        {/* Floor row */}
                        <div className="split-bench-poll-rowlabel">Floor</div>
                        <div className="split-bench-poll-cell">{hz(result.local_floor_hz)}</div>
                        <div className="split-bench-poll-cell">{hz(result.remote_floor_hz)}</div>
                        {/* Avg row */}
                        <div className="split-bench-poll-rowlabel">Avg</div>
                        <div className="split-bench-poll-cell accent">{hz(result.local_scan_hz)}</div>
                        <div className="split-bench-poll-cell accent">{hz(result.remote_scan_hz)}</div>
                        {/* Peak row */}
                        <div className="split-bench-poll-rowlabel">Peak</div>
                        <div className="split-bench-poll-cell bright">{hz(result.local_peak_hz)}</div>
                        <div className="split-bench-poll-cell bright">{hz(result.remote_peak_hz)}</div>
                    </div>
                </div>

            </div>
        </div>
    );
}


