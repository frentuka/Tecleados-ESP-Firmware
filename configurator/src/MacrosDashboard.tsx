import { useState, useEffect, useRef } from 'react';
import { createPortal } from 'react-dom';
import type { Macro, MacroElement, MacroAction } from './App';
import { getKeyName, getKeyClass, MACRO_BASE, BROWSER_CODE_TO_HID } from './KeyDefinitions';
import SearchableKeyModal from './SearchableKeyModal';

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

function MacroEditorModal({ macro: initialMacro, onSave, onClose, macros, maxEvents }: MacroEditorModalProps) {
    const implodeElements = (els: MacroElement[]): MacroElement[] => {
        const result: MacroElement[] = [];
        for (let i = 0; i < els.length; i++) {
            const current = els[i];
            const next = els[i + 1];
            if (current.type === 'key' && next?.type === 'sleep') {
                result.push({ ...current, inlineSleep: next.duration });
                i++; // Skip next
            } else {
                result.push(current);
            }
        }
        return result;
    };

    const explodeElements = (els: MacroElement[]): MacroElement[] => {
        return els.flatMap(el => {
            if (el.type === 'key' && el.inlineSleep !== undefined) {
                const { inlineSleep, ...keyOnly } = el;
                return [keyOnly as MacroElement, { type: 'sleep' as const, duration: inlineSleep }];
            }
            return [el];
        });
    };

    const [name, setName] = useState(initialMacro.name || `Custom Macro #${macros.length + 1}`);
    const [elements, setElements] = useState<MacroElement[]>(implodeElements(initialMacro.elements || []));
    const [isKeyModalOpen, setIsKeyModalOpen] = useState(false);
    const [editingElementIndex, setEditingElementIndex] = useState<number | null>(null);
    const [draggedIndex, setDraggedIndex] = useState<number | null>(null);
    const [dragOverIndex, setDragOverIndex] = useState<number | null>(null);

    // Config and Recording State
    const [isRecording, setIsRecording] = useState(false);
    const [recordDelay, setRecordDelay] = useState(true);
    const [defaultDelay, setDefaultDelay] = useState(100);
    const [showConfigMenu, setShowConfigMenu] = useState(false);
    const configMenuRef = useRef<HTMLDivElement>(null);

    // Compute real event count (exploded = inline sleeps become separate events)
    const explodedCount = explodeElements(elements).length;
    const isAtEventLimit = maxEvents !== undefined && explodedCount >= maxEvents;

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
                    const exploded = explodeElements(prev);
                    if (maxEvents !== undefined && exploded.length >= maxEvents) {
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
                    const exploded = explodeElements(prev);
                    if (maxEvents !== undefined && exploded.length >= maxEvents) {
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

    const addKey = () => {
        setEditingElementIndex(null);
        setIsKeyModalOpen(true);
    };

    const handleSelectKey = (key: number) => {
        if (editingElementIndex !== null) {
            const newElements = [...elements];
            newElements[editingElementIndex] = { type: 'key', key };
            setElements(newElements);
        } else {
            setElements([...elements, { type: 'key', key }]);
        }
        setIsKeyModalOpen(false);
    };

    const removeElement = (index: number) => {
        setElements(elements.filter((_, i) => i !== index));
    };

    const updateSleep = (index: number, duration: number) => {
        const newElements = [...elements];
        newElements[index] = { type: 'sleep', duration: Math.max(0, duration) };
        setElements(newElements);
    };

    const toggleAction = (index: number) => {
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
        newElements[index] = { ...el, action: nextAction };
        setElements(newElements);
    };

    const toggleInlineSleep = (index: number) => {
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
        const newElements = [...elements];
        const el = newElements[index];
        if (el.type !== 'key') return;
        el.inlineSleep = Math.max(0, duration);
        setElements(newElements);
    };

    const handleDragStart = (e: React.DragEvent, index: number) => {
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
                        <div style={{ display: 'flex', gap: '8px' }}>
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
                                Add
                            </button>

                            <button
                                className="btn btn-sm"
                                onClick={() => setIsRecording(!isRecording)}
                                style={{ backgroundColor: 'var(--danger-color)', color: 'white', border: '1px solid var(--danger-color)' }}
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
                                    <div className="dropdown-menu" style={{ width: '220px', padding: '12px', right: '0', top: '100%' }}>
                                        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: '12px' }}>
                                            <span style={{ fontSize: '14px', color: 'var(--text-primary)' }}>Record delay</span>
                                            <input
                                                type="checkbox"
                                                checked={recordDelay}
                                                onChange={(e) => setRecordDelay(e.target.checked)}
                                                style={{ width: '16px', height: '16px', cursor: 'pointer' }}
                                            />
                                        </div>
                                        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
                                            <span style={{ fontSize: '14px', color: 'var(--text-primary)' }}>Default delay</span>
                                            <div style={{ display: 'flex', alignItems: 'center' }}>
                                                <input
                                                    type="number"
                                                    value={defaultDelay === 0 ? '' : defaultDelay}
                                                    onChange={e => setDefaultDelay(e.target.value === '' ? 0 : parseInt(e.target.value) || 0)}
                                                    min="0"
                                                    style={{ width: '60px', padding: '4px', background: 'rgba(0,0,0,0.2)', border: '1px solid rgba(255,255,255,0.1)', color: 'var(--text-primary)', borderRadius: '4px', textAlign: 'right' }}
                                                />
                                                <span style={{ fontSize: '12px', color: 'var(--text-secondary)', marginLeft: '6px' }}>ms</span>
                                            </div>
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
                            <div className="empty-state">No elements added yet.</div>
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
                                                    className={`btn-action-toggle ${el.key >= MACRO_BASE ? 'disabled' : ''}`}
                                                    onClick={() => toggleAction(i)}
                                                    title={el.key >= MACRO_BASE ? "Macros are always 'Tap'" : (el.action ? el.action.charAt(0).toUpperCase() + el.action.slice(1) : 'Tap')}
                                                    disabled={el.key >= MACRO_BASE}
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
                                                <button
                                                    className={`btn-action-toggle btn-moon-toggle ${el.inlineSleep !== undefined ? 'active' : ''}`}
                                                    onClick={() => toggleInlineSleep(i)}
                                                    title="Add sleep after this action"
                                                    style={{ order: 10 }}
                                                >
                                                    <MoonIcon />
                                                </button>
                                                {el.inlineSleep !== undefined && (
                                                    <div className="inline-sleep-container" style={{ order: 11 }}>
                                                        <span className="inline-sleep-label">then sleep for</span>
                                                        <input
                                                            type="number"
                                                            value={el.inlineSleep === 0 ? '' : el.inlineSleep}
                                                            onChange={e => updateInlineSleep(i, e.target.value === '' ? 0 : parseInt(e.target.value) || 0)}
                                                            min="0"
                                                            className="inline-sleep-input"
                                                        />
                                                        <span className="inline-sleep-suffix">ms</span>
                                                    </div>
                                                )}
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
                    <div style={{ padding: '8px 16px', background: 'rgba(255,160,0,0.12)', borderBottom: '1px solid rgba(255,160,0,0.3)', color: '#ffb74d', fontSize: '13px', textAlign: 'center' }}>
                        Maximum actions reached ({maxEvents})
                    </div>
                )}
                <div className="modal-footer">
                    <button className="btn" onClick={onClose}>Cancel</button>
                    <button className="btn btn-success" onClick={() => {
                        const exploded = explodeElements(elements);
                        const filteredElements = exploded.filter(el => el.type !== 'sleep' || el.duration > 0);
                        onSave({ ...initialMacro, name, elements: filteredElements });
                    }}>
                        Save
                    </button>
                </div>

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

interface MacrosDashboardProps {
    macros: Macro[];
    macroLimits?: { maxEvents: number; maxMacros: number } | null;
    onSaveMacro: (macro: Macro) => void;
    onDeleteMacro: (id: number) => void;
    onReload?: () => void;
    onFetchSingleMacro?: (id: number) => Promise<Macro | null>;
}

export default function MacrosDashboard({ macros, macroLimits, onSaveMacro, onDeleteMacro, onReload, onFetchSingleMacro }: MacrosDashboardProps) {
    const [editingMacro, setEditingMacro] = useState<Macro | null>(null);
    const [fetchingMacroId, setFetchingMacroId] = useState<number | null>(null);
    const [isMenuOpen, setIsMenuOpen] = useState(false);
    const menuRef = useRef<HTMLDivElement>(null);
    const isAtMacroLimit = macroLimits != null && macros.length >= macroLimits.maxMacros;

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

    return (
        <div className="macros-dashboard">
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
                <button className="btn btn-success" onClick={handleCreate} disabled={isAtMacroLimit} title={isAtMacroLimit ? `Maximum macros reached (${macroLimits!.maxMacros})` : undefined}>
                    <svg viewBox="0 0 24 24" width="18" height="18" fill="currentColor"><path d="M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z" /></svg>
                    Create Macro
                </button>
            </div>

            <div className="macros-list">
                {macros.length === 0 ? (
                    <div className="empty-state">No macros defined yet.</div>
                ) : (
                    <div className="macro-cards-grid">
                        {macros.map(m => (
                            <div key={m.id} className="macro-card glass-panel">
                                <div className="macro-card-header">
                                    <span className="macro-id">0x{(MACRO_BASE + m.id).toString(16).toUpperCase()}</span>
                                    <h4>{m.name || `Macro #${m.id}`}</h4>
                                </div>
                                <div className="macro-card-body">
                                    <div className="macro-preview-sequence">
                                        {m.elements && m.elements.length > 0 ? (
                                            <>
                                                {m.elements.slice(0, 5).map((el, i) => (
                                                    <span key={i} className="preview-el">
                                                        {el.type === 'key' ? getKeyName(el.key, macros) : `⌛${el.duration}ms`}
                                                    </span>
                                                ))}
                                                {m.elements.length > 5 && <span className="preview-more">...</span>}
                                            </>
                                        ) : (
                                            <span className="preview-more" style={{ opacity: 0.5, fontStyle: 'italic' }}>
                                                Elements hidden (Click Edit to view)
                                            </span>
                                        )}
                                    </div>
                                </div>
                                <div className="macro-card-actions">
                                    <button className="btn btn-sm" onClick={() => handleEdit(m)} disabled={fetchingMacroId !== null}>
                                        {fetchingMacroId === m.id ? 'Loading...' : 'Edit'}
                                    </button>
                                    <button className="btn btn-sm btn-danger" onClick={() => onDeleteMacro(m.id)}>Delete</button>
                                </div>
                            </div>
                        ))}
                    </div>
                )}
            </div>

            {editingMacro && (
                <MacroEditorModal
                    macro={editingMacro}
                    macros={macros}
                    maxEvents={macroLimits?.maxEvents}
                    onSave={(m) => {
                        onSaveMacro(m);
                        setEditingMacro(null);
                    }}
                    onClose={() => setEditingMacro(null)}
                />
            )}
        </div>
    );
}
