import { useState } from 'react';
import { createPortal } from 'react-dom';
import type { CustomKey, CustomKeyPR, CustomKeyMA } from './HIDService';
import SearchableKeyModal from './SearchableKeyModal';
import { getKeyName, CKEY_BASE } from './KeyDefinitions';
import type { Macro } from './App';

// ── Props ─────────────────────────────────────────────────────────────────────

interface CustomKeysDashboardProps {
    customKeys: CustomKey[];
    macros: Macro[];
    isDeveloperMode: boolean;
    onSave:   (ckey: CustomKey) => Promise<void>;
    onDelete: (id: number) => Promise<void>;
    onReload: () => void;
}

// ── Default values ─────────────────────────────────────────────────────────────

function makeDefaultPR(): CustomKeyPR {
    return {
        pressAction:     0,
        releaseAction:   0,
        pressDuration:   20,
        releaseDuration: 20,
    };
}

function makeDefaultMA(): CustomKeyMA {
    return {
        tapAction:          0,
        doubleTapAction:    0,
        holdAction:         0,
        doubleTapThreshold: 300,
        holdThreshold:      500,
        tapDuration:        20,
        doubleTapDuration:  20,
        holdDuration:       20,
    };
}

function makeNewCKey(id: number = -1): CustomKey {
    return { id, name: '', mode: 0, pr: makeDefaultPR() };
}

// ── Action slot button ─────────────────────────────────────────────────────────

interface ActionSlotProps {
    label:  string;
    value:  number;
    macros: Macro[];
    onChange: (v: number) => void;
}

function ActionSlot({ label, value, macros, onChange }: ActionSlotProps) {
    const [open, setOpen] = useState(false);
    const displayName = getKeyName(value, macros);

    return (
        <div className="ckey-action-slot">
            <span className="ckey-slot-label">{label}</span>
            <button
                className={`ckey-slot-btn ${value ? 'has-value' : ''}`}
                onClick={() => setOpen(true)}
                title={`Select action for ${label}`}
            >
                <span className={`key-chip ${value ? 'key-chip-active' : ''}`}>
                    {value ? displayName : '(none)'}
                </span>
            </button>
            {open && (
                <SearchableKeyModal
                    currentValue={value}
                    macros={macros}
                    customKeys={[]} 
                    onSelect={v => { onChange(v); setOpen(false); }}
                    onClose={() => setOpen(false)}
                />
            )}
        </div>
    );
}

// ── Preview Components ────────────────────────────────────────────────────────

function CKeyPreviewSequence({ ck, macros }: { ck: CustomKey, macros: Macro[] }) {
    if (ck.mode === 0 && ck.pr) {
        return (
            <div className="ck-preview-seq">
                <div className="ck-preview-item" title="Press">
                    <span className="ck-preview-label">P:</span>
                    <span className="ck-preview-val">{getKeyName(ck.pr.pressAction, macros)}</span>
                </div>
                <div className="ck-preview-item" title="Release">
                    <span className="ck-preview-label">R:</span>
                    <span className="ck-preview-val">{getKeyName(ck.pr.releaseAction, macros)}</span>
                </div>
            </div>
        );
    }
    if (ck.mode === 1 && ck.ma) {
        return (
            <div className="ck-preview-seq">
                 <div className="ck-preview-item" title="Tap">
                    <span className="ck-preview-label">T:</span>
                    <span className="ck-preview-val">{getKeyName(ck.ma.tapAction, macros)}</span>
                </div>
                <div className="ck-preview-item" title="Double Tap">
                    <span className="ck-preview-label">2T:</span>
                    <span className="ck-preview-val">{getKeyName(ck.ma.doubleTapAction, macros)}</span>
                </div>
                <div className="ck-preview-item" title="Hold">
                    <span className="ck-preview-label">H:</span>
                    <span className="ck-preview-val">{getKeyName(ck.ma.holdAction, macros)}</span>
                </div>
            </div>
        );
    }
    return null;
}

function CKeyCard({ ck, isSelected, onClick, macros, isDeveloperMode }: { 
    ck: CustomKey, 
    isSelected: boolean, 
    onClick: () => void, 
    macros: Macro[],
    isDeveloperMode: boolean
}) {
    return (
        <div 
            className={`ckey-card glass-panel ${isSelected ? 'ckey-card-selected' : ''}`}
            onClick={onClick}
        >
            <div className="ckey-card-header">
                <div className="ckey-card-title-row">
                    <h4>{ck.name || `CK[${ck.id}]`}</h4>
                    <span className={`ckey-card-badge ${ck.mode === 0 ? 'badge-pr' : 'badge-ma'}`}>
                        {ck.mode === 0 ? 'PR' : 'MA'}
                    </span>
                </div>
                {isDeveloperMode && (
                    <div className="ckey-card-id-dev">
                        ID: 0x{(CKEY_BASE + ck.id).toString(16).toUpperCase()}
                    </div>
                )}
            </div>
            <div className="ckey-card-body">
                <CKeyPreviewSequence ck={ck} macros={macros} />
            </div>
        </div>
    );
}

// ── Editor Modal ─────────────────────────────────────────────────────────────

interface CKeyEditorModalProps {
    ckey: CustomKey;
    macros: Macro[];
    isSaving: boolean;
    error: string | null;
    onSave: (ckey: CustomKey) => void;
    onDelete: (id: number) => void;
    onClose: () => void;
}

function CKeyEditorModal({ ckey, macros, isSaving, error, onSave, onDelete, onClose }: CKeyEditorModalProps) {
    const [local, setLocal] = useState<CustomKey>({ ...ckey });

    const handleField = (f: string, v: any) => setLocal(prev => ({ ...prev, [f]: v }));
    const handlePR = (f: string, v: any) => setLocal(prev => ({
        ...prev, pr: { ...(prev.pr || makeDefaultPR()), [f]: v }
    }));
    const handleMA = (f: string, v: any) => setLocal(prev => ({
        ...prev, ma: { ...(prev.ma || makeDefaultMA()), [f]: v }
    }));

    return createPortal(
        <div className="modal-overlay" onClick={onClose}>
            <div className="modal-content ckey-editor-modal" onClick={e => e.stopPropagation()}>
                <div className="modal-header">
                    <h3 className="ckey-editor-title">
                        {local.id < 32 ? `Edit Custom Key [ID: ${local.id}]` : 'New Custom Key'}
                    </h3>
                    <button className="btn-close" onClick={onClose}>×</button>
                </div>

                <div className="modal-body">
                    {error && <div className="ckey-error">{error}</div>}

                    <div className="ckey-field">
                        <label className="ckey-field-label">Name</label>
                        <input
                            className="ckey-field-input"
                            value={local.name}
                            onChange={e => handleField('name', e.target.value)}
                            placeholder="Enter a descriptive name..."
                        />
                    </div>

                    <div className="ckey-field">
                        <label className="ckey-field-label">Mode</label>
                        <div className="ckey-mode-selector">
                            <button
                                className={`ckey-mode-btn ${local.mode === 0 ? 'active' : ''}`}
                                onClick={() => handleField('mode', 0)}
                            >
                                <span className="mode-icon">⇄</span> Press/Release
                            </button>
                            <button
                                className={`ckey-mode-btn ${local.mode === 1 ? 'active' : ''}`}
                                onClick={() => handleField('mode', 1)}
                            >
                                <span className="mode-icon">⟳</span> Multi-Action
                            </button>
                        </div>
                    </div>

                    {local.mode === 0 && (
                        <div className="ckey-section">
                            <h4 className="ckey-section-title">Actions</h4>
                            <div className="ckey-action-grid">
                                <ActionSlot
                                    label="On Press"
                                    value={local.pr?.pressAction || 0}
                                    macros={macros}
                                    onChange={v => handlePR('pressAction', v)}
                                />
                                <div className="ckey-duration-row">
                                    <label>Action Duration (ms)</label>
                                    <input
                                        type="number"
                                        value={local.pr?.pressDuration || 20}
                                        onChange={e => handlePR('pressDuration', parseInt(e.target.value) || 0)}
                                    />
                                </div>
                                <ActionSlot
                                    label="On Release"
                                    value={local.pr?.releaseAction || 0}
                                    macros={macros}
                                    onChange={v => handlePR('releaseAction', v)}
                                />
                                <div className="ckey-duration-row">
                                    <label>Action Duration (ms)</label>
                                    <input
                                        type="number"
                                        value={local.pr?.releaseDuration || 20}
                                        onChange={e => handlePR('releaseDuration', parseInt(e.target.value) || 0)}
                                    />
                                </div>
                            </div>
                        </div>
                    )}

                    {local.mode === 1 && (
                        <div className="ckey-section">
                            <h4 className="ckey-section-title">Actions</h4>
                            <div className="ckey-action-grid">
                                <ActionSlot
                                    label="Single Tap"
                                    value={local.ma?.tapAction || 0}
                                    macros={macros}
                                    onChange={v => handleMA('tapAction', v)}
                                />
                                <ActionSlot
                                    label="Double Tap"
                                    value={local.ma?.doubleTapAction || 0}
                                    macros={macros}
                                    onChange={v => handleMA('doubleTapAction', v)}
                                />
                                <ActionSlot
                                    label="Hold"
                                    value={local.ma?.holdAction || 0}
                                    macros={macros}
                                    onChange={v => handleMA('holdAction', v)}
                                />
                            </div>

                            <h4 className="ckey-section-title" style={{ marginTop: '1.25rem' }}>Thresholds</h4>
                            <div className="ckey-threshold-grid">
                                <div className="ckey-duration-row">
                                    <label>Double Tap Window (ms)</label>
                                    <input
                                        type="number"
                                        value={local.ma?.doubleTapThreshold || 300}
                                        onChange={e => handleMA('doubleTapThreshold', parseInt(e.target.value) || 0)}
                                    />
                                </div>
                                <div className="ckey-duration-row">
                                    <label>Hold Threshold (ms)</label>
                                    <input
                                        type="number"
                                        value={local.ma?.holdThreshold || 500}
                                        onChange={e => handleMA('holdThreshold', parseInt(e.target.value) || 0)}
                                    />
                                </div>
                            </div>
                        </div>
                    )}
                </div>

                <div className="modal-footer">
                    <div style={{ marginRight: 'auto' }}>
                        <button
                            className="btn btn-danger"
                            onClick={() => { if (confirm('Delete this custom key?')) { onDelete(local.id); onClose(); } }}
                            disabled={isSaving}
                        >
                            Delete
                        </button>
                    </div>
                    <button className="btn" onClick={onClose} disabled={isSaving}>Cancel</button>
                    <button
                        className="btn btn-success"
                        onClick={() => onSave(local)}
                        disabled={isSaving}
                    >
                        {isSaving ? 'Saving...' : 'Save to Device'}
                    </button>
                </div>
            </div>
        </div>,
        document.body
    );
}

// ── Main Dashboard ─────────────────────────────────────────────────────────────

const CKEY_MAX = 32;

export default function CustomKeysDashboard({ customKeys, macros, isDeveloperMode, onSave, onDelete, onReload }: CustomKeysDashboardProps) {
    const [selected, setSelected] = useState<CustomKey | null>(null);
    const [isSaving, setIsSaving] = useState(false);
    const [error, setError] = useState<string | null>(null);

    const handleNew = () => {
        let firstAvailable = -1;
        for (let i = 0; i < CKEY_MAX; i++) {
            if (!customKeys.find(k => k.id === i)) {
                firstAvailable = i;
                break;
            }
        }
        
        if (firstAvailable === -1) {
            setError(`Maximum number of custom keys reached (${CKEY_MAX}).`);
            return;
        }

        setSelected(makeNewCKey(firstAvailable));
        setError(null);
    };

    const handleSave = async (ckey: CustomKey) => {
        if (!ckey.name.trim()) { setError('Please enter a name for the custom key.'); return; }
        setIsSaving(true);
        setError(null);
        try {
            await onSave(ckey);
            setSelected(null);
        } catch (e: unknown) {
            setError(e instanceof Error ? e.message : 'Save failed');
        } finally {
            setIsSaving(false);
        }
    };

    const handleDeleteKey = async (id: number) => {
        setIsSaving(true);
        try {
            await onDelete(id);
            setSelected(null);
        } catch (e: unknown) {
            setError(e instanceof Error ? e.message : 'Delete failed');
        } finally {
            setIsSaving(false);
        }
    };

    const sortedKeys = [...customKeys].sort((a, b) => a.id - b.id);

    return (
        <div className="ckey-dashboard">
            <div className="ckey-dashboard-header">
                <h2 className="section-title">Custom Keys</h2>
                <div style={{ display: 'flex', gap: '0.5rem', alignItems: 'center' }}>
                    <span className="ckey-count-badge">{customKeys.length} / {CKEY_MAX}</span>
                    <button id="ckey-reload-btn" className="btn" onClick={onReload} title="Reload from device">↺ Reload</button>
                    <button id="ckey-new-top-btn" className="btn btn-success" onClick={handleNew} disabled={customKeys.length >= CKEY_MAX}>+ New</button>
                </div>
            </div>

            <div className="ckey-list-full">
                <div className="macro-cards-grid">
                    {sortedKeys.length === 0 ? (
                        <div className="empty-state">No custom keys defined yet.</div>
                    ) : (
                        sortedKeys.map(ck => (
                            <CKeyCard 
                                key={ck.id} 
                                ck={ck} 
                                isSelected={selected?.id === ck.id}
                                onClick={() => setSelected(ck)}
                                macros={macros}
                                isDeveloperMode={isDeveloperMode}
                            />
                        ))
                    )}
                </div>
            </div>

            {selected && (
                <CKeyEditorModal
                    ckey={selected}
                    macros={macros}
                    isSaving={isSaving}
                    error={error}
                    onSave={handleSave}
                    onDelete={handleDeleteKey}
                    onClose={() => setSelected(null)}
                />
            )}
        </div>
    );
}
