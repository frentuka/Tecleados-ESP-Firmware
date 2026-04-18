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
        case SPLIT_STATE_CONNECTED:    return '#2ecc71';
        case SPLIT_STATE_PAIRING:
        case SPLIT_STATE_CONNECTING:   return '#f39c12';
        case SPLIT_STATE_DISCONNECTED: return '#e74c3c';
        default:                       return 'rgba(255,255,255,0.4)';
    }
}

// ── Test mode: keyboard visualiser ───────────────────────────────────────────

const MATRIX_ROWS = 6;
const MATRIX_COLS = 18;

function MatrixVisualiser({ bitmap }: { bitmap: Uint8Array | null }) {
    if (!bitmap) return <div style={{ color: 'rgba(255,255,255,0.4)', fontSize: 12 }}>No data</div>;

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
                    style={{
                        width: 18,
                        height: 18,
                        borderRadius: 3,
                        background: pressed ? '#2ecc71' : 'rgba(255,255,255,0.08)',
                        border: '1px solid rgba(255,255,255,0.12)',
                        transition: 'background 0.1s',
                    }}
                />
            );
        }
    }

    return (
        <div style={{ display: 'grid', gridTemplateColumns: `repeat(${MATRIX_COLS}, 18px)`, gap: 3 }}>
            {cells}
        </div>
    );
}

// ── Props ─────────────────────────────────────────────────────────────────────

interface SplitDashboardProps {
    isConnected: boolean;
    deviceStatus: DeviceStatus | null;
    onLog: (text: string) => void;
}

// ── Component ─────────────────────────────────────────────────────────────────

const SplitDashboard: React.FC<SplitDashboardProps> = ({ isConnected, deviceStatus, onLog }) => {
    const [isExpanded, setIsExpanded] = useState(true);
    const [pairingTimeout, setPairingTimeout] = useState(30);
    const [testModeActive, setTestModeActive] = useState(false);
    const [remoteMatrix, setRemoteMatrix] = useState<Uint8Array | null>(null);
    const [benchStatus, setBenchStatus] = useState<string | null>(null);
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
        const ok = await hidService.splitStartPairing(pairingTimeout * 1000);
        onLog(ok ? `Split: pairing started (${pairingTimeout}s timeout)` : 'Split: start pairing failed');
    }, [pairingTimeout, onLog]);

    const handleCancelPairing = useCallback(async () => {
        const ok = await hidService.splitCancelPairing();
        onLog(ok ? 'Split: pairing cancelled' : 'Split: cancel pairing failed');
    }, [onLog]);

    const handleUnpair = useCallback(async () => {
        const ok = await hidService.splitUnpair();
        onLog(ok ? 'Split: unpaired' : 'Split: unpair failed');
    }, [onLog]);

    const handleRoleSwap = useCallback(async () => {
        const ok = await hidService.splitRoleSwap();
        onLog(ok ? 'Split: role swap requested' : 'Split: role swap failed');
    }, [onLog]);

    const handleRunBenchmark = useCallback(async () => {
        const ok = await hidService.splitRunBenchmark();
        if (ok) {
            setBenchStatus('Running…');
            const bRef = setInterval(async () => {
                const res = await hidService.splitGetBench();
                if (res && (!res.active && res.min > 0)) {
                    clearInterval(bRef);
                    setBenchStatus(`min=${(res.min/1000).toFixed(2)}ms avg=${(res.avg/1000).toFixed(2)}ms max=${(res.max/1000).toFixed(2)}ms lost=${res.lost}`);
                    setTimeout(() => setBenchStatus(null), 10000);
                }
            }, 500);
        } else {
            onLog('Split: Benchmark start failed');
        }
    }, [onLog]);

    // ── Render ────────────────────────────────────────────────────────────

    const statusDot = (
        <span style={{
            display: 'inline-block',
            width: 8,
            height: 8,
            borderRadius: '50%',
            background: stateColor(splitState),
            boxShadow: `0 0 6px ${stateColor(splitState)}`,
            animation: isPairing ? 'split-pulse 1.2s infinite ease-in-out' : 'none',
        }} />
    );

    return (
        <div style={{ 
            background: 'rgba(25, 25, 25, 0.4)',
            border: '1px solid rgba(255, 255, 255, 0.05)',
            borderRadius: 10,
            overflow: 'hidden',
            marginBottom: 16
        }}>
            {/* Header */}
            <div
                style={{ 
                    cursor: 'pointer', 
                    display: 'flex', 
                    alignItems: 'center', 
                    justifyContent: 'space-between',
                    padding: '16px 20px',
                    background: isExpanded ? 'rgba(255, 255, 255, 0.02)' : 'transparent',
                    borderBottom: isExpanded ? '1px solid rgba(255, 255, 255, 0.05)' : 'none',
                    transition: 'background 0.2s'
                }}
                onClick={() => setIsExpanded(!isExpanded)}
            >
                <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
                    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ opacity: 0.7 }}>
                        <path d="M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71"></path>
                        <path d="M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71"></path>
                    </svg>
                    <span style={{ fontWeight: 600, fontSize: 15, letterSpacing: 0.3 }}>Split Keyboard</span>
                    <div style={{ display: 'flex', alignItems: 'center', gap: 6, opacity: 0.8 }}>
                        {statusDot}
                        <span style={{ fontSize: 12 }}>{stateLabel(splitState)}</span>
                        {splitRole !== SPLIT_ROLE_NONE && <span style={{ fontSize: 12, opacity: 0.5 }}>· {roleLabel(splitRole)}</span>}
                    </div>
                </div>
                <span style={{ opacity: 0.5, fontSize: 14 }}>{isExpanded ? '▲' : '▼'}</span>
            </div>

            {isExpanded && (
                <div style={{ display: 'flex', flexDirection: 'column', gap: 20, padding: 20 }}>

                    <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(200px, 1fr))', gap: 16 }}>
                        
                        {/* Commands Section */}
                        <div style={{
                            background: 'rgba(0, 0, 0, 0.2)',
                            borderRadius: 8,
                            padding: 16,
                            display: 'flex',
                            flexDirection: 'column',
                            gap: 16
                        }}>
                            <div style={{ fontSize: 11, fontWeight: 600, textTransform: 'uppercase', letterSpacing: 1, color: '#aaa' }}>
                                Split Actions
                            </div>
                            <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap' }}>
                                {isConnectedS ? (
                                    <>
                                        <button className="btn btn-primary btn-sm" onClick={handleRoleSwap} disabled={!isConnected}>
                                            🔄 Switch Roles
                                        </button>
                                        {splitRole === SPLIT_ROLE_MASTER && (
                                            <button className="btn btn-secondary btn-sm" onClick={handleRunBenchmark} disabled={!isConnected}>
                                                🏎 Benchmark {benchStatus ? `(${benchStatus})` : ''}
                                            </button>
                                        )}
                                    </>
                                ) : (
                                    <span style={{ fontSize: 12, color: 'rgba(255,255,255,0.4)' }}>No actions available (unpaired/idle)</span>
                                )}
                            </div>
                        </div>

                        {/* Pairing Section */}
                        <div style={{
                            background: 'rgba(0, 0, 0, 0.2)',
                            borderRadius: 8,
                            padding: 16,
                            display: 'flex',
                            flexDirection: 'column',
                            gap: 16
                        }}>
                            <div style={{ fontSize: 11, fontWeight: 600, textTransform: 'uppercase', letterSpacing: 1, color: '#aaa' }}>
                                Pairing Controls
                            </div>
                            <div style={{ display: 'flex', gap: 8, alignItems: 'center', flexWrap: 'wrap' }}>
                                {!isPairing ? (
                                    <>
                                        <button className="btn btn-success btn-sm" onClick={handleStartPairing} disabled={!isConnected}>
                                            Start Pairing
                                        </button>
                                        {isConnectedS && (
                                            <button className="btn btn-danger btn-sm" onClick={handleUnpair} disabled={!isConnected}>
                                                Unpair
                                            </button>
                                        )}
                                        <label style={{ display: 'flex', alignItems: 'center', gap: 6, fontSize: 12, opacity: 0.7 }}>
                                            Timeout
                                            <input
                                                type="number"
                                                min={5} max={120} step={5}
                                                value={pairingTimeout}
                                                onChange={e => setPairingTimeout(Number(e.target.value))}
                                                style={{ width: 52, background: 'rgba(255,255,255,0.07)', border: '1px solid rgba(255,255,255,0.15)', borderRadius: 4, color: '#fff', padding: '2px 6px', fontSize: 12 }}
                                            />
                                            s
                                        </label>
                                    </>
                                ) : (
                                    <button className="btn btn-secondary btn-sm" onClick={handleCancelPairing} disabled={!isConnected}>
                                        Cancel Pairing
                                    </button>
                                )}
                            </div>
                        </div>

                    </div>

                    {/* Test mode — only shown when MASTER + connected */}
                    {isConnectedS && splitRole === SPLIT_ROLE_MASTER && (
                        <div style={{ background: 'rgba(0, 0, 0, 0.2)', borderRadius: 8, padding: 16 }}>
                            <div style={{ display: 'flex', alignItems: 'center', gap: 12, marginBottom: 12 }}>
                                <div style={{ fontSize: 11, fontWeight: 600, textTransform: 'uppercase', color: '#aaa', letterSpacing: 1 }}>
                                    Remote Key Test
                                </div>
                                <button className={`btn btn-sm ${testModeActive ? 'btn-danger' : 'btn-secondary'}`} onClick={() => setTestModeActive(!testModeActive)}>
                                    {testModeActive ? 'Stop Test' : 'Start Test'}
                                </button>
                            </div>
                            {testModeActive && (
                                <div style={{ overflowX: 'auto', paddingBottom: 4 }}>
                                    <MatrixVisualiser bitmap={remoteMatrix} />
                                </div>
                            )}
                        </div>
                    )}

                </div>
            )}

            <style>{`
                @keyframes split-pulse {
                    0%   { opacity: 1; }
                    50%  { opacity: 0.3; }
                    100% { opacity: 1; }
                }
            `}</style>
        </div>
    );
};

export default SplitDashboard;
