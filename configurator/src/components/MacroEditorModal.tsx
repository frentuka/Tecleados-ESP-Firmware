import React, { useState, useEffect, useRef, useCallback } from 'react';
import { createPortal } from 'react-dom';
import type { Macro, MacroElement } from '../types/macros';
import SearchableKeyModal from './SearchableKeyModal';
import { useConfirm } from '../hooks/useConfirm';
import MacroModeModal from './MacroModeModal';
import MacroTimelineEditor, { type MacroTimelineRef } from './timeline/MacroTimelineEditor';
import MacroListEditor, { type MacroListRef } from './MacroListEditor';

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
    const [isModeModalOpen, setIsModeModalOpen] = useState(false);
    const [macroConfig, setMacroConfig] = useState<{ execMode: number, stackMax: number, repeatCount: number }>({
        execMode: initialMacro?.execMode || 1,
        stackMax: initialMacro?.stackMax || 0,
        repeatCount: initialMacro?.repeatCount || 0
    });
    const [elements, setElements] = useState<MacroElement[]>(initialMacro.elements || []);
    const [isKeyModalOpen, setIsKeyModalOpen] = useState(false);

    // View mode state
    const [viewMode, setViewMode] = useState<'timeline' | 'list'>('timeline');

    // Ref works for both
    const listEditorRef = useRef<any>(null);
    const timelineEditorRef = useRef<any>(null);
    const editorRef = {
        get current() {
            return viewMode === 'list' ? listEditorRef.current : timelineEditorRef.current;
        }
    };

    const [isRecording, setIsRecording] = useState(false);
    const [recordDelay, setRecordDelay] = useState(true);
    const [clearOnRecord, setClearOnRecord] = useState(false);
    const [defaultDelay, setDefaultDelay] = useState(100);
    const [defaultPressTime, setDefaultPressTime] = useState(10);
    const [showConfigMenu, setShowConfigMenu] = useState(false);
    const configMenuRef = useRef<HTMLDivElement>(null);

    const isAtEventLimit = maxEvents !== undefined && elements.length >= maxEvents;

    // Defer rendering of heavy list/timeline components so the modal opens instantly
    const [isReady, setIsReady] = useState(false);
    useEffect(() => {
        const timer = setTimeout(() => setIsReady(true), 10);
        return () => clearTimeout(timer);
    }, []);

    const contentRef = useRef<HTMLDivElement>(null);
    const prevViewRef = useRef(viewMode);
    
    React.useLayoutEffect(() => {
        if (prevViewRef.current !== viewMode && contentRef.current) {
            const el = contentRef.current;
            // Record old height
            const oldHeight = el.getBoundingClientRect().height;
            
            // Turn off transitions to measure new height
            el.style.transition = 'none';
            el.style.height = 'auto';
            const newHeight = el.getBoundingClientRect().height;
            
            // Set back to old height
            el.style.height = `${oldHeight}px`;
            
            // Force reflow
            void el.offsetHeight;
            
            // Turn on transition and animate to new height
            el.style.transition = 'height 0.3s cubic-bezier(0.2, 0.8, 0.2, 1)';
            el.style.height = `${newHeight}px`;
            
            const timer = setTimeout(() => {
                if (contentRef.current) {
                    contentRef.current.style.transition = 'none';
                    contentRef.current.style.height = 'auto';
                }
            }, 300);
            
            prevViewRef.current = viewMode;
            return () => clearTimeout(timer);
        }
    }, [viewMode]);

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

    const addKey = () => setIsKeyModalOpen(true);

    const handleSelectKey = (key: number) => {
        if (viewMode === 'list') {
            const listRef = editorRef.current as MacroListRef;
            if (listRef.getEditingIndex && listRef.getEditingIndex() !== null) {
                listRef.commitKeyEdit(key);
            } else {
                editorRef.current?.addKeyBlock(key);
            }
        } else {
            editorRef.current?.addKeyBlock(key);
        }
        setIsKeyModalOpen(false);
    };

    const handleRequestKeyModal = useCallback(() => {
        setIsKeyModalOpen(true);
    }, []);

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
            <div className="modal-content macro-editor-modal" ref={contentRef} onClick={e => e.stopPropagation()} style={{ maxWidth: '700px', width: '90%' }}>
                <div className="modal-header macro-modal-header">
                    <div className="macro-name-container"><MacroNameInput initialName={name} onChange={setName} /></div>

                    <div className="macro-editor-actions-header">
                        <div style={{ display: 'flex', gap: '8px', alignItems: 'center' }}>
                            {/* View Toggle */}
                            <div className="btn-group" style={{ display: 'flex', backgroundColor: 'var(--bg-panel, #2a2a35)', borderRadius: '4px', overflow: 'hidden', border: '1px solid var(--border-color, #333)', marginRight: '8px' }}>
                                <button
                                    className="btn-icon-sm"
                                    style={{
                                        padding: '6px 10px',
                                        width: 'auto',
                                        height: 'auto',
                                        backgroundColor: viewMode === 'timeline' ? 'var(--accent-color)' : 'transparent',
                                        color: viewMode === 'timeline' ? '#fff' : '#888',
                                        border: 'none',
                                        cursor: 'pointer',
                                        display: 'flex',
                                        alignItems: 'center',
                                        justifyContent: 'center'
                                    }}
                                    onClick={() => setViewMode('timeline')}
                                    title="Timeline View"
                                >
                                    <svg viewBox="0 0 24 24" width="16" height="16" fill="currentColor">
                                        <rect x="2" y="5" width="10" height="4" rx="1" />
                                        <rect x="8" y="11" width="8" height="4" rx="1" />
                                        <rect x="14" y="17" width="8" height="4" rx="1" />
                                    </svg>
                                </button>
                                <button
                                    className="btn-icon-sm"
                                    style={{
                                        padding: '6px 10px',
                                        width: 'auto',
                                        height: 'auto',
                                        backgroundColor: viewMode === 'list' ? 'var(--accent-color)' : 'transparent',
                                        color: viewMode === 'list' ? '#fff' : '#888',
                                        border: 'none',
                                        cursor: 'pointer',
                                        display: 'flex',
                                        alignItems: 'center',
                                        justifyContent: 'center'
                                    }}
                                    onClick={() => setViewMode('list')}
                                    title="List View"
                                >
                                    <svg viewBox="0 0 24 24" width="16" height="16" fill="currentColor">
                                        <rect x="3" y="4" width="18" height="3" rx="1" />
                                        <rect x="3" y="10.5" width="18" height="3" rx="1" />
                                        <rect x="3" y="17" width="18" height="3" rx="1" />
                                    </svg>
                                </button>
                            </div>

                            <button className="btn btn-secondary btn-sm" onClick={addKey} disabled={isAtEventLimit} title={isAtEventLimit ? `Maximum actions reached (${maxEvents})` : 'Add Action'} style={{ display: 'flex', alignItems: 'center', gap: '4px' }}>
                                <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                                    <line x1="12" y1="5" x2="12" y2="19" />
                                    <line x1="5" y1="12" x2="19" y2="12" />
                                </svg>
                                <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                                    <rect x="3" y="3" width="18" height="18" rx="2" ry="2" />
                                    <path d="M15 17L12 7L9 17" />
                                    <path d="M10.5 14.5H13.5" />
                                </svg>
                            </button>
                            <button className="btn btn-secondary btn-sm" onClick={() => editorRef.current?.addSleepBlock(defaultDelay || 100)} disabled={isAtEventLimit} title={isAtEventLimit ? `Maximum actions reached (${maxEvents})` : 'Add Delay'} style={{ display: 'flex', alignItems: 'center', gap: '4px' }}>
                                <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                                    <line x1="12" y1="5" x2="12" y2="19" />
                                    <line x1="5" y1="12" x2="19" y2="12" />
                                </svg>
                                <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                                    <path d="M5 20h14" />
                                    <path d="M5 4h14" />
                                    <path d="M17 20v-2.172a2 2 0 0 0-.586-1.414L12 12l-4.414 4.414A2 2 0 0 0 7 17.828V20" />
                                    <path d="M7 4v2.172a2 2 0 0 0 .586 1.414L12 12l4.414 -4.414A2 2 0 0 0 17 6.172V4" />
                                </svg>
                            </button>
                            <button
                                className="btn btn-sm"
                                onClick={() => { if (!isRecording && clearOnRecord) { setElements([]); editorRef.current?.clearAll(); } setIsRecording(!isRecording); }}
                                style={{ backgroundColor: 'var(--danger-color)', color: 'white', display: 'flex', alignItems: 'center', justifyContent: 'center', padding: '6px 10px' }}
                                title={isRecording ? 'Stop Recording' : 'Start Recording'}
                            >
                                {isRecording ? (
                                    <svg viewBox="0 0 24 24" width="16" height="16" fill="currentColor"><rect x="6" y="6" width="12" height="12" rx="2" ry="2" /></svg>
                                ) : (
                                    <svg viewBox="0 0 24 24" width="16" height="16" fill="currentColor"><circle cx="12" cy="12" r="6" /></svg>
                                )}
                            </button>
                            <div className="add-action-dropdown" ref={configMenuRef} style={{ position: 'relative' }}>
                                <button className={`btn-icon btn-icon-ghost ${showConfigMenu ? 'active' : ''}`} onClick={() => setShowConfigMenu(!showConfigMenu)} title="Config" style={{ width: '30px', height: '30px', display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
                                    <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
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
                                            <button className="btn btn-sm btn-danger" style={{ width: '100%', padding: '0.4rem' }} onClick={async () => { if (await confirm('Clear Actions', 'Are you sure you want to clear all actions?')) { setElements([]); editorRef.current?.clearAll(); setShowConfigMenu(false); } }}>Clear all actions</button>
                                        </div>
                                    </div>
                                )}
                            </div>
                        </div>
                    </div>
                </div>
                <style>{`
                    @keyframes viewFadeIn {
                        from { opacity: 0; }
                        to { opacity: 1; }
                    }
                    @keyframes spinFade {
                        0% { transform: rotate(0deg); opacity: 0.5; }
                        50% { opacity: 1; }
                        100% { transform: rotate(360deg); opacity: 0.5; }
                    }
                `}</style>
                <div className="modal-body" style={{ display: 'flex', flexDirection: 'column', flex: 1, overflow: 'hidden' }}>
                    {!isReady ? (
                        <div style={{ flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center', color: 'var(--text-secondary)' }}>
                            <div style={{ width: '32px', height: '32px', border: '3px solid rgba(255,255,255,0.05)', borderTopColor: 'var(--accent-color)', borderRadius: '50%', animation: 'spinFade 1s linear infinite' }} />
                        </div>
                    ) : (
                        <>
                            <div className="macro-elements-list" style={{ display: viewMode === 'list' ? 'flex' : 'none', flex: 1, overflowY: 'auto', animation: 'viewFadeIn 0.2s ease-out' }}>
                                <MacroListEditor
                                    ref={listEditorRef as any}
                                    elements={elements}
                                    macros={macros}
                                    onChange={setElements}
                                    isRecording={isRecording}
                                    recordDelay={recordDelay}
                                    maxEvents={maxEvents}
                                    defaultDelay={defaultDelay}
                                    defaultPressTime={defaultPressTime}
                                    onRequestKeyModal={handleRequestKeyModal}
                                />
                            </div>
                            <div className="macro-elements-list" style={{ display: viewMode === 'timeline' ? 'flex' : 'none', padding: 0, flex: 1, overflow: 'hidden', animation: 'viewFadeIn 0.25s cubic-bezier(0.2, 0.8, 0.2, 1)' }}>
                                <MacroTimelineEditor
                                    ref={timelineEditorRef as any}
                                    elements={elements}
                                    macros={macros}
                                    onChange={setElements}
                                    isRecording={isRecording}
                                    recordDelay={recordDelay}
                                    maxEvents={maxEvents}
                                    isActiveView={viewMode === 'timeline'}
                                />
                            </div>
                        </>
                    )}
                </div>
                <div className="modal-footer">
                    <button className="btn" onClick={onClose}>Cancel</button>
                    <button className="btn btn-success" onClick={() => {
                        const filteredElements = elements.filter(el => el.type !== 'sleep' || el.duration > 0);
                        onSave({ ...initialMacro, ...macroConfig, name, elements: filteredElements });
                    }}>Save</button>
                </div>
                {isModeModalOpen && <MacroModeModal macro={{ ...initialMacro, ...macroConfig }} onSave={(m) => setMacroConfig({ execMode: m.execMode, stackMax: m.stackMax, repeatCount: m.repeatCount })} onClose={() => setIsModeModalOpen(false)} />}
                {isKeyModalOpen && (
                    <SearchableKeyModal
                        currentValue={0}
                        macros={macros}
                        onSelect={handleSelectKey}
                        onClose={() => {
                            setIsKeyModalOpen(false);
                            if (viewMode === 'list') {
                                (editorRef.current as MacroListRef).setEditingIndex?.(null);
                            }
                        }}
                    />
                )}
            </div>
        </div>,
        document.body
    );
}
