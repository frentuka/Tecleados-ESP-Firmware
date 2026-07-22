import React, { useState, useEffect, useCallback } from 'react';
import { hidService } from './HIDService';
import type { DeviceIdentity } from './services/DeviceController';
import { useNotificationStore } from './stores/notificationStore';
import { withTimeout, TimeoutError } from './utils/withTimeout';
import './assets/css/device-identity.css';

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
    transparent_stack_fallback: false,
};

// ── Component ──────────────────────────────────────────────────────────────────

const DeviceIdentityDashboard: React.FC<DeviceIdentityDashboardProps> = ({ isConnected, onLog }) => {
    const { showNotification } = useNotificationStore();
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
        draft.ble_shared_addr  !== saved.ble_shared_addr   ||
        draft.transparent_stack_fallback !== saved.transparent_stack_fallback;

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

    // ── Save ──────────────────────────────────────────────────────────────

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
        <div className="identity-page">

            {/* ── Page header ── */}
            <div className="identity-page-header">
                <div className="identity-page-header-left">
                    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ opacity: 0.6 }}>
                        <rect x="2" y="5" width="20" height="14" rx="2"/>
                        <circle cx="8" cy="12" r="2"/>
                        <path d="M14 9h4M14 12h4M14 15h2"/>
                    </svg>
                    <span className="identity-page-title">Device Identity</span>
                    {isLoading && <span className="identity-loading-hint">loading…</span>}
                </div>
                <button
                    id="di-apply-btn"
                    className={applyClass}
                    onClick={handleSave}
                    disabled={!isConnected || isSaving || (!isDirty && saveResult === null)}
                >
                    {isSaving ? 'Saving…' : saveResult === 'ok' ? '✓ Saved' : saveResult === 'err' ? '✗ Error' : 'Apply'}
                </button>
            </div>

            {/* ── Sections ── */}
            <div className="identity-sections">

                {/* ── Device Name ─────────────────────────────────── */}
                <IdentitySection label="Device Name">
                    <div className="identity-field-row">
                        <input
                            id="di-device-name"
                            type="text"
                            maxLength={31}
                            value={draft.device_name}
                            onChange={e => setField('device_name', e.target.value)}
                            placeholder="Tecleados MK1"
                            className="identity-input"
                        />
                        <span className="identity-char-count">{draft.device_name.length}/31</span>
                    </div>
                    <p className="identity-hint">
                        Bluetooth and USB device name shown to hosts on pairing.{' '}
                        <span className="identity-hint-warn">Name changes take effect after reconnect or restart.</span>
                    </p>
                </IdentitySection>

                {/* ── Split Keyboard ──────────────────────────────── */}
                <IdentitySection label="Split Keyboard">
                    <div className="identity-toggle-row">
                        <Toggle
                            id="di-is-split"
                            checked={draft.is_split}
                            onChange={v => setField('is_split', v)}
                        />
                        <label htmlFor="di-is-split" className="identity-toggle-label">
                            This device is part of a split keyboard
                        </label>
                    </div>

                    <div className={`identity-split-fields ${draft.is_split ? '' : 'disabled'}`}>
                        <FieldGroup label="Mirror Columns" hint="Column N maps to (MAX_COL−N). Enable on the mirrored half (e.g. right side).">
                            <div className="identity-toggle-row" style={{ marginTop: 4 }}>
                                <Toggle
                                    id="di-mirror-cols"
                                    checked={draft.split_mirror_cols}
                                    onChange={v => setField('split_mirror_cols', v)}
                                />
                                <label htmlFor="di-mirror-cols" className="identity-toggle-label">
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
                                className="identity-input"
                            />
                        </FieldGroup>
                    </div>
                </IdentitySection>

                {/* ── BLE Identity (Split) ───────────────────────── */}
                <IdentitySection label="BLE Identity (Split)">
                    <p className="identity-hint" style={{ marginTop: 0, marginBottom: 16 }}>
                        Set these to the same values on both halves so they share one BLE identity.
                        The host will reconnect automatically when roles swap.
                    </p>
                    <div className="identity-ble-fields">
                        <FieldGroup
                            label="BLE Name"
                            hint="Overrides Device Name in BLE advertisements. Leave blank to use Device Name."
                        >
                            <div className="identity-field-row">
                                <input
                                    id="di-ble-shared-name"
                                    type="text"
                                    maxLength={31}
                                    value={draft.ble_shared_name}
                                    onChange={e => setField('ble_shared_name', e.target.value)}
                                    placeholder={draft.device_name || 'Tecleados MK1'}
                                    className="identity-input"
                                />
                                <span className="identity-char-count">{draft.ble_shared_name.length}/31</span>
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
                                className="identity-input identity-input-mono"
                            />
                        </FieldGroup>
                    </div>
                    <p className="identity-hint" style={{ marginTop: 12 }}>
                        Changes take effect after restarting both halves.
                    </p>
                </IdentitySection>

            </div>
        </div>
    );
};

// ── Sub-components ────────────────────────────────────────────────────────────

function IdentitySection({ label, children }: { label: string; children: React.ReactNode }) {
    return (
        <div className="identity-section">
            <div className="identity-section-label">{label}</div>
            <div className="identity-section-body">{children}</div>
        </div>
    );
}

function FieldGroup({ label, hint, children }: { label: string; hint?: string; children: React.ReactNode }) {
    return (
        <div className="identity-field-group">
            <span className="identity-field-label">{label}</span>
            {children}
            {hint && <span className="identity-hint">{hint}</span>}
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
            className={`identity-toggle ${checked ? 'active' : ''}`}
        >
            <div className="identity-toggle-thumb" />
        </div>
    );
}

export default DeviceIdentityDashboard;
