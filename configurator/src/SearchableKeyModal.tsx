import { useState, useEffect, useRef } from 'react';
import { createPortal } from 'react-dom';
import { ALL_KEYS, getKeyClass, TRANSPARENT, getMacroKeyOptions, MACRO_BASE } from './KeyDefinitions';
import type { Macro } from './App';

interface SearchableKeyModalProps {
    currentValue: number;
    macros: Macro[];
    onSelect: (value: number) => void;
    onClose: () => void;
}

export default function SearchableKeyModal({ currentValue, macros, onSelect, onClose }: SearchableKeyModalProps) {
    const [searchTerm, setSearchTerm] = useState('');
    const inputRef = useRef<HTMLInputElement>(null);

    useEffect(() => {
        inputRef.current?.focus();
    }, []);

    const macroOptions = getMacroKeyOptions(macros);
    const combinedKeys = [...ALL_KEYS, ...macroOptions];

    const filteredKeys = combinedKeys.filter(k =>
        k.label.toLowerCase().includes(searchTerm.toLowerCase())
    );

    // Grouping for a better UX
    const CATEGORIES = [
        { name: 'Macros', filter: (v: number) => v >= MACRO_BASE && v <= 0x40FF },
        { name: 'Special', filter: (v: number) => v === TRANSPARENT || v === 0 },
        { name: 'Letters', filter: (v: number) => v >= 0x04 && v <= 0x1D },
        { name: 'Numbers', filter: (v: number) => (v >= 0x1E && v <= 0x27) },
        { name: 'Symbols', filter: (v: number) => (v >= 0x2D && v <= 0x38) || (v >= 0x28 && v <= 0x2C) },
        { name: 'Navigation', filter: (v: number) => (v >= 0x46 && v <= 0x52) || v === 0x65 || v === 0x39 },
        { name: 'F-Keys', filter: (v: number) => v >= 0x3A && v <= 0x45 },
        { name: 'Modifiers', filter: (v: number) => v >= 0xE0 && v <= 0xE7 },
        { name: 'System / BLE', filter: (v: number) => v >= 0x2000 && v <= 0x20FF },
    ];

    return createPortal(
        <div className="modal-overlay" onClick={onClose}>
            <div className="modal-content" onClick={e => e.stopPropagation()}>
                <div className="modal-header">
                    <h3>Select Key</h3>
                    <button className="btn-close" onClick={onClose}>&times;</button>
                </div>
                <div className="modal-search">
                    <input
                        ref={inputRef}
                        type="text"
                        placeholder="Search key name (e.g. 'Enter' or 'Shift')..."
                        value={searchTerm}
                        onChange={e => setSearchTerm(e.target.value)}
                        onKeyDown={e => {
                            if (e.key === 'Escape') onClose();
                            if (e.key === 'Enter' && filteredKeys.length > 0) onSelect(filteredKeys[0].value);
                        }}
                    />
                </div>
                <div className="modal-body">
                    {CATEGORIES.map(cat => {
                        const catKeys = filteredKeys.filter(k => cat.filter(k.value));
                        if (catKeys.length === 0) return null;
                        return (
                            <div key={cat.name} className="key-category">
                                <h5>{cat.name}</h5>
                                <div className="key-option-grid">
                                    {catKeys.map(k => (
                                        <button
                                            key={k.value}
                                            className={`key-option ${k.value === currentValue ? 'active' : ''} ${getKeyClass(k.value)}`}
                                            onClick={() => onSelect(k.value)}
                                            title={k.label}
                                        >
                                            <span className="key-option-label">{k.label}</span>
                                        </button>
                                    ))}
                                </div>
                            </div>
                        );
                    })}
                    {filteredKeys.length === 0 && (
                        <div className="no-results">No keys matching "{searchTerm}"</div>
                    )}
                </div>
            </div>
        </div>,
        document.body
    );
}
