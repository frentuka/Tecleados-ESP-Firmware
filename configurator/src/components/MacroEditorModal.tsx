import React, { useState, useEffect, useRef } from 'react';
import { createPortal } from 'react-dom';
import type { Macro, MacroElement } from '../types/macros';
import SearchableKeyModal from './SearchableKeyModal';
import { useConfirm } from '../hooks/useConfirm';
import MacroModeModal from './MacroModeModal';
// Removed unused icons
import MacroTimelineEditor, { type MacroTimelineRef } from './timeline/MacroTimelineEditor';

interface MacroEditorModalProps {
    macro: Macro;
    onSave: (macro: Macro) => void;
    onClose: () => void;
    macros: Macro[];
    maxEvents?: number;
}

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

export default function MacroEditorModal({ macro: initialMacro, onSave, onClose, macros, maxEvents }: MacroEditorModalProps) {
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
    const timelineRef = useRef<MacroTimelineRef>(null);

    const [isRecording, setIsRecording] = useState(false);
    const [recordDelay, setRecordDelay] = useState(true);
    const [clearOnRecord, setClearOnRecord] = useState(false);
    const [defaultDelay, setDefaultDelay] = useState(100);
    const [defaultPressTime, setDefaultPressTime] = useState(10);
    const [showConfigMenu, setShowConfigMenu] = useState(false);
    const configMenuRef = useRef<HTMLDivElement>(null);

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

    // Removed recordingStateRef

    const addKey = () => setIsKeyModalOpen(true);
    const handleSelectKey = (key: number) => {
        timelineRef.current?.addKeyBlock(key);
        setIsKeyModalOpen(false);
    };

    useEffect(() => {
        const handleGlobalKeyDown = (e: KeyboardEvent) => {
            if (e.key === 'Escape') {
                if (isRecording) return;
                if (isKeyModalOpen) { setIsKeyModalOpen(false); return; }
                if (isModeModalOpen) { setIsModeModalOpen(false); return; }
                onClose();
            }
        };
        window.addEventListener('keydown', handleGlobalKeyDown);
        return () => window.removeEventListener('keydown', handleGlobalKeyDown);
    }, [isRecording, isKeyModalOpen, isModeModalOpen, onClose]);

    // Removed legacy UI functions

    // Removed legacy drag and drop functions

    const [mouseDownOnOverlay, setMouseDownOnOverlay] = useState(false);
    const handleOverlayMouseDown = (e: React.MouseEvent) => {
        if (e.target === e.currentTarget) setMouseDownOnOverlay(true);
        else setMouseDownOnOverlay(false);
    };
    const handleOverlayMouseUp = (e: React.MouseEvent) => {
        if (mouseDownOnOverlay && e.target === e.currentTarget) onClose();
        setMouseDownOnOverlay(false);
    };

    return createPortal(
        <div className="modal-overlay" onMouseDown={handleOverlayMouseDown} onMouseUp={handleOverlayMouseUp}>
            <div className="modal-content macro-editor-modal" onClick={e => e.stopPropagation()} style={{ maxWidth: '600px' }}>
                <div className="modal-header macro-modal-header">
                    <div className="macro-name-container"><MacroNameInput initialName={name} onChange={setName} /></div>
                    <div className="macro-editor-actions-header">
                        <div style={{ display: 'flex', gap: '8px', alignItems: 'center' }}>
                            <button className="btn btn-secondary btn-sm" onClick={addKey} disabled={isAtEventLimit} title={isAtEventLimit ? `Maximum actions reached (${maxEvents})` : undefined}>
                                <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round" style={{ marginRight: '4px' }}>
                                    <line x1="12" y1="5" x2="12" y2="19" />
                                    <line x1="5" y1="12" x2="19" y2="12" />
                                </svg>
                                Action
                            </button>
                            <button className="btn btn-secondary btn-sm" onClick={() => timelineRef.current?.addSleepBlock(defaultDelay || 100)} disabled={isAtEventLimit} title={isAtEventLimit ? `Maximum actions reached (${maxEvents})` : undefined}>
                                <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ marginRight: '4px' }}>
                                    <path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"></path>
                                </svg>
                                Delay
                            </button>
                            <button className="btn btn-sm" onClick={() => { if (!isRecording && clearOnRecord) setElements([]); setIsRecording(!isRecording); }} style={{ backgroundColor: 'var(--danger-color)', color: 'white' }}>
                                {isRecording ? <>Stop</> : <>Record</>}
                            </button>
                            <div className="add-action-dropdown" ref={configMenuRef} style={{ position: 'relative' }}>
                                <button className={`btn-icon btn-icon-ghost ${showConfigMenu ? 'active' : ''}`} onClick={() => setShowConfigMenu(!showConfigMenu)} title="Config" style={{ width: '38px', height: '38px' }}>
                                    <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ verticalAlign: 'middle' }}>
                                        <circle cx="12" cy="12" r="3"></circle>
                                        <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"></path>
                                    </svg>
                                </button>
                                {showConfigMenu && (
                                    <div className="dropdown-menu config-dropdown">
                                        <div className="config-dropdown-row config-dropdown-toggle" onClick={() => setIsModeModalOpen(true)} style={{ cursor: 'pointer' }}>
                                            <span>Execution mode</span>
                                        </div>
                                        <label className="config-dropdown-row config-dropdown-toggle">
                                            <span>Record delay</span>
                                            <div className="toggle-switch">
                                                <input type="checkbox" checked={recordDelay} onChange={(e) => { if (isRecording) setIsRecording(false); setRecordDelay(e.target.checked); }} className="sr-only" />
                                                <span className="toggle-slider"></span>
                                            </div>
                                        </label>
                                        <label className="config-dropdown-row config-dropdown-toggle">
                                            <span>Clear on record</span>
                                            <div className="toggle-switch">
                                                <input type="checkbox" checked={clearOnRecord} onChange={(e) => { if (isRecording) setIsRecording(false); setClearOnRecord(e.target.checked); }} className="sr-only" />
                                                <span className="toggle-slider"></span>
                                            </div>
                                        </label>
                                        <div className="config-dropdown-row">
                                            <span>Default delay</span>
                                            <div className="config-delay-input"><input type="number" value={defaultDelay === 0 ? '' : defaultDelay} onChange={e => { if (isRecording) setIsRecording(false); setDefaultDelay(e.target.value === '' ? 0 : parseInt(e.target.value) || 0); }} min="0" /></div>
                                        </div>
                                        <div className="config-dropdown-row">
                                            <span>Default press time</span>
                                            <div className="config-delay-input"><input type="number" value={defaultPressTime === 0 ? '' : defaultPressTime} onChange={e => { if (isRecording) setIsRecording(false); setDefaultPressTime(e.target.value === '' ? 0 : parseInt(e.target.value) || 0); }} min="0" /></div>
                                        </div>
                                        <div className="config-dropdown-row" style={{ marginTop: '0.5rem' }}>
                                            <button className="btn btn-sm btn-danger" style={{ width: '100%', padding: '0.4rem' }} onClick={async () => { if (await confirm('Clear Actions', 'Are you sure you want to clear all actions?')) { timelineRef.current?.clearAll(); setShowConfigMenu(false); } }}>Clear all actions</button>
                                        </div>
                                    </div>
                                )}
                            </div>
                        </div>
                    </div>
                </div>
                <div className="modal-body">
                    <div className="macro-elements-list" style={{ padding: 0, height: '100%', overflow: 'hidden' }}>
                        <MacroTimelineEditor 
                            ref={timelineRef}
                            initialElements={initialMacro.elements || []}
                            macros={macros}
                            onChange={setElements}
                            isRecording={isRecording}
                            recordDelay={recordDelay}
                            maxEvents={maxEvents}
                        />
                    </div>
                </div>
                <div className="modal-footer">
                    <button className="btn" onClick={onClose}>Cancel</button>
                    <button className="btn btn-success" onClick={() => {
                        const filteredElements = elements.filter(el => el.type !== 'sleep' || el.duration > 0);
                        onSave({ ...initialMacro, ...macroConfig, name, elements: filteredElements });
                    }}>Save</button>
                </div>
                {isModeModalOpen && <MacroModeModal macro={{ ...initialMacro, ...macroConfig }} onSave={(m) => setMacroConfig({ execMode: m.execMode, stackMax: m.stackMax, repeatCount: m.repeatCount })} onClose={() => setIsModeModalOpen(false)} />}
                {isKeyModalOpen && <SearchableKeyModal currentValue={0} macros={macros} onSelect={handleSelectKey} onClose={() => setIsKeyModalOpen(false)} />}
            </div>
        </div>,
        document.body
    );
}
