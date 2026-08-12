import { useState, useRef, useEffect } from 'react';
import { createPortal } from 'react-dom';
import type { Macro } from '../types/macros';
import type { CustomKey } from '../types/customKeys';
import type { Combo } from '../types/combos';
import '../assets/css/import-export.css';

export interface GlobalImportData {
    layers: number[];
    layerNames?: Record<number, string>;
    hasPhysicalLayout: boolean;
    macros: Macro[];
    customKeys: CustomKey[];
    combos: Combo[];
}

export interface GlobalImportSelection {
    layers: number[];
    layerMapping: Record<number, number | null>;
    includePhysicalLayout: boolean;
    macros: Macro[];
    macroMapping: Record<number, number | null>;
    customKeys: CustomKey[];
    ckeyMapping: Record<number, number | null>;
    combos: Combo[];
    comboMapping: Record<number, number | null>;
}

interface GlobalImportModalProps {
    data: GlobalImportData;
    existingData: {
        layers: { id: number, name: string }[];
        macros: Macro[];
        customKeys: CustomKey[];
        combos: Combo[];
    };
    onClose: () => void;
    onImport: (selection: GlobalImportSelection) => void;
    isImporting: boolean;
    isDeveloperMode: boolean;
    limits: {
        maxMacros: number;
        currentMacros: number;
        maxCustomKeys: number;
        currentCustomKeys: number;
        maxCombos: number;
        currentCombos: number;
    };
}

export default function GlobalImportModal({ data, existingData, onClose, onImport, isImporting, isDeveloperMode, limits }: GlobalImportModalProps) {
    const [selectedLayers, setSelectedLayers] = useState<Set<number>>(new Set(data.layers));
    const [selectedMacros, setSelectedMacros] = useState<Set<number>>(new Set(data.macros.map(m => m.id)));
    const [selectedCKeys, setSelectedCKeys] = useState<Set<number>>(new Set(data.customKeys.map(c => c.id)));
    const [selectedCombos, setSelectedCombos] = useState<Set<number>>(new Set(data.combos.map(c => c.id)));

    const [layerMapping, setLayerMapping] = useState<Record<number, number | null>>({});
    const [macroMapping, setMacroMapping] = useState<Record<number, number | null>>({});
    const [ckeyMapping, setCkeyMapping] = useState<Record<number, number | null>>({});
    const [comboMapping, setComboMapping] = useState<Record<number, number | null>>({});
    
    const [includePhysicalLayout, setIncludePhysicalLayout] = useState<boolean>(false);

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
        if (isImporting) return;
        const next = new Set(set);
        if (next.has(id)) next.delete(id);
        else next.add(id);
        setter(next);
    };

    const totalSelected = selectedLayers.size + selectedMacros.size + selectedCKeys.size + selectedCombos.size;
    const totalItems = data.layers.length + data.macros.length + data.customKeys.length + data.combos.length;

    const allowedMacros = Math.max(0, limits.maxMacros - limits.currentMacros);
    const isMacrosOverLimit = selectedMacros.size > allowedMacros;

    const allowedCKeys = Math.max(0, limits.maxCustomKeys - limits.currentCustomKeys);
    const isCKeysOverLimit = selectedCKeys.size > allowedCKeys;

    const allowedCombos = Math.max(0, limits.maxCombos - limits.currentCombos);
    const isCombosOverLimit = selectedCombos.size > allowedCombos;

    const isAnyOverLimit = isMacrosOverLimit || isCKeysOverLimit || isCombosOverLimit;

    const handleSelectAllAll = () => {
        if (isImporting) return;
        setSelectedLayers(new Set(data.layers));
        setSelectedMacros(new Set(data.macros.map(m => m.id)));
        setSelectedCKeys(new Set(data.customKeys.map(c => c.id)));
        setSelectedCombos(new Set(data.combos.map(c => c.id)));
    };

    const handleDeselectAll = () => {
        if (isImporting) return;
        setSelectedLayers(new Set());
        setSelectedMacros(new Set());
        setSelectedCKeys(new Set());
        setSelectedCombos(new Set());
    };

    const handleSectionToggle = (e: React.MouseEvent, type: 'layers'|'macros'|'ckeys'|'combos', isChecked: boolean) => {
        e.stopPropagation();
        if (isImporting) return;
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

    const renderReplaceDropdown = (
        itemId: number,
        mapping: Record<number, number | null>,
        setMapping: React.Dispatch<React.SetStateAction<Record<number, number | null>>>,
        existingItems: { id: number, name: string }[]
    ) => {
        const takenIds = new Set(Object.entries(mapping).filter(([k, v]) => parseInt(k) !== itemId && v !== null).map(([_, v]) => v));
        const isAddNew = mapping[itemId] === null || mapping[itemId] === undefined;
        
        return (
            <CustomReplaceDropdown 
                value={mapping[itemId]}
                isAddNew={isAddNew}
                takenIds={takenIds}
                existingItems={existingItems}
                onChange={(val) => setMapping(prev => ({ ...prev, [itemId]: val }))}
            />
        );
    };

    const CustomReplaceDropdown = ({ 
        value, isAddNew, takenIds, existingItems, onChange 
    }: {
        value: number | null | undefined;
        isAddNew: boolean;
        takenIds: Set<number | null>;
        existingItems: { id: number, name: string }[];
        onChange: (val: number | null) => void;
    }) => {
        const [isOpen, setIsOpen] = useState(false);
        const containerRef = useRef<HTMLDivElement>(null);
        
        useEffect(() => {
            const handleClickOutside = (e: MouseEvent) => {
                if (containerRef.current && !containerRef.current.contains(e.target as Node)) {
                    setIsOpen(false);
                }
            };
            if (isOpen) document.addEventListener('mousedown', handleClickOutside);
            return () => document.removeEventListener('mousedown', handleClickOutside);
        }, [isOpen]);

        const selectedItem = existingItems.find(i => i.id === value);
        const label = isAddNew ? 'Add as new' : `Replace ${selectedItem?.name || `#${value}`}`;

        return (
            <div className={`ie-custom-dropdown ${isAddNew ? 'is-add-new' : ''} ${isOpen ? 'open' : ''}`} ref={containerRef} onClick={(e) => { e.stopPropagation(); setIsOpen(!isOpen); }}>
                <div className="ie-custom-dropdown-value">
                    {label}
                    <svg viewBox='0 0 24 24' fill='none' stroke='currentColor' strokeWidth='2' strokeLinecap='round' strokeLinejoin='round'>
                        <polyline points='6 9 12 15 18 9'></polyline>
                    </svg>
                </div>
                {isOpen && (
                    <div className="ie-custom-dropdown-menu">
                        <div className={`ie-custom-dropdown-option ${isAddNew ? 'selected' : ''}`} onClick={() => { onChange(null); setIsOpen(false); }}>
                            Add as new
                        </div>
                        {existingItems.map(item => {
                            const disabled = takenIds.has(item.id);
                            return (
                                <div 
                                    key={item.id} 
                                    className={`ie-custom-dropdown-option ${disabled ? 'disabled' : ''} ${value === item.id ? 'selected' : ''}`} 
                                    onClick={(e) => {
                                        if (disabled) { e.stopPropagation(); return; }
                                        onChange(item.id);
                                        setIsOpen(false);
                                    }}
                                >
                                    Replace {item.name || `#${item.id}`}
                                </div>
                            );
                        })}
                    </div>
                )}
            </div>
        );
    };

    const renderDetailView = (id: ViewState, title: string, count: number, total: number, content: React.ReactNode, warningNode?: React.ReactNode) => {
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
                    {warningNode}
                    <CustomCheckbox state={state} onClick={(e) => handleSectionToggle(e, id as any, state === 'all')} />
                </div>
                <div className="ie-detail-content">
                    {content}
                </div>
            </div>
        );
    };

    return createPortal(
        <div className="modal-overlay" onClick={isImporting ? undefined : onClose}>
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
                    <h2 className="ie-header-title">Import Configuration</h2>
                    <button className="ie-header-close" onClick={onClose} disabled={isImporting}>&times;</button>
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
                        {data.layers.length === 0 && <div className="ie-empty-state">No layers in file</div>}
                        {data.layers.map(l => {
                            const layerName = data.layerNames?.[l] || `Layer ${l}`;
                            return (
                                <div key={l} className={`ie-item ${selectedLayers.has(l) ? 'selected' : ''}`} onClick={() => toggleSetItem(selectedLayers, setSelectedLayers, l)}>
                                    <CustomCheckbox state={selectedLayers.has(l)} onClick={(e) => { e.stopPropagation(); toggleSetItem(selectedLayers, setSelectedLayers, l); }} />
                                    <div className="ie-item-info">
                                        <div className="ie-item-name">{layerName}</div>
                                    </div>
                                    {selectedLayers.has(l) && renderReplaceDropdown(l, layerMapping, setLayerMapping, existingData.layers)}
                                </div>
                            );
                        })}
                    </div>
                )}

                {activeView === 'macros' && renderDetailView('macros', 'Macros', selectedMacros.size, data.macros.length,
                    <div className="ie-item-list">
                        {data.macros.length === 0 && <div className="ie-empty-state">No macros in file</div>}
                        {data.macros.map(m => (
                            <div key={m.id} className={`ie-item ${selectedMacros.has(m.id) ? 'selected' : ''}`} onClick={() => toggleSetItem(selectedMacros, setSelectedMacros, m.id)}>
                                <CustomCheckbox state={selectedMacros.has(m.id)} onClick={(e) => { e.stopPropagation(); toggleSetItem(selectedMacros, setSelectedMacros, m.id); }} />
                                <div className="ie-item-info">
                                    <div className="ie-item-name">{m.name || `Imported Macro`}</div>
                                    <div className="ie-item-desc">{m.elements?.length || 0} actions</div>
                                </div>
                                {selectedMacros.has(m.id) && renderReplaceDropdown(m.id, macroMapping, setMacroMapping, existingData.macros.map(mx => ({ id: mx.id, name: mx.name })))}
                            </div>
                        ))}
                    </div>,
                    isMacrosOverLimit ? <span style={{ color: '#ff4444', fontSize: '0.8rem', marginLeft: '12px' }}>Limit exceeded by {selectedMacros.size - allowedMacros}</span> : null
                )}

                {activeView === 'ckeys' && renderDetailView('ckeys', 'Custom Keys', selectedCKeys.size, data.customKeys.length,
                    <div className="ie-item-list">
                        {data.customKeys.length === 0 && <div className="ie-empty-state">No custom keys in file</div>}
                        {data.customKeys.map(c => (
                            <div key={c.id} className={`ie-item ${selectedCKeys.has(c.id) ? 'selected' : ''}`} onClick={() => toggleSetItem(selectedCKeys, setSelectedCKeys, c.id)}>
                                <CustomCheckbox state={selectedCKeys.has(c.id)} onClick={(e) => { e.stopPropagation(); toggleSetItem(selectedCKeys, setSelectedCKeys, c.id); }} />
                                <div className="ie-item-info">
                                    <div className="ie-item-name">{c.name || `Imported Custom Key`}</div>
                                    <div className="ie-item-desc">{c.mode === 0 ? 'Press/Release' : 'Multi-Action'}</div>
                                </div>
                                {selectedCKeys.has(c.id) && renderReplaceDropdown(c.id, ckeyMapping, setCkeyMapping, existingData.customKeys.map(cx => ({ id: cx.id, name: cx.name })))}
                            </div>
                        ))}
                    </div>,
                    isCKeysOverLimit ? <span style={{ color: '#ff4444', fontSize: '0.8rem', marginLeft: '12px' }}>Limit exceeded by {selectedCKeys.size - allowedCKeys}</span> : null
                )}

                {activeView === 'combos' && renderDetailView('combos', 'Combos', selectedCombos.size, data.combos.length,
                    <div className="ie-item-list">
                        {data.combos.length === 0 && <div className="ie-empty-state">No combos in file</div>}
                        {data.combos.map(c => (
                            <div key={c.id} className={`ie-item ${selectedCombos.has(c.id) ? 'selected' : ''}`} onClick={() => toggleSetItem(selectedCombos, setSelectedCombos, c.id)}>
                                <CustomCheckbox state={selectedCombos.has(c.id)} onClick={(e) => { e.stopPropagation(); toggleSetItem(selectedCombos, setSelectedCombos, c.id); }} />
                                <div className="ie-item-info">
                                    <div className="ie-item-name">{c.name || `Imported Combo`}</div>
                                    <div className="ie-item-desc">{c.keys.length} keys</div>
                                </div>
                                {selectedCombos.has(c.id) && renderReplaceDropdown(c.id, comboMapping, setComboMapping, existingData.combos.map(cx => ({ id: cx.id, name: cx.name })))}
                            </div>
                        ))}
                    </div>,
                    isCombosOverLimit ? <span style={{ color: '#ff4444', fontSize: '0.8rem', marginLeft: '12px' }}>Limit exceeded by {selectedCombos.size - allowedCombos}</span> : null
                )}

                <div className="ie-footer">
                    {isDeveloperMode && data.hasPhysicalLayout && (
                        <div className="ie-dev-toggle" onClick={() => setIncludePhysicalLayout(!includePhysicalLayout)}>
                            <CustomCheckbox state={includePhysicalLayout} onClick={(e) => { e.stopPropagation(); setIncludePhysicalLayout(!includePhysicalLayout); }} />
                            <div className="ie-dev-toggle-text">
                                Overwrite Physical Layout Geometry (Developer)
                            </div>
                        </div>
                    )}

                    <div className="ie-footer-actions">
                        <button className="btn btn-secondary" onClick={onClose} disabled={isImporting}>Cancel</button>
                        <button className="btn btn-success" disabled={totalSelected === 0 && !includePhysicalLayout || isAnyOverLimit || isImporting} onClick={() => {
                            onImport({
                                layers: Array.from(selectedLayers),
                                layerMapping,
                                includePhysicalLayout,
                                macros: data.macros.filter(m => selectedMacros.has(m.id)),
                                macroMapping,
                                customKeys: data.customKeys.filter(c => selectedCKeys.has(c.id)),
                                ckeyMapping,
                                combos: data.combos.filter(c => selectedCombos.has(c.id)),
                                comboMapping
                            });
                        }}>
                            {isImporting ? 'Importing...' : 'Import'}
                        </button>
                    </div>
                </div>
            </div>
        </div>,
        document.body
    );
}
