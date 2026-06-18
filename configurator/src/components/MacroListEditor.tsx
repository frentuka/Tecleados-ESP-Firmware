import React, { useState, useEffect, useRef, useImperativeHandle, forwardRef } from 'react';
import type { MacroElement, Macro, MacroAction } from '../types/macros';
import { getKeyName, MACRO_BASE, BROWSER_CODE_TO_HID } from '../KeyDefinitions';
import { ActionTapIcon, ActionPressIcon, ActionReleaseIcon, MoonIcon } from './Icons';

export interface MacroListRef {
    addKeyBlock: (key: number) => void;
    addSleepBlock: (duration: number) => void;
    clearAll: () => void;
    setEditingIndex: (index: number | null) => void;
    getEditingIndex: () => number | null;
    commitKeyEdit: (key: number) => void;
}

interface MacroListEditorProps {
    elements: MacroElement[];
    macros: Macro[];
    onChange: (elements: MacroElement[]) => void;
    isRecording: boolean;
    recordDelay: boolean;
    maxEvents?: number;
    defaultPressTime: number;
    defaultDelay: number;
    onRequestKeyModal: (index: number | null) => void;
}

const MacroListEditor = forwardRef<MacroListRef, MacroListEditorProps>(({
    elements,
    macros,
    onChange,
    isRecording,
    recordDelay,
    maxEvents,
    defaultPressTime,
    defaultDelay,
    onRequestKeyModal
}, ref) => {
    const setElements = (updater: any) => {
        const nextElements = typeof updater === 'function' ? updater(elements) : updater;
        onChange(nextElements);
    };

    const [draggedIndex, setDraggedIndex] = useState<number | null>(null);
    const [dragOverIndex, setDragOverIndex] = useState<number | null>(null);
    const [editingIndex, setEditingIndex] = useState<number | null>(null);
    const listEndRef = useRef<HTMLDivElement>(null);

    // Scroll to bottom on new item (if not dragging)
    const prevLenRef = useRef(elements.length);
    useEffect(() => {
        if (listEndRef.current && elements.length > prevLenRef.current && draggedIndex === null) {
            listEndRef.current.scrollIntoView({ behavior: 'smooth', block: 'end' });
        }
        prevLenRef.current = elements.length;
    }, [elements.length, draggedIndex]);

    useImperativeHandle(ref, () => ({
        addKeyBlock: (key: number) => {
            const isMacro = key >= MACRO_BASE && key < MACRO_BASE + 256;
            const newEl: MacroElement = { type: 'key', key };
            if (!isMacro) {
                newEl.pressTime = defaultPressTime;
            }
            setElements(prev => [...prev, newEl]);
        },
        addSleepBlock: (duration: number) => {
            setElements(prev => [...prev, { type: 'sleep', duration }]);
        },
        clearAll: () => {
            setElements([]);
        },
        setEditingIndex: (index: number | null) => {
            setEditingIndex(index);
        },
        getEditingIndex: () => editingIndex,
        commitKeyEdit: (key: number) => {
            if (editingIndex !== null) {
                setElements(prev => {
                    const newElements = [...prev];
                    const oldEl = newElements[editingIndex];
                    newElements[editingIndex] = { ...oldEl, type: 'key', key };
                    return newElements;
                });
                setEditingIndex(null);
            }
        }
    }));

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
                    if (maxEvents !== undefined && prev.length >= maxEvents) return prev;
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
                    if (maxEvents !== undefined && prev.length >= maxEvents) return prev;
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
    }, [isRecording, recordDelay, maxEvents]);

    const removeElement = (index: number) => {
        setElements(prev => prev.filter((_, i) => i !== index));
    };

    const MAX_DELAY = 4294967295;

    const updateSleep = (index: number, duration: number) => {
        setElements(prev => {
            const newElements = [...prev];
            newElements[index] = { type: 'sleep', duration: Math.min(MAX_DELAY, Math.max(0, duration)) };
            return newElements;
        });
    };

    const toggleAction = (index: number) => {
        setElements(prev => {
            const el = prev[index];
            if (el.type !== 'key') return prev;
            if (el.key >= MACRO_BASE && el.key < MACRO_BASE + 256) return prev;
            const currentAction = el.action || 'tap';
            let nextAction: MacroAction = 'tap';
            if (currentAction === 'tap') nextAction = 'press';
            else if (currentAction === 'press') nextAction = 'release';
            else nextAction = 'tap';
            const newElements = [...prev];
            const updatedEl: any = { ...el, action: nextAction };
            if (nextAction === 'tap') {
                updatedEl.pressTime = el.pressTime !== undefined ? el.pressTime : defaultPressTime;
            } else {
                delete updatedEl.pressTime;
            }
            newElements[index] = updatedEl;
            return newElements;
        });
    };

    const toggleInlineSleep = (index: number) => {
        setElements(prev => {
            const newElements = [...prev];
            const el = { ...newElements[index] };
            if (el.type !== 'key') return prev;
            if (el.inlineSleep !== undefined) {
                delete el.inlineSleep;
            } else {
                el.inlineSleep = Math.min(MAX_DELAY, defaultDelay);
            }
            newElements[index] = el;
            return newElements;
        });
    };

    const updateInlineSleep = (index: number, duration: number) => {
        setElements(prev => {
            const newElements = [...prev];
            const el = { ...newElements[index] };
            if (el.type !== 'key') return prev;
            el.inlineSleep = Math.min(MAX_DELAY, Math.max(0, duration));
            newElements[index] = el;
            return newElements;
        });
    };

    const updatePressTime = (index: number, duration: number) => {
        setElements(prev => {
            const newElements = [...prev];
            const el = { ...newElements[index] };
            if (el.type !== 'key') return prev;
            el.pressTime = Math.min(MAX_DELAY, Math.max(0, duration));
            newElements[index] = el;
            return newElements;
        });
    };

    const handleDragStart = (e: React.DragEvent, index: number) => {
        setDraggedIndex(index);
        e.dataTransfer.effectAllowed = 'move';
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
        setElements(prev => {
            const newElements = [...prev];
            const draggedItem = newElements[draggedIndex];
            const targetItem = newElements[targetIndex];
            if (draggedItem.type === 'sleep' && targetItem.type === 'key') {
                newElements[targetIndex] = { ...targetItem, inlineSleep: draggedItem.duration || 10 };
                newElements.splice(draggedIndex, 1);
                return newElements;
            } else {
                newElements.splice(draggedIndex, 1);
                newElements.splice(targetIndex, 0, draggedItem);
                return newElements;
            }
        });
        setDraggedIndex(null);
        setDragOverIndex(null);
    };

    const duplicateElement = (index: number) => {
        setElements(prev => {
            const newElements = [...prev];
            const elementToDuplicate = JSON.parse(JSON.stringify(newElements[index]));
            newElements.splice(index + 1, 0, elementToDuplicate);
            return newElements;
        });
    };

    const handleDragEnd = () => {
        setDraggedIndex(null);
        setDragOverIndex(null);
    };

    return (
        <>
            {elements.length === 0 ? <div className="empty-state">No actions added yet.</div> : elements.map((el, i) => (
                <div key={i} className={`macro-element-row ${draggedIndex === i ? 'dragging' : ''} ${dragOverIndex === i && draggedIndex !== null && draggedIndex !== i ? (draggedIndex > i ? 'drag-over-top' : 'drag-over-bottom') : ''}`} draggable="true" onDragStart={(e) => handleDragStart(e, i)} onDragOver={(e) => handleDragOver(e, i)} onDragEnd={handleDragEnd} onDrop={(e) => handleDrop(e, i)}>
                    <div className="element-content">
                        {el.type === 'key' ? (
                            <>
                                <button className={`btn-action-toggle ${(el.key >= MACRO_BASE && el.key < MACRO_BASE + 256) ? 'disabled' : ''}`} onClick={() => toggleAction(i)} disabled={el.key >= MACRO_BASE && el.key < MACRO_BASE + 256}>
                                    {(!el.action || el.action === 'tap') && <ActionTapIcon />}
                                    {el.action === 'press' && <ActionPressIcon />}
                                    {el.action === 'release' && <ActionReleaseIcon />}
                                </button>
                                <div className="key-preview" onClick={() => { setEditingIndex(i); onRequestKeyModal(i); }}>{getKeyName(el.key, macros)}</div>
                                {(!el.action || el.action === 'tap') && !(el.key >= MACRO_BASE && el.key < MACRO_BASE + 256) && (
                                    <div className="press-time-container">
                                        <span>Press for</span>
                                        <input type="number" value={el.pressTime === 0 ? '' : (el.pressTime ?? defaultPressTime)} onChange={e => updatePressTime(i, e.target.value === '' ? 0 : parseInt(e.target.value) || 0)} min="0" className="press-time-input" />
                                        <span>ms</span>
                                    </div>
                                )}
                                <div className={`inline-sleep-container ${el.inlineSleep !== undefined ? 'expanded' : ''}`}>
                                    <div className="inline-sleep-inner" title={el.inlineSleep !== undefined ? `Then, do nothing for ${el.inlineSleep} milliseconds` : 'Add delay after this action'}>
                                        <div className="inline-sleep-fields">
                                            <input type="number" value={el.inlineSleep === 0 ? '' : (el.inlineSleep ?? '')} onChange={e => updateInlineSleep(i, e.target.value === '' ? 0 : parseInt(e.target.value) || 0)} min="0" className="inline-sleep-input" />
                                            <span className="inline-sleep-suffix" style={{ fontSize: '0.75em', opacity: 0.8 }}>ms</span>
                                        </div>
                                        <button className={`btn-action-toggle btn-moon-toggle ${el.inlineSleep !== undefined ? 'active' : ''}`} onClick={() => toggleInlineSleep(i)}>
                                            <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                                                <circle cx="12" cy="12" r="10"></circle>
                                                <polyline points="12 6 12 12 16 14"></polyline>
                                            </svg>
                                        </button>
                                    </div>
                                </div>
                            </>
                        ) : (
                            <div className="sleep-preview" title={`Then, do nothing for ${el.duration} milliseconds`}>
                                <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ opacity: 0.7 }}>
                                    <circle cx="12" cy="12" r="10"></circle>
                                    <polyline points="12 6 12 12 16 14"></polyline>
                                </svg>
                                <span>Sleep</span>
                                <input type="number" value={el.duration === 0 ? '' : el.duration} onChange={e => updateSleep(i, e.target.value === '' ? 0 : parseInt(e.target.value) || 0)} min="0" />
                                <span className="inline-sleep-suffix" style={{ fontSize: '0.75em', opacity: 0.8 }}>ms</span>
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
            ))}
            <div ref={listEndRef} />
        </>
    );
});

export default React.memo(MacroListEditor);
