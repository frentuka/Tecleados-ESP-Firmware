import React, { useState, useEffect } from 'react';
import { createPortal } from 'react-dom';
import type { ImportableMacro } from '../types/macros';

interface ImportModalProps {
    macros: ImportableMacro[];
    maxMacros: number;
    currentCount: number;
    onClose: () => void;
    onImport: (selectedMacros: ImportableMacro[]) => void;
    isImporting: boolean;
}

export default function ImportModal({ macros, maxMacros, currentCount, onClose, onImport, isImporting }: ImportModalProps) {
    const [selectedTempIds, setSelectedTempIds] = useState<Set<string>>(new Set());

    useEffect(() => {
        if (selectedTempIds.size === 0 && macros.length > 0 && !isImporting) {
            setSelectedTempIds(new Set(macros.map(m => m.tempId)));
        }
    }, [macros, isImporting]);

    const allowedToImport = Math.max(0, maxMacros - currentCount);
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
