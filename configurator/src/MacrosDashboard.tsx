import { useState } from 'react';
import { createPortal } from 'react-dom';
import type { Macro, MacroElement } from './App';
import { getKeyName, getKeyClass, MACRO_BASE } from './KeyDefinitions';
import SearchableKeyModal from './SearchableKeyModal';

interface MacroEditorModalProps {
    macro: Macro;
    onSave: (macro: Macro) => void;
    onClose: () => void;
    macros: Macro[]; // For name validation/defaulting
}

function MacroEditorModal({ macro: initialMacro, onSave, onClose, macros }: MacroEditorModalProps) {
    const [name, setName] = useState(initialMacro.name || `Custom Macro #${macros.length + 1}`);
    const [elements, setElements] = useState<MacroElement[]>(initialMacro.elements || []);
    const [isKeyModalOpen, setIsKeyModalOpen] = useState(false);
    const [editingElementIndex, setEditingElementIndex] = useState<number | null>(null);

    const addKey = () => {
        setEditingElementIndex(null);
        setIsKeyModalOpen(true);
    };

    const addSleep = () => {
        setElements([...elements, { type: 'sleep', duration: 10 }]);
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
        newElements[index] = { type: 'sleep', duration: Math.max(1, duration) };
        setElements(newElements);
    };

    const moveElement = (index: number, direction: 'up' | 'down') => {
        if (direction === 'up' && index === 0) return;
        if (direction === 'down' && index === elements.length - 1) return;

        const newElements = [...elements];
        const targetIndex = direction === 'up' ? index - 1 : index + 1;
        [newElements[index], newElements[targetIndex]] = [newElements[targetIndex], newElements[index]];
        setElements(newElements);
    };

    return createPortal(
        <div className="modal-overlay" onClick={onClose}>
            <div className="modal-content macro-editor-modal" onClick={e => e.stopPropagation()} style={{ maxWidth: '600px' }}>
                <div className="modal-header">
                    <h3>{initialMacro.id === -1 ? 'Create Macro' : 'Edit Macro'}</h3>
                    <button className="btn-close" onClick={onClose}>&times;</button>
                </div>
                <div className="modal-body">
                    <div className="form-group">
                        <label>Macro Name</label>
                        <input
                            type="text"
                            value={name}
                            onChange={e => setName(e.target.value)}
                            placeholder="Enter macro name..."
                            className="macro-name-input"
                        />
                    </div>

                    <div className="macro-editor-actions">
                        <button className="btn btn-secondary" onClick={addKey}>
                            <svg viewBox="0 0 24 24" width="16" height="16" fill="currentColor"><path d="M20 5H4c-1.1 0-1.99.9-1.99 2L2 17c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V7c0-1.1-.9-2-2-2zm-9 3h2v2h-2V8zm0 3h2v2h-2v-2zM8 8h2v2H8V8zm0 3h2v2H8v-2zM5 8h2v2H5V8zm0 3h2v2H5v-2zm9 7H8v-2h6v2zm0-5h2v2h-2v-2zm0-3h2v2h-2V8zm3 3h2v2h-2v-2zm0-3h2v2h-2V8z" /></svg>
                            Add Key
                        </button>
                        <button className="btn btn-secondary" onClick={addSleep}>
                            <svg viewBox="0 0 24 24" width="16" height="16" fill="currentColor"><path d="M11.99 2C6.47 2 2 6.48 2 12s4.47 10 9.99 10C17.52 22 22 17.52 22 12S17.52 2 11.99 2zm3.3 14.71L11 12.41V7h2v4.59l3.71 3.71-1.42 1.41z" /></svg>
                            Add Sleep
                        </button>
                    </div>

                    <div className="macro-elements-list">
                        <label>Sequence</label>
                        {elements.length === 0 ? (
                            <div className="empty-state">No elements added yet.</div>
                        ) : (
                            elements.map((el, i) => (
                                <div key={i} className="macro-element-row">
                                    <div className="element-drag-controls">
                                        <button onClick={() => moveElement(i, 'up')} disabled={i === 0}>▲</button>
                                        <button onClick={() => moveElement(i, 'down')} disabled={i === elements.length - 1}>▼</button>
                                    </div>

                                    <div className="element-content">
                                        {el.type === 'key' ? (
                                            <div className={`key-preview ${getKeyClass(el.key)}`} onClick={() => {
                                                setEditingElementIndex(i);
                                                setIsKeyModalOpen(true);
                                            }}>
                                                {getKeyName(el.key, macros)}
                                            </div>
                                        ) : (
                                            <div className="sleep-preview">
                                                <span>Sleep</span>
                                                <input
                                                    type="number"
                                                    value={el.duration}
                                                    onChange={e => updateSleep(i, parseInt(e.target.value) || 0)}
                                                    min="1"
                                                />
                                                <span>ms</span>
                                            </div>
                                        )}
                                    </div>

                                    <button className="btn-remove" onClick={() => removeElement(i)}>&times;</button>
                                </div>
                            ))
                        )}
                    </div>
                </div>
                <div className="modal-footer">
                    <button className="btn" onClick={onClose}>Cancel</button>
                    <button className="btn btn-success" onClick={() => onSave({ ...initialMacro, name, elements })}>
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
        </div>,
        document.body
    );
}

interface MacrosDashboardProps {
    macros: Macro[];
    onSaveMacro: (macro: Macro) => void;
    onDeleteMacro: (id: number) => void;
}

export default function MacrosDashboard({ macros, onSaveMacro, onDeleteMacro }: MacrosDashboardProps) {
    const [editingMacro, setEditingMacro] = useState<Macro | null>(null);

    const handleCreate = () => {
        setEditingMacro({ id: -1, name: '', elements: [] });
    };

    const handleEdit = (macro: Macro) => {
        setEditingMacro(macro);
    };

    return (
        <div className="macros-dashboard">
            <div className="dashboard-header">
                <button className="btn btn-success" onClick={handleCreate}>
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
                                        {m.elements.slice(0, 5).map((el, i) => (
                                            <span key={i} className="preview-el">
                                                {el.type === 'key' ? getKeyName(el.key, macros) : `⌛${el.duration}ms`}
                                            </span>
                                        ))}
                                        {m.elements.length > 5 && <span className="preview-more">...</span>}
                                    </div>
                                </div>
                                <div className="macro-card-actions">
                                    <button className="btn btn-sm" onClick={() => handleEdit(m)}>Edit</button>
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
