import { useState, useRef, useEffect } from 'react';
import { createPortal } from 'react-dom';
import type { Combo } from './types/combos';
import type { Macro } from './types/macros';
import SearchableKeyModal from './components/SearchableKeyModal';
import ComboKeySelector from './components/ComboKeySelector';
import { getKeyName } from './KeyDefinitions';
import { saveJsonFile } from './utils/fileUtils';
import { useNotificationStore } from './stores/notificationStore';
import './assets/css/custom-keys-dashboard.css'; // Reusing CSS from custom keys for general layout

interface CombosDashboardProps {
    combos: Combo[];
    macros: Macro[];
    isDeveloperMode: boolean;
    onSave: (combo: Combo) => Promise<void>;
    onDelete: (id: number) => Promise<void>;
    onReload?: () => void;
}

const LAYER_NAMES = ['Base', 'FN1', 'FN2', 'FN3'];
const COMBO_MAX = 32;

function ComboCard({ combo, isSelected, onClick, onDelete, macros, isDeveloperMode }: {
    combo: Combo,
    isSelected: boolean,
    onClick: () => void,
    onDelete: (id: number) => void,
    macros: Macro[],
    isDeveloperMode: boolean
}) {
    return (
        <div
            className={`macro-card ${isSelected ? 'ckey-card-selected' : ''}`}
            onClick={onClick}
        >
            <div className="macro-card-content-wrapper" style={{ padding: '0.75rem' }}>
                <div className="macro-card-header">
                    <h4>{combo.name || `Combo[${combo.id}]`}</h4>
                </div>
                <div className="macro-card-body">
                    <div style={{ fontSize: '0.85rem', color: 'var(--text-secondary)' }}>
                        <strong>Trigger:</strong> {combo.keys.length} keys {combo.strictOrder ? '(Ordered)' : ''}
                    </div>
                    <div style={{ fontSize: '0.85rem', color: 'var(--text-secondary)' }}>
                        <strong>Action:</strong> {combo.action ? getKeyName(combo.action, macros) : '(none)'}
                    </div>
                    <div style={{ fontSize: '0.8rem', color: 'var(--text-muted)', marginTop: '0.25rem' }}>
                        Layers: {combo.activeLayers.length === 0 ? 'None' : combo.activeLayers.map(l => LAYER_NAMES[l]).join(', ')}
                    </div>
                </div>
            </div>

            <div className="macro-card-actions" onClick={e => e.stopPropagation()}>
                {isDeveloperMode && (
                    <div className="macro-id" style={{ marginRight: '0.5rem', opacity: 0.6, fontSize: '0.75rem' }}>
                        ID: {combo.id}
                    </div>
                )}
                <button
                    className="btn-icon btn-danger"
                    title="Delete"
                    onClick={e => { e.stopPropagation(); onDelete(combo.id); }}
                >
                    <svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ verticalAlign: 'middle' }}>
                        <polyline points="3 6 5 6 21 6"></polyline>
                        <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"></path>
                    </svg>
                </button>
            </div>
        </div>
    );
}

const ComboNameInput = ({ initialName, onChange }: { initialName: string, onChange: (val: string) => void }) => {
    const [localName, setLocalName] = useState(initialName);
    return (
        <input
            type="text"
            value={localName}
            onChange={e => setLocalName(e.target.value)}
            onBlur={() => onChange(localName)}
            onKeyDown={e => { if (e.key === 'Enter') { e.currentTarget.blur(); } }}
            placeholder="Combo Name..."
            className="macro-name-input-compact"
        />
    );
};

function ComboEditorModal({ combo, macros, isSaving, error, onSave, onDelete, onClose, isDeveloperMode }: {
    combo: Combo;
    macros: Macro[];
    isSaving: boolean;
    error: string | null;
    onSave: (c: Combo) => void;
    onDelete: (id: number) => void;
    onClose: () => void;
    isDeveloperMode: boolean;
}) {
    const [local, setLocal] = useState<Combo>({ ...combo });
    const [actionSelectorOpen, setActionSelectorOpen] = useState(false);

    const toggleLayer = (layerIdx: number) => {
        setLocal(prev => {
            const next = new Set(prev.activeLayers);
            if (next.has(layerIdx)) next.delete(layerIdx);
            else next.add(layerIdx);
            return { ...prev, activeLayers: Array.from(next).sort() };
        });
    };

    return createPortal(
        <div className="modal-overlay" onClick={onClose}>
            <div className="modal-content ckey-editor-modal" style={{ maxWidth: '800px', width: '90%' }} onClick={e => e.stopPropagation()}>
                <div className="modal-header macro-modal-header">
                    <div className="macro-name-container">
                        <ComboNameInput 
                            initialName={local.name || (local.id >= 0 && local.id < COMBO_MAX ? `Combo #${local.id}` : '')} 
                            onChange={val => setLocal({ ...local, name: val })} 
                        />
                    </div>
                    {isDeveloperMode && (
                        <div className="macro-editor-actions-header">
                            <span style={{color: 'var(--text-muted)', fontSize: '0.8rem', marginRight: '1rem'}}>
                                ID: {local.id < COMBO_MAX ? local.id : 'New'}
                            </span>
                        </div>
                    )}
                    <button className="btn-close" onClick={onClose}>×</button>
                </div>

                <div className="modal-body" style={{ display: 'flex', flexDirection: 'column', gap: '1rem' }}>
                    {error && <div className="ckey-error">{error}</div>}

                    <div className="ckey-field" style={{ margin: 0 }}>
                        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '0.5rem' }}>
                            <label className="ckey-field-label" style={{ margin: 0 }}>Trigger Keys ({local.keys.length}/8)</label>
                            <label className="ckey-field-label" style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', cursor: 'pointer', margin: 0 }}>
                                <input 
                                    type="checkbox" 
                                    checked={local.strictOrder}
                                    onChange={e => setLocal({...local, strictOrder: e.target.checked})}
                                />
                                Strict Order
                            </label>
                        </div>
                        <div style={{ fontSize: '0.8rem', color: 'var(--text-secondary)', fontStyle: 'italic', marginBottom: '0.5rem' }}>
                            Select the keys on the layout below. {local.strictOrder && 'The numbers indicate the required order.'}
                        </div>
                        <div style={{ border: '1px solid var(--border-color)', borderRadius: '8px', padding: '0.5rem', background: 'var(--bg-color)' }}>
                            <ComboKeySelector 
                                selectedKeys={local.keys}
                                onChange={keys => setLocal({ ...local, keys })}
                                strictOrder={local.strictOrder}
                            />
                        </div>
                    </div>

                    <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '1rem' }}>
                        <div className="ckey-field" style={{ margin: 0 }}>
                            <label className="ckey-field-label">Action</label>
                            <button
                                className={`ckey-slot-btn ${local.action ? 'has-value' : ''}`}
                                onClick={() => setActionSelectorOpen(true)}
                                style={{ width: '200px', justifyContent: 'flex-start' }}
                            >
                                <span className={`key-chip ${local.action ? 'key-chip-active' : ''}`}>
                                    {local.action ? getKeyName(local.action, macros) : 'Select Action...'}
                                </span>
                            </button>
                            {actionSelectorOpen && (
                                <SearchableKeyModal
                                    currentValue={local.action}
                                    macros={macros}
                                    onSelect={v => { setLocal({ ...local, action: v }); setActionSelectorOpen(false); }}
                                    onClose={() => setActionSelectorOpen(false)}
                                />
                            )}
                        </div>

                        <div className="ckey-field" style={{ margin: 0 }}>
                            <label className="ckey-field-label">Active Layers</label>
                            <div style={{ display: 'flex', gap: '0.5rem', flexWrap: 'wrap' }}>
                                {LAYER_NAMES.map((name, i) => (
                                    <button
                                        key={i}
                                        className={`ckey-mode-btn ${local.activeLayers.includes(i) ? 'active' : ''}`}
                                        onClick={() => toggleLayer(i)}
                                        style={{ flex: 1, padding: '0.5rem' }}
                                    >
                                        {name}
                                    </button>
                                ))}
                            </div>
                        </div>
                    </div>
                </div>

                <div className="modal-footer">
                    <div style={{ marginRight: 'auto' }}>
                        <button
                            className="btn btn-danger"
                            onClick={() => { onDelete(local.id); onClose(); }}
                            disabled={isSaving || local.id === -1}
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
                        {isSaving ? 'Saving...' : 'Save'}
                    </button>
                </div>
            </div>
        </div>,
        document.body
    );
}

export default function CombosDashboard({ combos, macros, isDeveloperMode, onSave, onDelete, onReload }: CombosDashboardProps) {
    const { showNotification } = useNotificationStore();
    const [selected, setSelected] = useState<Combo | null>(null);
    const [isSaving, setIsSaving] = useState(false);
    const [error, setError] = useState<string | null>(null);
    const [isMenuOpen, setIsMenuOpen] = useState(false);

    const menuRef = useRef<HTMLDivElement>(null);
    const fileInputRef = useRef<HTMLInputElement>(null);

    useEffect(() => {
        function handleClickOutside(event: MouseEvent) {
            if (menuRef.current && !menuRef.current.contains(event.target as Node)) {
                setIsMenuOpen(false);
            }
        }
        document.addEventListener('mousedown', handleClickOutside);
        return () => document.removeEventListener('mousedown', handleClickOutside);
    }, []);

    const handleExport = async () => {
        try {
            const dataStr = JSON.stringify(combos, null, 2);
            await saveJsonFile(dataStr, 'combos_export.json');
            showNotification("Combos exported successfully", "success");
        } catch (err) {
            showNotification("Failed to export combos.", "error");
        }
        setIsMenuOpen(false);
    };

    const handleImport = async (event: React.ChangeEvent<HTMLInputElement>) => {
        const file = event.target.files?.[0];
        if (!file) return;

        const reader = new FileReader();
        reader.onload = async (e) => {
            try {
                const importedData = JSON.parse(e.target?.result as string);
                const parsedKeys = Array.isArray(importedData) ? importedData : [importedData];

                for (const c of parsedKeys) {
                    await onSave(c);
                }
                showNotification("Combos imported successfully", "success");
            } catch (error) {
                showNotification("Failed to import combos.", "error");
            }
        };
        reader.readAsText(file);
        event.target.value = '';
        setIsMenuOpen(false);
    };

    const handleNew = () => {
        let firstAvailable = -1;
        for (let i = 0; i < COMBO_MAX; i++) {
            if (!combos.find(k => k.id === i)) {
                firstAvailable = i;
                break;
            }
        }

        if (firstAvailable === -1) {
            setError(`Maximum number of combos reached (${COMBO_MAX}).`);
            return;
        }

        // Generate default name "Combo #(n+1)", appending "-x" if occupied
        const n = combos.length;
        const baseName = `Combo #${n + 1}`;
        const isNameOccupied = (nameToCheck: string) => {
            return combos.some(c => c.name.trim().toLowerCase() === nameToCheck.trim().toLowerCase());
        };

        let finalName = baseName;
        if (isNameOccupied(baseName)) {
            let x = 2;
            while (isNameOccupied(`${baseName}-${x}`)) {
                x++;
            }
            finalName = `${baseName}-${x}`;
        }

        setSelected({
            id: -1,
            name: finalName,
            keys: [],
            action: 0,
            activeLayers: [0, 1, 2, 3],
            strictOrder: false
        });
        setError(null);
    };

    const handleSave = async (combo: Combo) => {
        if (!combo.name.trim()) { setError('Please enter a name for the combo.'); return; }
        if (combo.keys.length < 2) { setError('A combo must have at least 2 keys.'); return; }
        if (combo.action === 0) { setError('Please select an action.'); return; }
        
        setIsSaving(true);
        setError(null);
        try {
            await onSave(combo);
            setSelected(null);
            showNotification(`Combo "${combo.name}" saved`, "success");
        } catch (e: unknown) {
            const msg = e instanceof Error ? e.message : 'Save failed';
            setError(msg);
            showNotification(`Failed to save combo: ${msg}`, "error");
        } finally {
            setIsSaving(false);
        }
    };

    const handleDelete = async (id: number) => {
        setIsSaving(true);
        try {
            await onDelete(id);
            setSelected(null);
            showNotification("Combo deleted", "success");
        } catch (e: unknown) {
            const msg = e instanceof Error ? e.message : 'Delete failed';
            setError(msg);
            showNotification(`Failed to delete combo: ${msg}`, "error");
        } finally {
            setIsSaving(false);
        }
    };

    const sortedCombos = [...combos].sort((a, b) => a.id - b.id);

    return (
        <div className="ckey-dashboard" style={{ height: '100%' }}>
            <div className="ckey-dashboard-header" style={{ position: 'relative', display: 'flex', justifyContent: 'center', alignItems: 'center', marginBottom: '0', flexShrink: 0, minHeight: '42px' }}>
                <span className="board-title">Combos</span>

                <div style={{ position: 'absolute', right: 0, top: 0, bottom: 0, display: 'flex', alignItems: 'center' }}>
                    <div className="menu-container" ref={menuRef}>
                        <button className="btn-icon" onClick={() => setIsMenuOpen(!isMenuOpen)} title="Options">
                            <svg viewBox="0 0 24 24" width="24" height="24" fill="currentColor">
                                <path d="M12 8c1.1 0 2-.9 2-2s-.9-2-2-2-2 .9-2 2 .9 2 2 2zm0 2c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2zm0 6c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2z" />
                            </svg>
                        </button>
                        {isMenuOpen && (
                            <div className="dropdown-menu">
                                <button className="dropdown-item" onClick={handleExport}>Export Combos</button>
                                <button className="dropdown-item" onClick={() => { fileInputRef.current?.click(); setIsMenuOpen(false); }}>Import Combos</button>
                                <button className="dropdown-item" onClick={() => { onReload?.(); setIsMenuOpen(false); }}>Refresh</button>
                            </div>
                        )}
                    </div>
                </div>

                <div style={{ display: 'flex', gap: '0.5rem', alignItems: 'center' }}>
                    <span className="ckey-count-badge">{combos.length} / {COMBO_MAX}</span>
                    <button className="btn-new-action btn-new-success" onClick={handleNew} disabled={combos.length >= COMBO_MAX}>
                        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
                            <line x1="12" y1="5" x2="12" y2="19"></line>
                            <line x1="5" y1="12" x2="19" y2="12"></line>
                        </svg>
                        New
                    </button>
                </div>
            </div>

            <div className="ckey-list-full list-scroll-area">
                <div className="macro-cards-list">
                    {sortedCombos.length === 0 ? (
                        <div className="empty-state">No combos defined yet.</div>
                    ) : (
                        sortedCombos.map(c => (
                            <ComboCard
                                key={c.id}
                                combo={c}
                                isSelected={selected?.id === c.id}
                                onClick={() => setSelected(c)}
                                onDelete={handleDelete}
                                macros={macros}
                                isDeveloperMode={isDeveloperMode}
                            />
                        ))
                    )}
                </div>
            </div>

            {selected && (
                <ComboEditorModal
                    combo={selected}
                    macros={macros}
                    isSaving={isSaving}
                    error={error}
                    onSave={handleSave}
                    onDelete={handleDelete}
                    onClose={() => setSelected(null)}
                    isDeveloperMode={isDeveloperMode}
                />
            )}
            <input type="file" ref={fileInputRef} style={{ display: 'none' }} accept=".json" onChange={handleImport} />
        </div>
    );
}
