import React, { useState, useEffect, useRef, useMemo } from 'react';
import type { Macro, ImportableMacro } from './types/macros';
import { MACRO_BASE } from './KeyDefinitions';
import { getModeBadge } from './components/MacroIcons';
import MacroEditorModal from './components/MacroEditorModal';
import { MacroPreview } from './components/MacroPreview';
import MacroModeModal from './components/MacroModeModal';
import ExportModal from './components/ExportModal';
import ImportModal from './components/ImportModal';

interface MacrosDashboardProps {
    macros: Macro[];
    macroLimits: { maxEvents: number; maxMacros: number } | null;
    isDeveloperMode: boolean;
    onSaveMacro: (newMacro: Macro) => Promise<void>;
    onDeleteMacro: (id: number) => Promise<void>;
    onReload: () => void;
    onFetchSingleMacro: (id: number) => Promise<Macro | null>;
}

export default function MacrosDashboard({ 
    macros, 
    macroLimits, 
    isDeveloperMode, 
    onSaveMacro, 
    onDeleteMacro, 
    onReload, 
    onFetchSingleMacro 
}: MacrosDashboardProps) {
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
    const [macrosToImport, setMacrosToImport] = useState<ImportableMacro[]>([]);
    
    const fileInputRef = useRef<HTMLInputElement>(null);
    const menuRef = useRef<HTMLDivElement>(null);
    const importGuardRef = useRef(false);

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
                    if (err.name !== 'AbortError') throw err;
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

    const handleImport = async (event: React.ChangeEvent<HTMLInputElement>) => {
        const file = event.target.files?.[0];
        if (!file) return;

        const reader = new FileReader();
        reader.onload = async (e) => {
            try {
                const importedData = JSON.parse(e.target?.result as string);
                const parsedMacros = Array.isArray(importedData) ? importedData : [importedData];
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
        event.target.value = '';
    };

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
                    setMacrosToImport(prev => prev.filter(item => item.tempId !== tempId));
                } catch (err: any) {
                    alert(`Failed to save imported macro "${newMacro.name}": ${err?.message || 'Unknown error'}`);
                    break;
                } finally {
                    setIsCreating(false);
                }
            }
        } finally {
            setIsImporting(false);
            setMacrosToImport(prev => {
                if (prev.length === 0) setIsImportModalOpen(false);
                return prev;
            });
            importGuardRef.current = false;
        }
    };

    return (
        <div className="macros-dashboard">
            <input type="file" ref={fileInputRef} style={{ display: 'none' }} accept=".json" onChange={handleImport} />
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
                                <button className="dropdown-item" onClick={() => { setIsExportModalOpen(true); setIsMenuOpen(false); }}>Export Macros</button>
                                <button className="dropdown-item" onClick={() => { fileInputRef.current?.click(); setIsMenuOpen(false); }}>Import Macros</button>
                                <button className="dropdown-item" onClick={() => { onReload?.(); setIsMenuOpen(false); }}>Refresh</button>
                            </div>
                        )}
                    </div>
                </div>
                <button className="btn" onClick={handleCreate} disabled={isAtMacroLimit} title={isAtMacroLimit ? `Maximum macros reached (${macroLimits!.maxMacros})` : undefined}>
                    Create Macro
                </button>
            </div>

            <div className="macros-list">
                {macros.length === 0 && !isCreating ? (
                    <div className="empty-state">No macros defined yet.</div>
                ) : (
                    <div className="macro-cards-grid">
                        {sortedMacros.map(m => (
                            <div key={m.id} className={`macro-card glass-panel ${busyMacroIds.has(m.id) ? 'macro-card-busy' : ''}`} onClick={() => !busyMacroIds.has(m.id) && handleEdit(m)}>
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
                                    {m.elements && <MacroPreview m={m} macros={macros} />}
                                </div>
                                <div className="macro-card-actions" onClick={e => e.stopPropagation()}>
                                    <div style={{ flex: 1, display: 'flex', alignItems: 'center' }}>
                                        {fetchingMacroId === m.id && <span style={{ fontSize: '0.8rem', color: 'var(--text-secondary)' }}>Loading...</span>}
                                        {isDeveloperMode && (
                                            <span className="macro-id-dev">
                                                ID: 0x{(MACRO_BASE + m.id).toString(16).toUpperCase()}
                                            </span>
                                        )}
                                    </div>
                                    <button className="btn-icon btn-danger" title="Delete" onClick={() => handleDelete(m.id)} disabled={busyMacroIds.has(m.id)}>
                                        Delete
                                    </button>
                                </div>
                            </div>
                        ))}
                        {isCreating && (
                            <div className="macro-card glass-panel macro-card-busy">
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
                        if (isNew) setIsCreating(true);
                        else markBusy(m.id, 'Saving...');
                        try {
                            await onSaveMacro(m);
                        } catch (err: any) {
                            alert(`Failed to save macro: ${err?.message || 'Unknown error'}`);
                        } finally {
                            if (isNew) setIsCreating(false);
                            else clearBusy(m.id);
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
