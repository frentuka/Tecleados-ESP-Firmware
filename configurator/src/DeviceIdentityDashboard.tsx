import React, { useState, useEffect, useCallback } from 'react';
import { hidService } from './HIDService';
import type { DeviceIdentity } from './services/DeviceController';

// ── Props ──────────────────────────────────────────────────────────────────────

interface DeviceIdentityDashboardProps {
    isConnected: boolean;
    onLog: (text: string) => void;
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

// ── Component ──────────────────────────────────────────────────────────────────

const DeviceIdentityDashboard: React.FC<DeviceIdentityDashboardProps> = ({ isConnected, onLog }) => {
    const [isExpanded, setIsExpanded] = useState(true);
    const [saved, setSaved] = useState<DeviceIdentity>(DEFAULT_IDENTITY);
    const [draft, setDraft] = useState<DeviceIdentity>(DEFAULT_IDENTITY);
    const [isSaving, setIsSaving] = useState(false);
    const [isLoading, setIsLoading] = useState(false);
    const [saveResult, setSaveResult] = useState<'ok' | 'err' | null>(null);

    const isDirty =
        draft.device_name      !== saved.device_name      ||
        draft.is_split          !== saved.is_split           ||
        draft.split_mirror_cols !== saved.split_mirror_cols ||
        draft.split_variant     !== saved.split_variant     ||
        draft.ble_shared_name  !== saved.ble_shared_name   ||
        draft.ble_shared_addr  !== saved.ble_shared_addr;

    // ── Fetch ──────────────────────────────────────────────────────────────

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
        }
    }, [isConnected, onLog]);

    useEffect(() => {
        if (isConnected) fetchIdentity();
        else {
            setSaved(DEFAULT_IDENTITY);
            setDraft(DEFAULT_IDENTITY);
        }
    }, [isConnected, fetchIdentity]);

    // ── Save ──────────────────────────────────────────────────────────────

    const handleSave = async () => {
        if (!isConnected || isSaving) return;
        setIsSaving(true);
        setSaveResult(null);
        const ok = await hidService.saveDeviceIdentity(draft);
        setIsSaving(false);
        setSaveResult(ok ? 'ok' : 'err');
        if (ok) {
            setSaved(draft);
            onLog(`Device Identity: saved (name="${draft.device_name}", ble_name="${draft.ble_shared_name}", split=${draft.is_split})`);
        } else {
            onLog('Device Identity: save failed');
        }
        setTimeout(() => setSaveResult(null), 2000);
    };

    // ── Field helpers ─────────────────────────────────────────────────────

    const setField = <K extends keyof DeviceIdentity>(key: K, value: DeviceIdentity[K]) =>
        setDraft(prev => ({ ...prev, [key]: value }));

    // ── Apply button state ────────────────────────────────────────────────

    const applyClass = `btn btn-sm ${
        saveResult === 'ok' ? 'btn-success' :
        saveResult === 'err' ? 'btn-danger' :
        isDirty ? 'btn-success btn-apply-active' : 'btn-secondary btn-apply-idle'
    }`;

    // ── Render ────────────────────────────────────────────────────────────

    return (
        <div style={{ padding: '0 4px' }}>
            {/* Header */}
            <div
                className="section-title"
                style={{ cursor: 'pointer', display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}
                onClick={() => setIsExpanded(!isExpanded)}
            >
                <span style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                    {/* ID card icon */}
                    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ opacity: 0.7, flexShrink: 0 }}>
                        <rect x="2" y="5" width="20" height="14" rx="2"/>
                        <circle cx="8" cy="12" r="2"/>
                        <path d="M14 9h4M14 12h4M14 15h2"/>
                    </svg>
                    Device Identity
                    {isLoading && (
                        <span style={{ fontSize: 10, fontWeight: 400, opacity: 0.5, marginLeft: 4 }}>loading…</span>
                    )}
                </span>
                <span style={{ opacity: 0.5, fontSize: 14 }}>{isExpanded ? '▲' : '▼'}</span>
            </div>

            {isExpanded && (
                <div style={{ display: 'flex', flexDirection: 'column', gap: 20, padding: '12px 0' }}>

                    {/* ── Device Name ─────────────────────────────────── */}
                    <Section label="Device Name">
                        <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                            <input
                                id="di-device-name"
                                type="text"
                                maxLength={31}
                                value={draft.device_name}
                                onChange={e => setField('device_name', e.target.value)}
                                placeholder="Antigravity KB"
                                style={inputStyle}
                            />
                            <span style={{ fontSize: 11, opacity: 0.4, whiteSpace: 'nowrap' }}>
                                {draft.device_name.length}/31
                            </span>
                        </div>
                        <p style={hintStyle}>
                            Bluetooth and USB device name shown to hosts on pairing.<br/>
                            <span style={{color: 'rgba(230, 200, 100, 0.9)'}}>Note: Name changes take effect after you reconnect or restart the device.</span>
                        </p>
                    </Section>

                    {/* ── Split ──────────────────────────────────────── */}
                    <Section label="Split Keyboard">
                        {/* Is Split toggle */}
                        <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginBottom: 12 }}>
                            <Toggle
                                id="di-is-split"
                                checked={draft.is_split}
                                onChange={v => setField('is_split', v)}
                            />
                            <label htmlFor="di-is-split" style={{ fontSize: 13, cursor: 'pointer', userSelect: 'none' }}>
                                This device is part of a split keyboard
                            </label>
                        </div>

                        {/* Split-specific fields */}
                        <div
                            style={{
                                display: 'grid',
                                gridTemplateColumns: '1fr 1fr',
                                gap: 12,
                                opacity: draft.is_split ? 1 : 0.35,
                                pointerEvents: draft.is_split ? 'auto' : 'none',
                                transition: 'opacity 0.2s',
                            }}
                        >
                            <FieldGroup label="Mirror Columns" hint="Column N maps to (MAX_COL−N). Enable on the mirrored half (e.g. right side).">
                                <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
                                    <Toggle
                                        id="di-mirror-cols"
                                        checked={draft.split_mirror_cols}
                                        onChange={v => setField('split_mirror_cols', v)}
                                    />
                                    <label htmlFor="di-mirror-cols" style={{ fontSize: 13, cursor: 'pointer', userSelect: 'none' }}>
                                        {draft.split_mirror_cols ? 'Enabled' : 'Disabled'}
                                    </label>
                                </div>
                            </FieldGroup>

                            <FieldGroup label="Variant Name" hint={`e.g. "Left", "Right", "Numpad"`}>
                                <input
                                    id="di-split-variant"
                                    type="text"
                                    maxLength={15}
                                    value={draft.split_variant}
                                    onChange={e => setField('split_variant', e.target.value)}
                                    placeholder="Left"
                                    style={inputStyle}
                                />
                            </FieldGroup>
                        </div>
                    </Section>

                    {/* ── BLE Identity (Split) ───────────────────────── */}
                    <Section label="BLE Identity (Split)">
                        <p style={{ ...hintStyle, marginTop: 0, marginBottom: 12 }}>
                            Set these to the same values on both halves so they share one BLE identity.
                            The host will reconnect automatically when roles swap.
                        </p>
                        <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
                            <FieldGroup
                                label="BLE Name"
                                hint="Overrides Device Name in BLE advertisements. Leave blank to use Device Name."
                            >
                                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                                    <input
                                        id="di-ble-shared-name"
                                        type="text"
                                        maxLength={31}
                                        value={draft.ble_shared_name}
                                        onChange={e => setField('ble_shared_name', e.target.value)}
                                        placeholder={draft.device_name || 'Antigravity KB'}
                                        style={inputStyle}
                                    />
                                    <span style={{ fontSize: 11, opacity: 0.4, whiteSpace: 'nowrap' }}>
                                        {draft.ble_shared_name.length}/31
                                    </span>
                                </div>
                            </FieldGroup>

                            <FieldGroup
                                label="BLE MAC Address"
                                hint='Shared static random address base, e.g. "C2:13:57:9B:EF:01". Leave blank for auto. Set bit 7+6 of first byte for Static Random type (e.g. C2:…).'
                            >
                                <input
                                    id="di-ble-shared-addr"
                                    type="text"
                                    maxLength={17}
                                    value={draft.ble_shared_addr}
                                    onChange={e => setField('ble_shared_addr', e.target.value.toUpperCase())}
                                    placeholder="AA:BB:CC:DD:EE:FF"
                                    style={{ ...inputStyle, fontFamily: 'monospace', letterSpacing: 1 }}
                                />
                            </FieldGroup>
                        </div>
                        <p style={{ ...hintStyle, marginTop: 10 }}>
                            Changes take effect after restarting both halves.
                        </p>
                    </Section>

                    {/* ── Apply button ─────────────────────────────────── */}
                    <div style={{ display: 'flex', justifyContent: 'flex-end' }}>
                        <button
                            id="di-apply-btn"
                            className={applyClass}
                            onClick={handleSave}
                            disabled={!isConnected || isSaving || (!isDirty && saveResult === null)}
                        >
                            {isSaving ? 'Saving…' : saveResult === 'ok' ? '✓ Saved' : saveResult === 'err' ? '✗ Error' : 'Apply'}
                        </button>
                    </div>

                </div>
            )}
        </div>
    );
};

// ── Sub-components ────────────────────────────────────────────────────────────

function Section({ label, children }: { label: string; children: React.ReactNode }) {
    return (
        <div>
            <div style={{
                fontSize: 11,
                fontWeight: 600,
                textTransform: 'uppercase',
                opacity: 0.5,
                marginBottom: 10,
                letterSpacing: 1,
            }}>
                {label}
            </div>
            {children}
        </div>
    );
}

function FieldGroup({ label, hint, children }: { label: string; hint?: string; children: React.ReactNode }) {
    return (
        <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
            <span style={{ fontSize: 11, fontWeight: 600, color: 'var(--accent-color)', letterSpacing: 0.5 }}>{label}</span>
            {children}
            {hint && <span style={hintStyle}>{hint}</span>}
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
            style={{
                width: 36,
                height: 20,
                borderRadius: 10,
                background: checked ? 'var(--accent-color)' : 'rgba(255,255,255,0.15)',
                position: 'relative',
                cursor: 'pointer',
                transition: 'background 0.2s',
                flexShrink: 0,
            }}
        >
            <div style={{
                width: 14,
                height: 14,
                borderRadius: '50%',
                background: '#fff',
                position: 'absolute',
                top: 3,
                left: checked ? 19 : 3,
                transition: 'left 0.2s',
                boxShadow: '0 1px 3px rgba(0,0,0,0.4)',
            }} />
        </div>
    );
}

// ── Shared styles ─────────────────────────────────────────────────────────────

const inputStyle: React.CSSProperties = {
    background: 'rgba(255,255,255,0.07)',
    border: '1px solid rgba(255,255,255,0.15)',
    borderRadius: 6,
    color: '#fff',
    padding: '5px 10px',
    fontSize: 13,
    outline: 'none',
    width: '100%',
    transition: 'border-color 0.15s',
};

const hintStyle: React.CSSProperties = {
    fontSize: 11,
    opacity: 0.4,
    marginTop: 3,
    lineHeight: 1.4,
};

export default DeviceIdentityDashboard;
