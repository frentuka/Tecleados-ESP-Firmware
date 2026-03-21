import { useState } from 'react';
import { createPortal } from 'react-dom';
import type { Macro } from '../types/macros';

interface ExportModalProps {
    macros: Macro[];
    onClose: () => void;
    onExport: (selectedMacros: Macro[]) => void;
    isExporting: boolean;
}

export default function ExportModal({ macros, onClose, onExport, isExporting }: ExportModalProps) {
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
