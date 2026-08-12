import { useState } from 'react';
import { createPortal } from 'react-dom';
import type { Macro } from '../types/macros';
import type { CustomKey } from '../types/customKeys';
import type { Combo } from '../types/combos';
import '../assets/css/import-export.css';

export interface GlobalExportData {
    layers: number[];
    layerNames?: Record<number, string>;
    hasPhysicalLayout: boolean;
    macros: Macro[];
    customKeys: CustomKey[];
    combos: Combo[];
}

export interface GlobalExportSelection {
    layers: number[];

    macroIds: number[];
    customKeyIds: number[];
    comboIds: number[];
}

interface GlobalExportModalProps {
    data: GlobalExportData;
    onClose: () => void;
    onExport: (selection: GlobalExportSelection) => void;
    isExporting: boolean;
}

export default function GlobalExportModal({ data, onClose, onExport, isExporting }: GlobalExportModalProps) {
    const [selectedLayers, setSelectedLayers] = useState<Set<number>>(new Set(data.layers));
    const [selectedMacros, setSelectedMacros] = useState<Set<number>>(new Set(data.macros.map(m => m.id)));
    const [selectedCKeys, setSelectedCKeys] = useState<Set<number>>(new Set(data.customKeys.map(c => c.id)));
    const [selectedCombos, setSelectedCombos] = useState<Set<number>>(new Set(data.combos.map(c => c.id)));

    type ViewState = 'grid' | 'layers' | 'macros' | 'ckeys' | 'combos';
    type CheckState = 'all' | 'partial' | 'none';

    const [activeView, setActiveView] = useState<ViewState>('grid');

    const getCheckState = (count: number, total: number): CheckState => {
        if (total === 0) return 'none';
        if (count === total) return 'all';
        if (count > 0) return 'partial';
        return 'none';
    };

    const toggleSetItem = (set: Set<number>, setter: React.Dispatch<React.SetStateAction<Set<number>>>, id: number) => {
        if (isExporting) return;
        const next = new Set(set);
        if (next.has(id)) next.delete(id);
        else next.add(id);
        setter(next);
    };

    const totalSelected = selectedLayers.size + selectedMacros.size + selectedCKeys.size + selectedCombos.size;
    const totalItems = data.layers.length + data.macros.length + data.customKeys.length + data.combos.length;

    const handleSelectAllAll = () => {
        if (isExporting) return;
        setSelectedLayers(new Set(data.layers));
        setSelectedMacros(new Set(data.macros.map(m => m.id)));
        setSelectedCKeys(new Set(data.customKeys.map(c => c.id)));
        setSelectedCombos(new Set(data.combos.map(c => c.id)));
    };

    const handleDeselectAll = () => {
        if (isExporting) return;
        setSelectedLayers(new Set());
        setSelectedMacros(new Set());
        setSelectedCKeys(new Set());
        setSelectedCombos(new Set());
    };

    const handleSectionToggle = (e: React.MouseEvent, type: 'layers'|'macros'|'ckeys'|'combos', isChecked: boolean) => {
        e.stopPropagation();
        if (isExporting) return;
        if (type === 'layers') setSelectedLayers(isChecked ? new Set() : new Set(data.layers));
        if (type === 'macros') setSelectedMacros(isChecked ? new Set() : new Set(data.macros.map(m => m.id)));
        if (type === 'ckeys') setSelectedCKeys(isChecked ? new Set() : new Set(data.customKeys.map(c => c.id)));
        if (type === 'combos') setSelectedCombos(isChecked ? new Set() : new Set(data.combos.map(c => c.id)));
    };

    const CustomCheckbox = ({ state, onClick }: { state: CheckState | boolean, onClick: (e: React.MouseEvent) => void }) => {
        const isAll = state === 'all' || state === true;
        const isPartial = state === 'partial';
        return (
            <button className={`ie-checkbox ${isAll ? 'checked' : isPartial ? 'partial' : ''}`} onClick={onClick} type="button">
                {isAll && (
                    <svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
                        <polyline points="20 6 9 17 4 12" />
                    </svg>
                )}
                {isPartial && (
                    <svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
                        <line x1="5" y1="12" x2="19" y2="12" />
                    </svg>
                )}
            </button>
        );
    };

    const renderGridCard = (id: ViewState, title: string, count: number, total: number) => {
        const state = getCheckState(count, total);
        const isDisabled = total === 0;
        return (
            <div className={`ie-category-card ${isDisabled ? 'disabled' : ''}`} onClick={isDisabled ? undefined : () => setActiveView(id)}>
                {!isDisabled && <CustomCheckbox state={state} onClick={(e) => handleSectionToggle(e, id as any, state === 'all')} />}
                <div className="ie-category-title">{title}</div>
                <div className="ie-category-count">{isDisabled ? 'No items available' : `${count} / ${total} selected`}</div>
            </div>
        );
    };

    const renderDetailView = (id: ViewState, title: string, count: number, total: number, content: React.ReactNode) => {
        const state = getCheckState(count, total);
        return (
            <div className="ie-detail-container">
                <div className="ie-detail-header">
                    <button className="btn-icon-ghost" style={{ padding: '8px' }} onClick={() => setActiveView('grid')} title="Back">
                        <svg viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                            <line x1="19" y1="12" x2="5" y2="12" />
                            <polyline points="12 19 5 12 12 5" />
                        </svg>
                    </button>
                    <div className="ie-detail-title">{title} <span style={{ opacity: 0.5, fontWeight: 'normal', fontSize: '0.85em', marginLeft: '6px' }}>({count}/{total})</span></div>
                    <CustomCheckbox state={state} onClick={(e) => handleSectionToggle(e, id as any, state === 'all')} />
                </div>
                <div className="ie-detail-content">
                    {content}
                </div>
            </div>
        );
    };

    return createPortal(
        <div className="modal-overlay" onClick={isExporting ? undefined : onClose}>
            <div className="ie-modal-content" onClick={e => e.stopPropagation()}>
                <div className="ie-header">
                    <div className="ie-header-left">
                        <CustomCheckbox state={getCheckState(totalSelected, totalItems)} onClick={getCheckState(totalSelected, totalItems) === 'all' ? handleDeselectAll : handleSelectAllAll} />
                        <button className="btn-icon-ghost" style={{ padding: '4px' }} onClick={handleDeselectAll} title="Deselect All">
                            <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                                <circle cx="12" cy="12" r="10" />
                                <line x1="15" y1="9" x2="9" y2="15" />
                                <line x1="9" y1="9" x2="15" y2="15" />
                            </svg>
                        </button>
                    </div>
                    <h2 className="ie-header-title">Export Configuration</h2>
                    <button className="ie-header-close" onClick={onClose} disabled={isExporting}>&times;</button>
                </div>

                {activeView === 'grid' && (
                    <div className="ie-grid-container">
                        {renderGridCard('layers', 'Layers', selectedLayers.size, data.layers.length)}
                        {renderGridCard('macros', 'Macros', selectedMacros.size, data.macros.length)}
                        {renderGridCard('ckeys', 'Custom Keys', selectedCKeys.size, data.customKeys.length)}
                        {renderGridCard('combos', 'Combos', selectedCombos.size, data.combos.length)}
                    </div>
                )}
                
                {activeView === 'layers' && renderDetailView('layers', 'Layers', selectedLayers.size, data.layers.length,
                    <div className="ie-item-list">
                        {data.layers.length === 0 && <div className="ie-empty-state">No layers found</div>}
                        {data.layers.map(l => {
                            const layerName = data.layerNames?.[l] || `Layer ${l}`;
                            return (
                            <div key={l} className={`ie-item ${selectedLayers.has(l) ? 'selected' : ''}`} onClick={() => toggleSetItem(selectedLayers, setSelectedLayers, l)}>
                                <CustomCheckbox state={selectedLayers.has(l)} onClick={(e) => { e.stopPropagation(); toggleSetItem(selectedLayers, setSelectedLayers, l); }} />
                                <div className="ie-item-info">
                                    <div className="ie-item-name">{layerName}</div>
                                </div>
                            </div>
                        )})}
                    </div>
                )}

                {activeView === 'macros' && renderDetailView('macros', 'Macros', selectedMacros.size, data.macros.length,
                    <div className="ie-item-list">
                        {data.macros.length === 0 && <div className="ie-empty-state">No macros found</div>}
                        {data.macros.map(m => (
                            <div key={m.id} className={`ie-item ${selectedMacros.has(m.id) ? 'selected' : ''}`} onClick={() => toggleSetItem(selectedMacros, setSelectedMacros, m.id)}>
                                <CustomCheckbox state={selectedMacros.has(m.id)} onClick={(e) => { e.stopPropagation(); toggleSetItem(selectedMacros, setSelectedMacros, m.id); }} />
                                <div className="ie-item-info">
                                    <div className="ie-item-name">{m.name || `Macro #${m.id}`}</div>
                                    <div className="ie-item-desc">{m.elements?.length || 0} actions</div>
                                </div>
                            </div>
                        ))}
                    </div>
                )}

                {activeView === 'ckeys' && renderDetailView('ckeys', 'Custom Keys', selectedCKeys.size, data.customKeys.length,
                    <div className="ie-item-list">
                        {data.customKeys.length === 0 && <div className="ie-empty-state">No custom keys found</div>}
                        {data.customKeys.map(c => (
                            <div key={c.id} className={`ie-item ${selectedCKeys.has(c.id) ? 'selected' : ''}`} onClick={() => toggleSetItem(selectedCKeys, setSelectedCKeys, c.id)}>
                                <CustomCheckbox state={selectedCKeys.has(c.id)} onClick={(e) => { e.stopPropagation(); toggleSetItem(selectedCKeys, setSelectedCKeys, c.id); }} />
                                <div className="ie-item-info">
                                    <div className="ie-item-name">{c.name || `Custom Key #${c.id}`}</div>
                                    <div className="ie-item-desc">{c.mode === 0 ? 'Press/Release' : 'Multi-Action'}</div>
                                </div>
                            </div>
                        ))}
                    </div>
                )}

                {activeView === 'combos' && renderDetailView('combos', 'Combos', selectedCombos.size, data.combos.length,
                    <div className="ie-item-list">
                        {data.combos.length === 0 && <div className="ie-empty-state">No combos found</div>}
                        {data.combos.map(c => (
                            <div key={c.id} className={`ie-item ${selectedCombos.has(c.id) ? 'selected' : ''}`} onClick={() => toggleSetItem(selectedCombos, setSelectedCombos, c.id)}>
                                <CustomCheckbox state={selectedCombos.has(c.id)} onClick={(e) => { e.stopPropagation(); toggleSetItem(selectedCombos, setSelectedCombos, c.id); }} />
                                <div className="ie-item-info">
                                    <div className="ie-item-name">{c.name || `Combo #${c.id}`}</div>
                                    <div className="ie-item-desc">{c.keys.length} keys</div>
                                </div>
                            </div>
                        ))}
                    </div>
                )}

                <div className="ie-footer">
                    <div className="ie-footer-actions">
                        <button className="btn btn-secondary" onClick={onClose} disabled={isExporting}>Cancel</button>
                        <button className="btn btn-success" disabled={totalSelected === 0 || isExporting} onClick={() => {
                            onExport({
                                layers: Array.from(selectedLayers),
                                macroIds: Array.from(selectedMacros),
                                customKeyIds: Array.from(selectedCKeys),
                                comboIds: Array.from(selectedCombos)
                            });
                        }}>
                            {isExporting ? 'Exporting...' : 'Export'}
                        </button>
                    </div>
                </div>
            </div>
        </div>,
        document.body
    );
}
