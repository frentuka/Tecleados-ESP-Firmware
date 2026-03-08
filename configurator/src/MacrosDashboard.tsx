import React, { useState, useEffect, useRef, useLayoutEffect, useMemo } from 'react';
import { createPortal } from 'react-dom';
import type { Macro, MacroElement, MacroAction } from './App';
import { getKeyName, getKeyClass, MACRO_BASE, BROWSER_CODE_TO_HID } from './KeyDefinitions';
import SearchableKeyModal from './SearchableKeyModal';
import { useConfirm } from './hooks/useConfirm';

interface ImportableMacro extends Macro {
    tempId: string;
}

// ── Execution Mode Helpers ──────────────────────────────────────────────
type ModeCategory = 'once' | 'repeat' | 'burst';

function getModeCategory(execMode: number): ModeCategory {
    if (execMode >= 3 && execMode <= 6) return 'repeat';
    if (execMode === 7) return 'burst';
    return 'once';
}

// ── Execution Mode Icons (2-symbol system) ──────────────────────────────
const IconOne = () => <span className="m-sym">1</span>;
const IconN = () => <span className="m-sym">N</span>;
const IconXSmall = () => <span className="m-sym m-smaller">×</span>;
const IconPlus = () => <span className="m-sym m-smaller">+</span>;
const IconExclamation = () => <span className="m-sym m-smaller">!</span>;
const IconMultiply = () => <span className="m-sym m-smaller">×</span>;

const IconHold = () => (
    <svg className="m-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
        <path d="M12 4v14M7 13l5 5 5-5M5 21h14" />
    </svg>
);

const IconToggle = () => (
    <svg className="m-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
        <path d="M18.36 6.64a9 9 0 1 1-12.73 0M12 2v10" />
    </svg>
);

const IconLoop = () => (
    <svg className="m-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
        <path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8" />
        <polyline points="21 3 21 8 16 8" />
    </svg>
);

const IconLoopCancel = () => (
    <svg className="m-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
        <path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8" />
        <polyline points="21 3 21 8 16 8" />
        <line x1="10" y1="10" x2="14" y2="14" strokeWidth="3" />
        <line x1="14" y1="10" x2="10" y2="14" strokeWidth="3" />
    </svg>
);

function getModeBadge(execMode: number): React.ReactNode {
    // 0=stack-once, 1=no-stack, 2=stack-N
    // 3=hold-finish, 4=hold-cancel, 5=toggle-finish, 6=toggle-cancel, 7=burst
    switch (execMode) {
        case 0: return <><IconOne /><IconXSmall /></>;
        case 1: return <><IconOne /><IconExclamation /></>;
        case 2: return <><IconN /><IconPlus /></>;
        case 3: return <><IconLoop /><IconHold /></>;
        case 4: return <><IconLoopCancel /><IconHold /></>;
        case 5: return <><IconLoop /><IconToggle /></>;
        case 6: return <><IconLoopCancel /><IconToggle /></>;
        case 7: return <><IconN /><IconMultiply /></>;
        default: return <><IconOne /><IconXSmall /></>;
    }
}

interface MacroEditorModalProps {
    macro: Macro;
    onSave: (macro: Macro) => void;
    onClose: () => void;
    macros: Macro[]; // For name validation/defaulting
    maxEvents?: number; // Max events allowed per macro (from firmware)
}

const ActionTapIcon = () => (
    <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <circle cx="12" cy="12" r="3" fill="currentColor" />
        <path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.93 4.93l2.12 2.12M16.95 16.95l2.12 2.12M4.93 19.07l2.12-2.12M16.95 7.05l2.12-2.12" />
    </svg>
);

const ActionPressIcon = () => (
    <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <path d="M12 5v13M19 12l-7 7-7-7" />
        <path d="M5 22h14" />
    </svg>
);

const ActionReleaseIcon = () => (
    <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <path d="M12 18V5M5 11l7-7 7 7" />
        <path d="M5 22h14" />
    </svg>
);

const MoonIcon = () => (
    <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"></path>
    </svg>
);

const MacroNameInput = ({ initialName, onChange }: { initialName: string, onChange: (val: string) => void }) => {
    const [localName, setLocalName] = useState(initialName);
    return (
        <input
            type="text"
            value={localName}
            onChange={e => setLocalName(e.target.value)}
            onBlur={() => onChange(localName)}
            onKeyDown={e => { if (e.key === 'Enter') { e.currentTarget.blur(); } }}
            placeholder="Macro Name..."
            className="macro-name-input-compact"
        />
    );
};

// ── Macro Mode Selection Modal ──────────────────────────────────────────
interface MacroModeModalProps {
    macro: Macro;
    onSave: (macro: Macro) => void;
    onClose: () => void;
}

function MacroModeModal({ macro, onSave, onClose }: MacroModeModalProps) {
    // Derive initial state from the flat execMode
    const initCat = getModeCategory(macro.execMode ?? 0);
    const initOnce = (macro.execMode !== undefined && macro.execMode <= 2) ? macro.execMode : 0;
    const initRepeatTrigger: 'hold' | 'toggle' = (macro.execMode === 5 || macro.execMode === 6) ? 'toggle' : 'hold';
    const initRepeatCancel = (macro.execMode === 4 || macro.execMode === 6);

    const [category, setCategory] = useState<ModeCategory>(initCat);
    const [onceMode, setOnceMode] = useState<number>(initOnce); // 0=stack-once, 1=no-stack, 2=stack-N
    const [stackMax, setStackMax] = useState(macro.stackMax ?? 2);
    const [repeatTrigger, setRepeatTrigger] = useState<'hold' | 'toggle'>(initRepeatTrigger);
    const [repeatCancel, setRepeatCancel] = useState(initRepeatCancel);
    const [repeatCount, setRepeatCount] = useState(macro.repeatCount ?? 2);
    const [mouseDownOnOverlay, setMouseDownOnOverlay] = useState(false);

    // Compute the flat execMode from the UI state
    const computeExecMode = (): number => {
        if (category === 'once') return onceMode; // 0, 1, or 2
        if (category === 'repeat') {
            if (repeatTrigger === 'hold') return repeatCancel ? 4 : 3;
            return repeatCancel ? 6 : 5;
        }
        return 7; // burst
    };

    const handleSave = () => {
        const mode = computeExecMode();
        const updated: Macro = {
            ...macro,
            execMode: mode,
            stackMax: mode === 2 ? Math.max(1, stackMax) : undefined,
            repeatCount: mode === 7 ? Math.max(1, repeatCount) : undefined,
        };
        onSave(updated);
        onClose();
    };

    const categoryButtons: { id: ModeCategory; icon: string; label: string; tagline: string }[] = [
        { id: 'once', icon: '1×', label: 'Once', tagline: 'Runs the macro a single time per press' },
        { id: 'repeat', icon: '⟳', label: 'Repeat', tagline: 'Loops the macro continuously' },
        { id: 'burst', icon: 'N×', label: 'Burst', tagline: 'Fires the macro multiple times per press' },
    ];

    return createPortal(
        <div
            className="modal-overlay"
            onMouseDown={e => { if (e.target === e.currentTarget) setMouseDownOnOverlay(true); else setMouseDownOnOverlay(false); }}
            onMouseUp={e => { if (mouseDownOnOverlay && e.target === e.currentTarget) onClose(); setMouseDownOnOverlay(false); }}
        >
            <div className="modal-content macro-mode-modal" onClick={e => e.stopPropagation()}>
                <div className="modal-header">
                    <h3>Execution Mode — {macro.name || `Macro #${macro.id}`}</h3>
                </div>
                <div className="modal-body" style={{ padding: '1.25rem 1.5rem' }}>
                    <p className="macro-mode-intro">Choose how this macro behaves when its key is pressed.</p>

                    <div className="macro-mode-categories">
                        {categoryButtons.map(cat => (
                            <div key={cat.id} className="macro-mode-cat-block">
                                <button
                                    className={`macro-mode-cat-btn ${category === cat.id ? 'active' : ''}`}
                                    onClick={() => setCategory(cat.id)}
                                >
                                    <span className="macro-mode-cat-icon">{cat.icon}</span>
                                    <div className="macro-mode-cat-text">
                                        <span className="macro-mode-cat-label">{cat.label}</span>
                                        <span className="macro-mode-cat-tagline">{cat.tagline}</span>
                                    </div>
                                </button>

                                {/* ── Once sub-options ── */}
                                {category === 'once' && cat.id === 'once' && (
                                    <div className="macro-mode-suboptions">
                                        <div className="macro-mode-sub-title">If pressed again while still running:</div>
                                        <label className={`macro-mode-radio ${onceMode === 0 ? 'active' : ''}`} onClick={() => setOnceMode(0)}>
                                            <input type="radio" name="once" checked={onceMode === 0} readOnly />
                                            <div>
                                                <div className="macro-mode-radio-label">Queue once</div>
                                                <div className="macro-mode-radio-desc">Runs one more time after the current execution finishes. Additional presses are ignored.</div>
                                            </div>
                                        </label>
                                        <label className={`macro-mode-radio ${onceMode === 1 ? 'active' : ''}`} onClick={() => setOnceMode(1)}>
                                            <input type="radio" name="once" checked={onceMode === 1} readOnly />
                                            <div>
                                                <div className="macro-mode-radio-label">Ignore</div>
                                                <div className="macro-mode-radio-desc">Extra presses are completely ignored until the macro finishes running.</div>
                                            </div>
                                        </label>
                                        <label className={`macro-mode-radio ${onceMode === 2 ? 'active' : ''}`} onClick={() => setOnceMode(2)}>
                                            <input type="radio" name="once" checked={onceMode === 2} readOnly />
                                            <div>
                                                <div className="macro-mode-radio-label">Queue up to N times</div>
                                                <div className="macro-mode-radio-desc">Each press while running adds another queued execution, up to a maximum of N.</div>
                                            </div>
                                        </label>
                                        {onceMode === 2 && (
                                            <div className="macro-mode-input-row" onClick={e => e.stopPropagation()}>
                                                <span>Max queued runs:</span>
                                                <input
                                                    type="number" min="1" max="255"
                                                    value={stackMax === 0 ? '' : stackMax}
                                                    onChange={e => setStackMax(e.target.value === '' ? 0 : parseInt(e.target.value) || 1)}
                                                    className="macro-mode-input"
                                                />
                                            </div>
                                        )}
                                    </div>
                                )}

                                {/* ── Repeat sub-options ── */}
                                {category === 'repeat' && cat.id === 'repeat' && (
                                    <div className="macro-mode-suboptions">
                                        <div className="macro-mode-sub-title">How to start and stop:</div>
                                        <label className={`macro-mode-radio ${repeatTrigger === 'hold' ? 'active' : ''}`} onClick={() => setRepeatTrigger('hold')}>
                                            <input type="radio" name="trigger" checked={repeatTrigger === 'hold'} readOnly />
                                            <div>
                                                <div className="macro-mode-radio-label">Hold to repeat</div>
                                                <div className="macro-mode-radio-desc">The macro loops as long as the key is physically held down. Releasing the key stops it.</div>
                                            </div>
                                        </label>
                                        <label className={`macro-mode-radio ${repeatTrigger === 'toggle' ? 'active' : ''}`} onClick={() => setRepeatTrigger('toggle')}>
                                            <input type="radio" name="trigger" checked={repeatTrigger === 'toggle'} readOnly />
                                            <div>
                                                <div className="macro-mode-radio-label">Toggle</div>
                                                <div className="macro-mode-radio-desc">Press once to start looping, press again to stop. No need to hold the key.</div>
                                            </div>
                                        </label>

                                        <div className="macro-mode-sub-title" style={{ marginTop: '0.75rem' }}>When stopped:</div>
                                        <label className={`macro-mode-radio ${!repeatCancel ? 'active' : ''}`} onClick={() => setRepeatCancel(false)}>
                                            <input type="radio" name="cancel" checked={!repeatCancel} readOnly />
                                            <div>
                                                <div className="macro-mode-radio-label">Finish current run</div>
                                                <div className="macro-mode-radio-desc">Completes the macro iteration that's currently executing before stopping. All keystrokes finish cleanly.</div>
                                            </div>
                                        </label>
                                        <label className={`macro-mode-radio ${repeatCancel ? 'active' : ''}`} onClick={() => setRepeatCancel(true)}>
                                            <input type="radio" name="cancel" checked={repeatCancel} readOnly />
                                            <div>
                                                <div className="macro-mode-radio-label">Cancel immediately</div>
                                                <div className="macro-mode-radio-desc">Stops the macro mid-execution. Any keys currently held by the macro will be released instantly.</div>
                                            </div>
                                        </label>
                                    </div>
                                )}

                                {/* ── Burst sub-options ── */}
                                {category === 'burst' && cat.id === 'burst' && (
                                    <div className="macro-mode-suboptions">
                                        <div className="macro-mode-sub-title">A single key press will fire the macro this many times in a row:</div>
                                        <div className="macro-mode-input-row" onClick={e => e.stopPropagation()}>
                                            <span>Repeat count:</span>
                                            <input
                                                type="number" min="1" max="255"
                                                value={repeatCount === 0 ? '' : repeatCount}
                                                onChange={e => setRepeatCount(e.target.value === '' ? 0 : parseInt(e.target.value) || 1)}
                                                className="macro-mode-input"
                                            />
                                            <span className="macro-mode-input-suffix">times</span>
                                        </div>
                                    </div>
                                )}
                            </div>
                        ))}
                    </div>
                </div>
                <div className="modal-footer">
                    <button className="btn" onClick={onClose}>Cancel</button>
                    <button className="btn btn-success" onClick={handleSave}>Apply</button>
                </div>
            </div>
        </div>,
        document.body
    );
}

interface ExportModalProps {
    macros: Macro[];
    onClose: () => void;
    onExport: (selectedMacros: Macro[]) => void;
    isExporting: boolean;
}

function ExportModal({ macros, onClose, onExport, isExporting }: ExportModalProps) {
    const [selectedIds, setSelectedIds] = useState<Set<number>>(new Set(macros.map(m => m.id)));

    const toggleSelection = (id: number) => {
        if (isExporting) return;
        const newSet = new Set(selectedIds);
        if (newSet.has(id)) {
            newSet.delete(id);
        } else {
            newSet.add(id);
        }
        setSelectedIds(newSet);
    };

    const selectAll = () => {
        if (isExporting) return;
        setSelectedIds(new Set(macros.map(m => m.id)));
    };

    const selectNone = () => {
        if (isExporting) return;
        setSelectedIds(new Set());
    };

    return createPortal(
        <div className="modal-overlay" onClick={isExporting ? undefined : onClose}>
            <div className="modal-content macro-list-modal" onClick={e => e.stopPropagation()}>
                <div className="modal-header">
                    <div>
                        <h3>Export Macros</h3>
                        <div className="macro-list-header-actions">
                            <button className="btn-ghost-sm" onClick={selectAll} disabled={isExporting}>Select All</button>
                            <button className="btn-ghost-sm" onClick={selectNone} disabled={isExporting}>Select None</button>
                        </div>
                    </div>
                    <button className="btn-close" onClick={onClose} disabled={isExporting}>&times;</button>
                </div>
                <div className="modal-body">
                    {macros.length === 0 ? (
                        <div className="macro-list-empty-state">
                            <div className="macro-list-empty-icon">📂</div>
                            <p>No macros available to export.</p>
                        </div>
                    ) : (
                        <div className="macro-list-container">
                            {macros.map(m => (
                                <div
                                    key={m.id}
                                    className={`macro-list-item ${selectedIds.has(m.id) ? 'selected' : ''}`}
                                    onClick={() => toggleSelection(m.id)}
                                    style={{ opacity: isExporting ? 0.6 : 1, pointerEvents: isExporting ? 'none' : 'auto' }}
                                >
                                    <div className="macro-checkbox-wrapper">
                                        <div className="macro-checkbox-custom">
                                            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="4" strokeLinecap="round" strokeLinejoin="round">
                                                <polyline points="20 6 9 17 4 12" />
                                            </svg>
                                        </div>
                                    </div>

                                    <div className="macro-list-icon">
                                        <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                                            <path d="M21 2l-2 2m-7.61 7.61a5.5 5.5 0 1 1-7.778 7.778 5.5 5.5 0 0 1 7.777-7.777zm0 0L15.5 7.5m0 0l3 3m-3-3l-2.5-2.5" />
                                        </svg>
                                    </div>

                                    <div className="macro-list-info">
                                        <div className="macro-list-name">{m.name || `Macro #${m.id}`}</div>
                                        <div className="macro-list-details">
                                            {m.elements?.length || 0} {(m.elements?.length || 0) === 1 ? 'action' : 'actions'}
                                        </div>
                                    </div>

                                    <div className="macro-id" style={{ fontSize: '0.7rem' }}>#{m.id}</div>
                                </div>
                            ))}
                        </div>
                    )}
                </div>

                <div className="macro-list-stats" style={{ margin: '0 1.5rem' }}>
                    <span>Selected: <strong>{selectedIds.size}</strong></span>
                    <span>Total: {macros.length}</span>
                </div>

                <div className="modal-footer">
                    <button className="btn" onClick={onClose} disabled={isExporting}>Cancel</button>
                    <button className="btn btn-primary" onClick={() => {
                        onExport(macros.filter(m => selectedIds.has(m.id)));
                    }} disabled={selectedIds.size === 0 || isExporting || macros.length === 0}>
                        {isExporting ? (
                            <>
                                <div className="macro-card-spinner" style={{ width: '16px', height: '16px', borderWidth: '2px' }}></div>
                                Exporting...
                            </>
                        ) : (
                            <>
                                <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round" style={{ marginRight: '8px' }}>
                                    <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" />
                                    <polyline points="7 10 12 15 17 10" />
                                    <line x1="12" y1="15" x2="12" y2="3" />
                                </svg>
                                {`Export (${selectedIds.size})`}
                            </>
                        )}
                    </button>
                </div>
            </div>
        </div>,
        document.body
    );
}

interface ImportModalProps {
    macros: ImportableMacro[];
    maxMacros: number;
    currentCount: number;
    onClose: () => void;
    onImport: (selectedMacros: ImportableMacro[]) => void;
    isImporting: boolean;
}

function ImportModal({ macros, maxMacros, currentCount, onClose, onImport, isImporting }: ImportModalProps) {
    const [selectedTempIds, setSelectedTempIds] = useState<Set<string>>(new Set());

    // Initialize selection once when macros are first loaded
    useEffect(() => {
        if (selectedTempIds.size === 0 && macros.length > 0 && !isImporting) {
            setSelectedTempIds(new Set(macros.map(m => m.tempId)));
        }
    }, [macros, isImporting]);

    const allowedToImport = Math.max(0, maxMacros - currentCount);

    // Only count macros that are still in the list AND selected
    const currentSelectedCount = macros.filter(m => selectedTempIds.has(m.tempId)).length;
    const isOverLimit = currentSelectedCount > allowedToImport;

    const toggleSelection = (tempId: string) => {
        if (isImporting) return;
        const newSet = new Set(selectedTempIds);
        if (newSet.has(tempId)) {
            newSet.delete(tempId);
        } else {
            newSet.add(tempId);
        }
        setSelectedTempIds(newSet);
    };

    const selectAll = () => {
        if (isImporting) return;
        setSelectedTempIds(new Set(macros.map(m => m.tempId)));
    };

    const selectNone = () => {
        if (isImporting) return;
        setSelectedTempIds(new Set());
    };

    return createPortal(
        <div className="modal-overlay" onClick={isImporting ? undefined : onClose}>
            <div className="modal-content macro-list-modal" onClick={e => e.stopPropagation()}>
                <div className="modal-header">
                    <div>
                        <h3>Import Macros</h3>
                        <div className="macro-list-header-actions">
                            <button className="btn-ghost-sm" onClick={selectAll} disabled={isImporting}>Select All</button>
                            <button className="btn-ghost-sm" onClick={selectNone} disabled={isImporting}>Select None</button>
                        </div>
                    </div>
                    <button className="btn-close" onClick={onClose} disabled={isImporting}>&times;</button>
                </div>
                <div className="modal-body">
                    {macros.length === 0 && isImporting ? (
                        <div className="macro-list-empty-state">
                            <div className="macro-card-spinner" style={{ marginBottom: '1rem' }}></div>
                            <p>Finishing imports...</p>
                        </div>
                    ) : (
                        <div className="macro-list-container">
                            {macros.map((m) => (
                                <div
                                    key={m.tempId}
                                    className={`macro-list-item ${selectedTempIds.has(m.tempId) ? 'selected' : ''}`}
                                    onClick={() => toggleSelection(m.tempId)}
                                    style={{ opacity: isImporting ? 0.6 : 1, pointerEvents: isImporting ? 'none' : 'auto' }}
                                >
                                    <div className="macro-checkbox-wrapper">
                                        <div className="macro-checkbox-custom">
                                            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="4" strokeLinecap="round" strokeLinejoin="round">
                                                <polyline points="20 6 9 17 4 12" />
                                            </svg>
                                        </div>
                                    </div>

                                    <div className="macro-list-icon">
                                        <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                                            <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" />
                                            <polyline points="17 8 12 3 7 8" />
                                            <line x1="12" y1="3" x2="12" y2="15" />
                                        </svg>
                                    </div>

                                    <div className="macro-list-info">
                                        <div className="macro-list-name">{m.name || `Imported Macro`}</div>
                                        <div className="macro-list-details">
                                            {m.elements?.length || 0} {(m.elements?.length || 0) === 1 ? 'action' : 'actions'}
                                        </div>
                                    </div>
                                </div>
                            ))}
                        </div>
                    )}
                </div>

                <div className="macro-list-stats" style={{ margin: '0 1.5rem', flexDirection: 'column', alignItems: 'stretch', gap: '4px' }}>
                    <div style={{ display: 'flex', justifyContent: 'space-between', width: '100%' }}>
                        <span>Selected to Import: <strong style={{ color: isOverLimit ? 'var(--danger-color)' : 'var(--accent-color)' }}>{currentSelectedCount}</strong></span>
                        <span>Slots Available: {allowedToImport}</span>
                    </div>
                    {isOverLimit && (
                        <div className="macro-list-error-text">
                            ⚠️ Maximum limit exceeded. Please deselect {currentSelectedCount - allowedToImport} macro(s).
                        </div>
                    )}
                </div>

                <div className="modal-footer">
                    <button className="btn" onClick={onClose} disabled={isImporting}>Cancel</button>
                    <button className="btn btn-primary" onClick={() => {
                        onImport(macros.filter(m => selectedTempIds.has(m.tempId)));
                    }} disabled={currentSelectedCount === 0 || isOverLimit || isImporting}>
                        {isImporting ? (
                            <>
                                <div className="macro-card-spinner" style={{ width: '16px', height: '16px' }}></div>
                                Importing...
                            </>
                        ) : 'Import Selected'}
                    </button>
                </div>
            </div>
        </div>,
        document.body
    );
}


function MacroEditorModal({ macro: initialMacro, onSave, onClose, macros, maxEvents }: MacroEditorModalProps) {
    const { confirm } = useConfirm();

    const [name, setName] = useState(initialMacro.name || `Custom Macro #${macros.length + 1}`);
    const [macroConfig, setMacroConfig] = useState({
        execMode: initialMacro.execMode,
        stackMax: initialMacro.stackMax,
        repeatCount: initialMacro.repeatCount
    });
    const [isModeModalOpen, setIsModeModalOpen] = useState(false);
    const [elements, setElements] = useState<MacroElement[]>(initialMacro.elements || []);
    const [isKeyModalOpen, setIsKeyModalOpen] = useState(false);
    const [editingElementIndex, setEditingElementIndex] = useState<number | null>(null);
    const [draggedIndex, setDraggedIndex] = useState<number | null>(null);
    const [dragOverIndex, setDragOverIndex] = useState<number | null>(null);

    // Config and Recording State
    const [isRecording, setIsRecording] = useState(false);
    const [recordDelay, setRecordDelay] = useState(true);
    const [clearOnRecord, setClearOnRecord] = useState(false);
    const [defaultDelay, setDefaultDelay] = useState(100);
    const [defaultPressTime, setDefaultPressTime] = useState(10);
    const [showConfigMenu, setShowConfigMenu] = useState(false);
    const configMenuRef = useRef<HTMLDivElement>(null);

    // Compute real event count
    const isAtEventLimit = maxEvents !== undefined && elements.length >= maxEvents;

    const listEndRef = useRef<HTMLDivElement>(null);
    const prevLenRef = useRef(elements.length);

    useEffect(() => {
        if (listEndRef.current && elements.length > prevLenRef.current) {
            listEndRef.current.scrollIntoView({ behavior: 'smooth', block: 'end' });
        }
        prevLenRef.current = elements.length;
    }, [elements.length]);

    useEffect(() => {
        const handleClickOutside = (event: MouseEvent) => {
            if (configMenuRef.current && !configMenuRef.current.contains(event.target as Node)) {
                setShowConfigMenu(false);
            }
        };

        if (showConfigMenu) {
            document.addEventListener('mousedown', handleClickOutside);
        }
        return () => {
            document.removeEventListener('mousedown', handleClickOutside);
        };
    }, [showConfigMenu]);

    const recordingStateRef = useRef({
        isRecording: false,
        lastEventTime: 0,
        activeKeys: new Set<string>(),
    });

    useEffect(() => {
        recordingStateRef.current.isRecording = isRecording;
        if (isRecording) {
            recordingStateRef.current.lastEventTime = 0;
            recordingStateRef.current.activeKeys.clear();
        }
    }, [isRecording]);

    useEffect(() => {
        const handleKeyDown = (e: KeyboardEvent) => {
            if (!recordingStateRef.current.isRecording) return;

            const hidCode = BROWSER_CODE_TO_HID[e.code];
            if (hidCode !== undefined) {
                e.preventDefault();
                e.stopPropagation();

                if (recordingStateRef.current.activeKeys.has(e.code)) return;
                recordingStateRef.current.activeKeys.add(e.code);

                const now = Date.now();
                const diff = recordingStateRef.current.lastEventTime > 0 ? now - recordingStateRef.current.lastEventTime : 0;

                setElements(prev => {
                    if (maxEvents !== undefined && prev.length >= maxEvents) {
                        return prev; // At limit, don't add more
                    }
                    const newEls = [...prev];
                    if (recordDelay && diff > 0) {
                        const lastEl = newEls.length > 0 ? newEls[newEls.length - 1] : null;
                        if (lastEl && lastEl.type === 'key') {
                            newEls[newEls.length - 1] = { ...lastEl, inlineSleep: (lastEl.inlineSleep || 0) + diff };
                        } else if (lastEl && lastEl.type === 'sleep') {
                            newEls[newEls.length - 1] = { ...lastEl, duration: lastEl.duration + diff };
                        } else {
                            newEls.push({ type: 'sleep', duration: diff });
                        }
                    }
                    newEls.push({ type: 'key', key: hidCode, action: 'press' });
                    return newEls;
                });
                recordingStateRef.current.lastEventTime = now;
            }
        };

        const handleKeyUp = (e: KeyboardEvent) => {
            if (!recordingStateRef.current.isRecording) return;

            const hidCode = BROWSER_CODE_TO_HID[e.code];
            if (hidCode !== undefined) {
                e.preventDefault();
                e.stopPropagation();

                recordingStateRef.current.activeKeys.delete(e.code);

                const now = Date.now();
                const diff = recordingStateRef.current.lastEventTime > 0 ? now - recordingStateRef.current.lastEventTime : 0;

                setElements(prev => {
                    if (maxEvents !== undefined && prev.length >= maxEvents) {
                        return prev; // At limit, don't add more
                    }
                    const newEls = [...prev];
                    if (recordDelay && diff > 0) {
                        const lastEl = newEls.length > 0 ? newEls[newEls.length - 1] : null;
                        if (lastEl && lastEl.type === 'key') {
                            newEls[newEls.length - 1] = { ...lastEl, inlineSleep: (lastEl.inlineSleep || 0) + diff };
                        } else if (lastEl && lastEl.type === 'sleep') {
                            newEls[newEls.length - 1] = { ...lastEl, duration: lastEl.duration + diff };
                        } else {
                            newEls.push({ type: 'sleep', duration: diff });
                        }
                    }
                    newEls.push({ type: 'key', key: hidCode, action: 'release' });
                    return newEls;
                });
                recordingStateRef.current.lastEventTime = now;
            }
        };

        if (isRecording) {
            window.addEventListener('keydown', handleKeyDown, { capture: true });
            window.addEventListener('keyup', handleKeyUp, { capture: true });
        }

        return () => {
            window.removeEventListener('keydown', handleKeyDown, { capture: true });
            window.removeEventListener('keyup', handleKeyUp, { capture: true });
        };
    }, [isRecording, recordDelay]);

    // Escape key handling
    useEffect(() => {
        const handleGlobalKeyDown = (e: KeyboardEvent) => {
            if (e.key === 'Escape') {
                if (isRecording) return;

                if (isKeyModalOpen) {
                    setIsKeyModalOpen(false);
                    return;
                }

                if (isModeModalOpen) {
                    setIsModeModalOpen(false);
                    return;
                }

                onClose();
            }
        };
        window.addEventListener('keydown', handleGlobalKeyDown);
        return () => window.removeEventListener('keydown', handleGlobalKeyDown);
    }, [isRecording, isKeyModalOpen, isModeModalOpen, onClose]);

    const addKey = () => {
        if (isRecording) setIsRecording(false);
        setEditingElementIndex(null);
        setIsKeyModalOpen(true);
    };

    const handleSelectKey = (key: number) => {
        if (isRecording) setIsRecording(false);
        if (editingElementIndex !== null) {
            const newElements = [...elements];
            const oldEl = newElements[editingElementIndex];
            newElements[editingElementIndex] = { ...oldEl, type: 'key', key };
            setElements(newElements);
        } else {
            const isMacro = key >= MACRO_BASE && key < MACRO_BASE + 256;
            const newEl: MacroElement = { type: 'key', key };
            if (!isMacro) {
                newEl.pressTime = defaultPressTime;
            }
            setElements([...elements, newEl]);
        }
        setIsKeyModalOpen(false);
    };

    const removeElement = (index: number) => {
        if (isRecording) setIsRecording(false);
        setElements(elements.filter((_, i) => i !== index));
    };

    const updateSleep = (index: number, duration: number) => {
        if (isRecording) setIsRecording(false);
        const newElements = [...elements];
        newElements[index] = { type: 'sleep', duration: Math.max(0, duration) };
        setElements(newElements);
    };

    const toggleAction = (index: number) => {
        if (isRecording) setIsRecording(false);
        const el = elements[index];
        if (el.type !== 'key') return;

        // Nested macros (MACRO_BASE to MACRO_BASE+255) are always "tap"
        if (el.key >= MACRO_BASE && el.key < MACRO_BASE + 256) return;

        const currentAction = el.action || 'tap';
        let nextAction: MacroAction = 'tap';

        if (currentAction === 'tap') nextAction = 'press';
        else if (currentAction === 'press') nextAction = 'release';
        else nextAction = 'tap';

        const newElements = [...elements];
        const updatedEl: any = { ...el, action: nextAction };

        if (nextAction === 'tap') {
            updatedEl.pressTime = el.pressTime !== undefined ? el.pressTime : defaultPressTime;
        } else {
            delete updatedEl.pressTime;
        }

        newElements[index] = updatedEl;
        setElements(newElements);
    };

    const toggleInlineSleep = (index: number) => {
        if (isRecording) setIsRecording(false);
        const newElements = [...elements];
        const el = newElements[index];
        if (el.type !== 'key') return;

        if (el.inlineSleep !== undefined) {
            delete el.inlineSleep;
        } else {
            el.inlineSleep = defaultDelay;
        }
        setElements(newElements);
    };

    const updateInlineSleep = (index: number, duration: number) => {
        if (isRecording) setIsRecording(false);
        const newElements = [...elements];
        const el = newElements[index];
        if (el.type !== 'key') return;
        el.inlineSleep = Math.max(0, duration);
        setElements(newElements);
    };

    const updatePressTime = (index: number, duration: number) => {
        if (isRecording) setIsRecording(false);
        const newElements = [...elements];
        const el = newElements[index];
        if (el.type !== 'key') return;
        el.pressTime = Math.max(0, duration);
        setElements(newElements);
    };

    const handleDragStart = (e: React.DragEvent, index: number) => {
        if (isRecording) setIsRecording(false);
        setDraggedIndex(index);
        e.dataTransfer.effectAllowed = 'move';
        // Required for Firefox
        e.dataTransfer.setData('text/plain', index.toString());
    };

    const handleDragOver = (e: React.DragEvent, index: number) => {
        e.preventDefault();
        setDragOverIndex(index);
    };

    const handleDrop = (e: React.DragEvent, targetIndex: number) => {
        if (isRecording) setIsRecording(false);
        e.preventDefault();
        if (draggedIndex === null || draggedIndex === targetIndex) {
            setDraggedIndex(null);
            setDragOverIndex(null);
            return;
        }

        const newElements = [...elements];
        const draggedItem = newElements[draggedIndex];
        const targetItem = newElements[targetIndex];

        // If dragging a sleep onto a key, absorb it as inlineSleep
        if (draggedItem.type === 'sleep' && targetItem.type === 'key') {
            newElements[targetIndex] = { ...targetItem, inlineSleep: draggedItem.duration || 10 };
            newElements.splice(draggedIndex, 1);
            setElements(newElements);
        } else {
            // Standard reordering
            newElements.splice(draggedIndex, 1);
            // If the element was moved from BEFORE the target to AFTER/ON it, 
            // the index might need adjustment depending on how splice works, 
            // but splice(index, 1) then splice(target, 0, item) is the standard 
            // way that handles the shift correctly.
            newElements.splice(targetIndex, 0, draggedItem);
            setElements(newElements);
        }
        setDraggedIndex(null);
        setDragOverIndex(null);
    };

    const duplicateElement = (index: number) => {
        if (isRecording) setIsRecording(false);
        const newElements = [...elements];
        const elementToDuplicate = JSON.parse(JSON.stringify(newElements[index]));
        newElements.splice(index + 1, 0, elementToDuplicate);
        setElements(newElements);
    };

    const handleDragEnd = () => {
        setDraggedIndex(null);
        setDragOverIndex(null);
    };


    const [mouseDownOnOverlay, setMouseDownOnOverlay] = useState(false);

    const handleOverlayMouseDown = (e: React.MouseEvent) => {
        if (e.target === e.currentTarget) {
            setMouseDownOnOverlay(true);
        } else {
            setMouseDownOnOverlay(false);
        }
    };

    const handleOverlayMouseUp = (e: React.MouseEvent) => {
        if (mouseDownOnOverlay && e.target === e.currentTarget) {
            onClose();
        }
        setMouseDownOnOverlay(false);
    };

    return createPortal(
        <div
            className="modal-overlay"
            onMouseDown={handleOverlayMouseDown}
            onMouseUp={handleOverlayMouseUp}
        >
            <div className="modal-content macro-editor-modal" onClick={e => e.stopPropagation()} style={{ maxWidth: '600px' }}>
                <div className="modal-header macro-modal-header">
                    <div className="macro-name-container">
                        <MacroNameInput
                            initialName={name}
                            onChange={setName}
                        />
                    </div>
                    <div className="macro-editor-actions-header">
                        <div style={{ display: 'flex', gap: '8px', alignItems: 'center' }}>
                            <button
                                className="btn btn-secondary btn-sm"
                                onClick={addKey}
                                disabled={isAtEventLimit}
                                title={isAtEventLimit ? `Maximum actions reached (${maxEvents})` : undefined}
                            >
                                <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round" style={{ marginRight: '4px' }}>
                                    <line x1="12" y1="5" x2="12" y2="19" />
                                    <line x1="5" y1="12" x2="19" y2="12" />
                                </svg>
                                Action
                            </button>
                            <button
                                className="btn btn-secondary btn-sm"
                                onClick={() => {
                                    if (isRecording) setIsRecording(false);
                                    setElements([...elements, { type: 'sleep', duration: defaultDelay || 100 }]);
                                }}
                                disabled={isAtEventLimit}
                                title={isAtEventLimit ? `Maximum actions reached (${maxEvents})` : undefined}
                            >
                                <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ marginRight: '4px' }}>
                                    <path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"></path>
                                </svg>
                                Delay
                            </button>

                            <button
                                className="btn btn-sm"
                                onClick={() => {
                                    if (!isRecording && clearOnRecord) {
                                        setElements([]);
                                    }
                                    setIsRecording(!isRecording);
                                }}
                                style={{ backgroundColor: 'var(--danger-color)', color: 'white' }}
                            >
                                {isRecording ? (
                                    <>
                                        <svg viewBox="0 0 24 24" width="16" height="16" stroke="currentColor" fill="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ marginRight: '4px', verticalAlign: 'text-bottom' }}>
                                            <rect x="6" y="6" width="12" height="12" rx="2" ry="2"></rect>
                                        </svg>
                                        Stop
                                    </>
                                ) : (
                                    <>
                                        <svg viewBox="0 0 24 24" width="16" height="16" stroke="currentColor" fill="none" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ marginRight: '4px', verticalAlign: 'text-bottom' }}>
                                            <circle cx="12" cy="12" r="6" fill="currentColor"></circle>
                                        </svg>
                                        Record
                                    </>
                                )}
                            </button>

                            <div className="add-action-dropdown" ref={configMenuRef} style={{ position: 'relative' }}>
                                <button
                                    className={`btn-icon btn-icon-ghost ${showConfigMenu ? 'active' : ''}`}
                                    onClick={() => setShowConfigMenu(!showConfigMenu)}
                                    title="Config"
                                    style={{ width: '38px', height: '38px' }}
                                >
                                    <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ verticalAlign: 'middle' }}>
                                        <circle cx="12" cy="12" r="3"></circle>
                                        <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"></path>
                                    </svg>
                                </button>
                                {showConfigMenu && (
                                    <div className="dropdown-menu config-dropdown">
                                        <div
                                            className="config-dropdown-row config-dropdown-toggle"
                                            onClick={() => setIsModeModalOpen(true)}
                                            style={{ cursor: 'pointer' }}
                                        >
                                            <span>Execution mode</span>
                                            <span className="macro-mode-badge-icon">
                                                {getModeBadge(macroConfig.execMode ?? 0)}
                                            </span>
                                        </div>
                                        <label className="config-dropdown-row config-dropdown-toggle">
                                            <span>Record delay</span>
                                            <div className="toggle-switch">
                                                <input
                                                    type="checkbox"
                                                    checked={recordDelay}
                                                    onChange={(e) => {
                                                        if (isRecording) setIsRecording(false);
                                                        setRecordDelay(e.target.checked);
                                                    }}
                                                    className="sr-only"
                                                />
                                                <span className="toggle-slider"></span>
                                            </div>
                                        </label>
                                        <label className="config-dropdown-row config-dropdown-toggle">
                                            <span>Clear on record</span>
                                            <div className="toggle-switch">
                                                <input
                                                    type="checkbox"
                                                    checked={clearOnRecord}
                                                    onChange={(e) => {
                                                        if (isRecording) setIsRecording(false);
                                                        setClearOnRecord(e.target.checked);
                                                    }}
                                                    className="sr-only"
                                                />
                                                <span className="toggle-slider"></span>
                                            </div>
                                        </label>
                                        <div className="config-dropdown-row">
                                            <span>Default delay</span>
                                            <div className="config-delay-input">
                                                <input
                                                    type="number"
                                                    value={defaultDelay === 0 ? '' : defaultDelay}
                                                    onChange={e => {
                                                        if (isRecording) setIsRecording(false);
                                                        setDefaultDelay(e.target.value === '' ? 0 : parseInt(e.target.value) || 0);
                                                    }}
                                                    min="0"
                                                />
                                                <span className="config-delay-suffix">ms</span>
                                            </div>
                                        </div>
                                        <div className="config-dropdown-row">
                                            <span>Default press time</span>
                                            <div className="config-delay-input">
                                                <input
                                                    type="number"
                                                    value={defaultPressTime === 0 ? '' : defaultPressTime}
                                                    onChange={e => {
                                                        if (isRecording) setIsRecording(false);
                                                        setDefaultPressTime(e.target.value === '' ? 0 : parseInt(e.target.value) || 0);
                                                    }}
                                                    min="0"
                                                />
                                                <span className="config-delay-suffix">ms</span>
                                            </div>
                                        </div>
                                        <div className="config-dropdown-row" style={{ marginTop: '0.5rem' }}>
                                            <button
                                                className="btn btn-sm btn-danger"
                                                style={{ width: '100%', padding: '0.4rem' }}
                                                onClick={async () => {
                                                    if (await confirm('Clear Actions', 'Are you sure you want to clear all actions?')) {
                                                        setElements([]);
                                                        setShowConfigMenu(false);
                                                    }
                                                }}
                                            >
                                                Clear all actions
                                            </button>
                                        </div>
                                    </div>
                                )}
                            </div>
                        </div>
                    </div>
                </div>
                <div className="modal-body">

                    <div className="macro-elements-list">
                        {elements.length === 0 ? (
                            <div className="empty-state">No actions added yet.</div>
                        ) : (
                            elements.map((el, i) => (
                                <div
                                    key={i}
                                    className={`macro-element-row ${draggedIndex === i ? 'dragging' : ''} ${dragOverIndex === i && draggedIndex !== null && draggedIndex !== i
                                        ? (draggedIndex > i ? 'drag-over-top' : 'drag-over-bottom')
                                        : ''
                                        }`}
                                    draggable="true"
                                    onDragStart={(e) => handleDragStart(e, i)}
                                    onDragOver={(e) => handleDragOver(e, i)}
                                    onDragEnd={handleDragEnd}
                                    onDrop={(e) => handleDrop(e, i)}
                                >
                                    <div className="element-content">
                                        {el.type === 'key' ? (
                                            <>
                                                <button
                                                    className={`btn-action-toggle ${(el.key >= MACRO_BASE && el.key < MACRO_BASE + 256) ? 'disabled' : ''}`}
                                                    onClick={() => toggleAction(i)}
                                                    title={(el.key >= MACRO_BASE && el.key < MACRO_BASE + 256) ? "Macros are always 'Tap'" : (el.action ? el.action.charAt(0).toUpperCase() + el.action.slice(1) : 'Tap')}
                                                    disabled={el.key >= MACRO_BASE && el.key < MACRO_BASE + 256}
                                                >
                                                    {(!el.action || el.action === 'tap') && <ActionTapIcon />}
                                                    {el.action === 'press' && <ActionPressIcon />}
                                                    {el.action === 'release' && <ActionReleaseIcon />}
                                                </button>

                                                <div className={`key-preview ${getKeyClass(el.key)}`} onClick={() => {
                                                    setEditingElementIndex(i);
                                                    setIsKeyModalOpen(true);
                                                }}>
                                                    {getKeyName(el.key, macros)}
                                                </div>

                                                {(!el.action || el.action === 'tap') && !(el.key >= MACRO_BASE && el.key < MACRO_BASE + 256) && (
                                                    <div className="press-time-container">
                                                        <span className="press-time-label">Press for</span>
                                                        <input
                                                            type="number"
                                                            value={el.pressTime === 0 ? '' : (el.pressTime ?? defaultPressTime)}
                                                            onChange={e => updatePressTime(i, e.target.value === '' ? 0 : parseInt(e.target.value) || 0)}
                                                            min="0"
                                                            className="press-time-input"
                                                        />
                                                        <span className="press-time-suffix">ms</span>
                                                    </div>
                                                )}

                                                <div className={`inline-sleep-container ${el.inlineSleep !== undefined ? 'expanded' : ''}`}>
                                                    <div className="inline-sleep-inner">
                                                        <div className="inline-sleep-fields">
                                                            <input
                                                                type="number"
                                                                value={el.inlineSleep === 0 ? '' : (el.inlineSleep ?? '')}
                                                                onChange={e => updateInlineSleep(i, e.target.value === '' ? 0 : parseInt(e.target.value) || 0)}
                                                                min="0"
                                                                className="inline-sleep-input"
                                                            />
                                                            <span className="inline-sleep-suffix">ms</span>
                                                        </div>
                                                        <button
                                                            className={`btn-action-toggle btn-moon-toggle ${el.inlineSleep !== undefined ? 'active' : ''}`}
                                                            onClick={() => toggleInlineSleep(i)}
                                                            title="Add sleep after this action"
                                                        >
                                                            <MoonIcon />
                                                        </button>
                                                    </div>
                                                </div>
                                            </>
                                        ) : (
                                            <div className="sleep-preview">
                                                <span>Sleep</span>
                                                <input
                                                    type="number"
                                                    value={el.duration === 0 ? '' : el.duration}
                                                    onChange={e => updateSleep(i, e.target.value === '' ? 0 : parseInt(e.target.value) || 0)}
                                                    min="0"
                                                />
                                                <span>ms</span>
                                            </div>
                                        )}
                                    </div>

                                    <div className="macro-element-actions">
                                        <button className="btn-icon-sm btn-duplicate" onClick={() => duplicateElement(i)} title="Duplicate">
                                            <svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                                                <rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect>
                                                <path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path>
                                            </svg>
                                        </button>
                                        <button className="btn-remove" onClick={() => removeElement(i)}>&times;</button>
                                    </div>
                                </div>
                            ))
                        )}
                        <div ref={listEndRef} />
                    </div>
                </div>
                {isAtEventLimit && (
                    <div className="limit-warning">
                        Maximum actions reached ({maxEvents})
                    </div>
                )}
                <div className="modal-footer">
                    <button className="btn" onClick={onClose}>Cancel</button>
                    <button className="btn btn-success" onClick={() => {
                        const filteredElements = elements.filter(el => el.type !== 'sleep' || el.duration > 0);
                        onSave({ ...initialMacro, ...macroConfig, name, elements: filteredElements });
                    }}>
                        Save
                    </button>
                </div>

                {isModeModalOpen && (
                    <MacroModeModal
                        macro={{ ...initialMacro, ...macroConfig }}
                        onSave={(m) => {
                            setMacroConfig({
                                execMode: m.execMode,
                                stackMax: m.stackMax,
                                repeatCount: m.repeatCount
                            });
                        }}
                        onClose={() => setIsModeModalOpen(false)}
                    />
                )}

                {isKeyModalOpen && (
                    <SearchableKeyModal
                        currentValue={editingElementIndex !== null && elements[editingElementIndex].type === 'key' ? (elements[editingElementIndex] as any).key : 0}
                        macros={macros}
                        onSelect={handleSelectKey}
                        onClose={() => setIsKeyModalOpen(false)}
                    />
                )}
            </div>
        </div >,
        document.body
    );
}

const MacroPreviewSequence = ({ m, macros }: { m: Macro; macros: Macro[] }) => {
    const containerRef = useRef<HTMLDivElement>(null);
    const [showCount, setShowCount] = useState<number>(0);
    const [measuredSig, setMeasuredSig] = useState<string | null>(null);

    const items = useMemo(() => {
        type PItem = { kind: 'key'; action: string; keyCode: number; sleepMs?: number } | { kind: 'sleep'; duration: number };
        const arr: PItem[] = [];
        if (!m.elements) return arr;
        for (let j = 0; j < m.elements.length; j++) {
            const el = m.elements[j];
            if (el.type === 'key') {
                if (el.inlineSleep !== undefined && el.inlineSleep > 0) {
                    arr.push({ kind: 'key', action: el.action || 'tap', keyCode: el.key, sleepMs: el.inlineSleep });
                } else {
                    arr.push({ kind: 'key', action: el.action || 'tap', keyCode: el.key });
                }
            } else {
                arr.push({ kind: 'sleep', duration: el.duration });
            }
        }
        return arr;
    }, [m.elements, macros]);

    const itemsSig = useMemo(() => JSON.stringify(items), [items]);
    const isMeasured = measuredSig === itemsSig;

    useEffect(() => {
        if (!containerRef.current || items.length === 0) return;

        // Observe the parent container (macro-card-content) because it has a stable width.
        // Observing our own div creates infinite loops if items line-wrap differently upon slicing.
        const parent = containerRef.current.parentElement;
        if (!parent) return;

        let lastWidth = parent.offsetWidth;

        const ro = new ResizeObserver((entries) => {
            if (!entries[0]) return;
            const w = (entries[0].target as HTMLElement).offsetWidth;
            // Only force remeasure if the parent card actually resizes (e.g., window resize)
            if (Math.abs(w - lastWidth) > 2) {
                lastWidth = w;
                setMeasuredSig(null);
            }
        });

        ro.observe(parent);
        return () => ro.disconnect();
    }, [itemsSig]);

    useLayoutEffect(() => {
        if (!isMeasured && containerRef.current && items.length > 0) {
            const container = containerRef.current;
            const children = Array.from(container.children) as HTMLElement[];
            if (children.length === 0) return;

            let rowCount = 1;
            let currentY = children[0].offsetTop;
            let breakIdx = items.length;

            for (let i = 0; i < children.length; i++) {
                // Allow a small tolerance for vertical flex misalignment
                if (children[i].offsetTop > currentY + 5) {
                    currentY = children[i].offsetTop;
                    rowCount++;
                }
                if (rowCount > 3) {
                    breakIdx = i;
                    break;
                }
            }

            if (breakIdx < items.length) {
                setShowCount(Math.max(1, breakIdx - 1));
            } else {
                setShowCount(items.length);
            }

            // Mark as measured for this precise content state
            setMeasuredSig(itemsSig);
        }
    }, [isMeasured, items, itemsSig]);

    if (!m.elements) {
        return (
            <div className="macro-preview-sequence">
                <span className="preview-more" style={{ opacity: 0.5, fontStyle: 'italic' }}>
                    Loading...
                </span>
            </div>
        );
    }

    if (m.elements.length === 0) {
        return (
            <div className="macro-preview-sequence">
                <span className="preview-more" style={{ opacity: 0.5, fontStyle: 'italic' }}>
                    empty
                </span>
            </div>
        );
    }

    const renderItem = (it: any, i: number) => {
        if (it.kind === 'key') {
            return (
                <span key={i} className={`preview-el${it.action !== 'tap' ? ` preview-el-${it.action}` : ''}`}>
                    <span className="preview-el-action">
                        {it.action === 'press' ? '↓' : it.action === 'release' ? '↑' : '↕'}
                    </span>
                    {getKeyName(it.keyCode, macros)}
                    {it.sleepMs !== undefined && (
                        <span className="preview-el-sleep">· {it.sleepMs}</span>
                    )}
                </span>
            );
        } else {
            return (
                <span key={i} className="preview-el preview-sleep">
                    <span className="preview-sleep-icon">🌙</span>
                    <span className="preview-sleep-val">{it.duration}</span>
                </span>
            );
        }
    };

    return (
        <div ref={containerRef} className="macro-preview-sequence" style={!isMeasured ? { opacity: 0 } : undefined}>
            {!isMeasured ? items.map(renderItem) : items.slice(0, showCount).map(renderItem)}
            {isMeasured && items.length > showCount && (
                <span className="preview-more">+{items.length - showCount}</span>
            )}
        </div>
    );
};

interface MacrosDashboardProps {
    macros: Macro[];
    macroLimits: { maxEvents: number; maxMacros: number } | null;
    isDeveloperMode: boolean;
    onSaveMacro: (newMacro: Macro) => Promise<void>;
    onDeleteMacro: (id: number) => Promise<void>;
    onReload: () => void;
    onFetchSingleMacro: (id: number) => Promise<Macro | null>;
}

export default function MacrosDashboard({ macros, macroLimits, isDeveloperMode, onSaveMacro, onDeleteMacro, onReload, onFetchSingleMacro }: MacrosDashboardProps) {
    const [editingMacro, setEditingMacro] = useState<Macro | null>(null);
    const [modeMacro, setModeMacro] = useState<Macro | null>(null);
    const [fetchingMacroId, setFetchingMacroId] = useState<number | null>(null);
    const [busyMacroIds, setBusyMacroIds] = useState<Map<number, string>>(new Map());
    const [isCreating, setIsCreating] = useState(false);
    const [isMenuOpen, setIsMenuOpen] = useState(false);
    const [isExportModalOpen, setIsExportModalOpen] = useState(false);
    const [isExporting, setIsExporting] = useState(false);
    const [isImportModalOpen, setIsImportModalOpen] = useState(false);
    const [isImporting, setIsImporting] = useState(false);
    const fileInputRef = useRef<HTMLInputElement>(null);
    const menuRef = useRef<HTMLDivElement>(null);
    const isAtMacroLimit = macroLimits != null && macros.length >= macroLimits.maxMacros;

    const sortedMacros = useMemo(() => {
        return [...macros].sort((a, b) => a.name.localeCompare(b.name));
    }, [macros]);

    const markBusy = (id: number, label: string) => {
        setBusyMacroIds(prev => new Map(prev).set(id, label));
    };
    const clearBusy = (id: number) => {
        setBusyMacroIds(prev => { const next = new Map(prev); next.delete(id); return next; });
    };

    useEffect(() => {
        function handleClickOutside(event: MouseEvent) {
            if (menuRef.current && !menuRef.current.contains(event.target as Node)) {
                setIsMenuOpen(false);
            }
        }
        document.addEventListener('mousedown', handleClickOutside);
        return () => document.removeEventListener('mousedown', handleClickOutside);
    }, []);

    const handleCreate = () => {
        setEditingMacro({ id: -1, name: '', elements: [] });
    };

    const handleEdit = async (macro: Macro) => {
        if (onFetchSingleMacro) {
            setFetchingMacroId(macro.id);
            const fullMacro = await onFetchSingleMacro(macro.id);
            setFetchingMacroId(null);
            if (fullMacro) {
                setEditingMacro(fullMacro);
            } else {
                alert("Failed to fetch full macro data.");
            }
        } else {
            setEditingMacro(macro);
        }
    };

    const handleDelete = async (id: number) => {
        // Deletion confirmation is now handled centrally by App.tsx passed down via onDeleteMacro
        markBusy(id, 'Deleting...');
        try {
            await onDeleteMacro(id);
        } catch (err: any) {
            alert(`Failed to delete macro: ${err?.message || 'Unknown error'}`);
        } finally {
            clearBusy(id);
        }
    };

    const handleExportSubmit = async (selectedMacros: Macro[]) => {
        setIsExporting(true);
        try {
            const fullMacros = [];
            for (const sm of selectedMacros) {
                if (onFetchSingleMacro) {
                    const fm = await onFetchSingleMacro(sm.id);
                    if (fm) {
                        const { id, ...macroWithoutId } = fm;
                        fullMacros.push(macroWithoutId);
                    }
                } else {
                    const { id, ...macroWithoutId } = sm;
                    fullMacros.push(macroWithoutId);
                }
            }
            const dataStr = JSON.stringify(fullMacros, null, 2);

            if ('showSaveFilePicker' in window) {
                try {
                    const handle = await (window as any).showSaveFilePicker({
                        suggestedName: 'macros_export.json',
                        types: [{
                            description: 'JSON Files',
                            accept: { 'application/json': ['.json'] },
                        }],
                    });
                    const writable = await handle.createWritable();
                    await writable.write(dataStr);
                    await writable.close();
                } catch (err: any) {
                    // Ignore AbortError when user cancels the save dialog
                    if (err.name !== 'AbortError') {
                        throw err;
                    }
                }
            } else {
                const url = "data:text/json;charset=utf-8," + encodeURIComponent(dataStr);
                const downloadAnchorNode = document.createElement('a');
                downloadAnchorNode.setAttribute("href", url);
                downloadAnchorNode.setAttribute("download", "macros_export.json");
                document.body.appendChild(downloadAnchorNode);
                downloadAnchorNode.click();
                downloadAnchorNode.remove();
            }
        } catch (err) {
            alert("Failed to export macros.");
        } finally {
            setIsExporting(false);
            setIsExportModalOpen(false);
        }
    };

    const [macrosToImport, setMacrosToImport] = useState<ImportableMacro[]>([]);

    const handleImport = async (event: React.ChangeEvent<HTMLInputElement>) => {
        const file = event.target.files?.[0];
        if (!file) return;

        const reader = new FileReader();
        reader.onload = async (e) => {
            try {
                const importedData = JSON.parse(e.target?.result as string);
                const parsedMacros = Array.isArray(importedData) ? importedData : [importedData];
                // Augment with tempId for robust tracking
                const importableMacros: ImportableMacro[] = parsedMacros.map((m, i) => ({
                    ...m,
                    tempId: `import-${Date.now()}-${i}-${Math.random().toString(36).substring(2, 9)}`
                }));
                setMacrosToImport(importableMacros);
                setIsImportModalOpen(true);
            } catch (error) {
                alert("Failed to parse JSON file.");
            }
        };
        reader.readAsText(file);

        event.target.value = ''; // Reset input
    };

    const importGuardRef = useRef(false);
    const handleImportSubmit = async (selectedMacros: ImportableMacro[]) => {
        if (importGuardRef.current) return;
        importGuardRef.current = true;
        setIsImporting(true);
        try {
            for (const m of selectedMacros) {
                const { tempId, id, ...restOfMacro } = m;
                const newMacro = { ...restOfMacro, id: -1 };
                setIsCreating(true);
                try {
                    await onSaveMacro(newMacro);
                    // On success, remove from the parent state so it disappears from the modal
                    setMacrosToImport(prev => prev.filter(item => item.tempId !== tempId));
                } catch (err: any) {
                    alert(`Failed to save imported macro "${newMacro.name}": ${err?.message || 'Unknown error'}`);
                    break; // Stop on first error to avoid alert storm
                } finally {
                    setIsCreating(false);
                }
            }
        } finally {
            setIsImporting(false);
            // Close the modal only if we've successfully processed all selected macros
            setMacrosToImport(prev => {
                if (prev.length === 0) {
                    setIsImportModalOpen(false);
                }
                return prev;
            });
            importGuardRef.current = false;
        }
    };

    return (
        <div className="macros-dashboard">
            <input
                type="file"
                ref={fileInputRef}
                style={{ display: 'none' }}
                accept=".json"
                onChange={handleImport}
            />
            <div className="macros-header">
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', width: '100%', marginBottom: '0.5rem' }}>
                    <h2 className="section-title">Macros Editor</h2>
                    <div className="menu-container" ref={menuRef}>
                        <button className="btn-icon" onClick={() => setIsMenuOpen(!isMenuOpen)} title="Options">
                            <svg viewBox="0 0 24 24" width="24" height="24" fill="currentColor">
                                <path d="M12 8c1.1 0 2-.9 2-2s-.9-2-2-2-2 .9-2 2 .9 2 2 2zm0 2c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2zm0 6c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2z" />
                            </svg>
                        </button>
                        {isMenuOpen && (
                            <div className="dropdown-menu">
                                <button className="dropdown-item" onClick={() => { setIsExportModalOpen(true); setIsMenuOpen(false); }}>
                                    <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                                        <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
                                        <polyline points="7 10 12 15 17 10"></polyline>
                                        <line x1="12" y1="15" x2="12" y2="3"></line>
                                    </svg>
                                    Export Macros
                                </button>
                                <button className="dropdown-item" onClick={() => { fileInputRef.current?.click(); setIsMenuOpen(false); }}>
                                    <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                                        <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
                                        <polyline points="17 8 12 3 7 8"></polyline>
                                        <line x1="12" y1="3" x2="12" y2="15"></line>
                                    </svg>
                                    Import Macros
                                </button>
                                <button className="dropdown-item" onClick={() => { onReload?.(); setIsMenuOpen(false); }}>
                                    <svg viewBox="0 0 24 24" width="18" height="18" fill="currentColor">
                                        <path d="M17.65 6.35C16.2 4.9 14.21 4 12 4c-4.42 0-7.99 3.58-7.99 8s3.57 8 7.99 8c3.73 0 6.84-2.55 7.73-6h-2.08c-.82 2.33-3.04 4-5.65 4-3.31 0-6-2.69-6-6s2.69-6 6-6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z" />
                                    </svg>
                                    Refresh
                                </button>
                            </div>
                        )}
                    </div>
                </div>
                <button className="btn" onClick={handleCreate} disabled={isAtMacroLimit} title={isAtMacroLimit ? `Maximum macros reached (${macroLimits!.maxMacros})` : undefined}>
                    <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round" style={{ marginRight: '6px' }}>
                        <line x1="12" y1="5" x2="12" y2="19" />
                        <line x1="5" y1="12" x2="19" y2="12" />
                    </svg>
                    Create Macro
                </button>
            </div>

            <div className="macros-list">
                {macros.length === 0 && !isCreating ? (
                    <div className="empty-state">No macros defined yet.</div>
                ) : (
                    <div className="macro-cards-grid">
                        {sortedMacros.map(m => (
                            <div key={m.id} className={`macro-card glass-panel ${busyMacroIds.has(m.id) ? 'macro-card-busy' : ''}`} onClick={() => !busyMacroIds.has(m.id) && handleEdit(m)} style={{ cursor: busyMacroIds.has(m.id) ? 'default' : 'pointer', position: 'relative' }}>
                                {busyMacroIds.has(m.id) && (
                                    <div className="macro-card-loading-overlay">
                                        <div className="macro-card-spinner" />
                                        <span>{busyMacroIds.get(m.id)}</span>
                                    </div>
                                )}
                                <button
                                    className="macro-mode-badge-corner"
                                    onClick={(e) => { e.stopPropagation(); if (!busyMacroIds.has(m.id)) setModeMacro(m); }}
                                    title="Change execution mode"
                                    disabled={busyMacroIds.has(m.id)}
                                >
                                    <span className="macro-mode-badge-icon">{getModeBadge(m.execMode ?? 0)}</span>
                                </button>

                                <div className="macro-card-header">
                                    <h4>{m.name || `Macro #${m.id}`}</h4>
                                </div>
                                <div className="macro-card-body">
                                    <MacroPreviewSequence m={m} macros={macros} />
                                </div>
                                <div className="macro-card-actions" onClick={e => e.stopPropagation()}>
                                    <div style={{ flex: 1, display: 'flex', alignItems: 'center' }}>
                                        {fetchingMacroId === m.id && <span style={{ fontSize: '0.8rem', color: 'var(--text-secondary)' }}>Loading...</span>}
                                        {isDeveloperMode && (
                                            <span className="macro-id-dev" style={{ position: 'relative', bottom: 'auto', right: 'auto', marginLeft: fetchingMacroId === m.id ? '0.5rem' : '0' }}>
                                                ID: 0x{(MACRO_BASE + m.id).toString(16).toUpperCase()}
                                            </span>
                                        )}
                                    </div>
                                    <button className="btn-icon btn-danger" title="Delete" onClick={() => handleDelete(m.id)} disabled={busyMacroIds.has(m.id)} style={{ position: 'absolute', bottom: '0.75rem', right: '0.75rem', width: '28px', height: '28px', padding: 0, background: 'rgba(255, 60, 60, 0.1)', border: '1px solid rgba(255, 60, 60, 0.25)', borderRadius: '6px' }}>
                                        <svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ verticalAlign: 'middle' }}>
                                            <polyline points="3 6 5 6 21 6"></polyline>
                                            <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"></path>
                                            <line x1="10" y1="11" x2="10" y2="17"></line>
                                            <line x1="14" y1="11" x2="14" y2="17"></line>
                                        </svg>
                                    </button>
                                </div>
                            </div>
                        ))}
                        {isCreating && (
                            <div className="macro-card glass-panel macro-card-busy" style={{ position: 'relative', cursor: 'default' }}>
                                <div className="macro-card-loading-overlay">
                                    <div className="macro-card-spinner" />
                                    <span>Creating...</span>
                                </div>
                                <div className="macro-card-header"><h4 style={{ opacity: 0.3 }}>New Macro</h4></div>
                                <div className="macro-card-body" />
                            </div>
                        )}
                    </div>
                )}
            </div>

            {modeMacro && (
                <MacroModeModal
                    macro={modeMacro}
                    onSave={async (m) => {
                        setModeMacro(null);
                        markBusy(m.id, 'Saving...');
                        try {
                            await onSaveMacro(m);
                        } catch (err: any) {
                            alert(`Failed to save mode: ${err?.message || 'Unknown error'}`);
                        } finally {
                            clearBusy(m.id);
                        }
                    }}
                    onClose={() => setModeMacro(null)}
                />
            )}

            {editingMacro && (
                <MacroEditorModal
                    macro={editingMacro}
                    macros={macros}
                    maxEvents={macroLimits?.maxEvents}
                    onSave={async (m) => {
                        setEditingMacro(null);
                        const isNew = m.id === -1;
                        if (isNew) {
                            setIsCreating(true);
                        } else {
                            markBusy(m.id, 'Saving...');
                        }
                        try {
                            await onSaveMacro(m);
                        } catch (err: any) {
                            alert(`Failed to save macro: ${err?.message || 'Unknown error'}`);
                        } finally {
                            if (isNew) {
                                setIsCreating(false);
                            } else {
                                clearBusy(m.id);
                            }
                        }
                    }}
                    onClose={() => setEditingMacro(null)}
                />
            )}

            {isExportModalOpen && (
                <ExportModal
                    macros={sortedMacros}
                    onClose={() => setIsExportModalOpen(false)}
                    onExport={handleExportSubmit}
                    isExporting={isExporting}
                />
            )}

            {isImportModalOpen && (
                <ImportModal
                    macros={macrosToImport}
                    maxMacros={macroLimits?.maxMacros || 0}
                    currentCount={macros.length}
                    onClose={() => setIsImportModalOpen(false)}
                    onImport={handleImportSubmit}
                    isImporting={isImporting}
                />
            )}
        </div>
    );
}
