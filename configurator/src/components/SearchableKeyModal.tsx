import React, { useState, useEffect, useRef } from 'react';
import { createPortal } from 'react-dom';
import { ALL_KEYS, getKeyClass, TRANSPARENT, getMacroKeyOptions, getCKeyOptions, getLayerKeyOptions, MACRO_BASE, CKEY_BASE, getSecondaryKeyName, LAYER_ACTION_MIN, LAYER_ACTION_MAX } from '../KeyDefinitions';
import { useLayoutStore } from '../stores/layoutStore';
import type { Macro } from '../types/macros';
import type { CustomKey } from '../types/customKeys';
import '../assets/css/searchable-key-modal.css';

interface SearchableKeyModalProps {
    title?: string;
    currentValue: number;
    macros: Macro[];
    customKeys?: CustomKey[];
    onSelect: (value: number) => void;
    onClose: () => void;
}

function SmoothHeight({ children, isExpanded }: { children: React.ReactNode, isExpanded: boolean }) {
    const [height, setHeight] = useState<number>(0);
    const contentRef = useRef<HTMLDivElement>(null);

    useEffect(() => {
        if (!contentRef.current) return;
        let animationFrameId: number;
        
        const observer = new ResizeObserver((entries) => {
            if (isExpanded && entries[0]) {
                animationFrameId = window.requestAnimationFrame(() => {
                    setHeight(entries[0].target.getBoundingClientRect().height);
                });
            }
        });
        
        observer.observe(contentRef.current);
        return () => {
            observer.disconnect();
            window.cancelAnimationFrame(animationFrameId);
        };
    }, [isExpanded]);

    useEffect(() => {
        if (!isExpanded) {
            setHeight(0);
        } else if (contentRef.current) {
            setHeight(contentRef.current.getBoundingClientRect().height);
        }
    }, [isExpanded]);

    const maxHeight = typeof window !== 'undefined' ? window.innerHeight * 0.55 : 500;
    const isScrollable = height > maxHeight;

    return (
        <div 
            className={`accordion-smooth-wrapper ${isExpanded ? 'open' : ''}`}
            style={{ 
                height: isExpanded ? (isScrollable ? maxHeight : height) : 0,
                overflowY: isScrollable ? 'auto' : 'hidden'
            }}
        >
            <div ref={contentRef} className="accordion-smooth-content">
                {children}
            </div>
        </div>
    );
}

// Icons for the menus
const KeyboardIcon = () => (
    <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
        <rect x="2" y="4" width="20" height="16" rx="2" ry="2"></rect>
        <line x1="6" y1="8" x2="6" y2="8"></line>
        <line x1="10" y1="8" x2="10" y2="8"></line>
        <line x1="14" y1="8" x2="14" y2="8"></line>
        <line x1="18" y1="8" x2="18" y2="8"></line>
        <line x1="6" y1="12" x2="6" y2="12"></line>
        <line x1="10" y1="12" x2="10" y2="12"></line>
        <line x1="14" y1="12" x2="14" y2="12"></line>
        <line x1="18" y1="12" x2="18" y2="12"></line>
        <line x1="7" y1="16" x2="17" y2="16"></line>
    </svg>
);

const MacroIcon = () => (
    <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
        <polygon points="5 3 19 12 5 21 5 3"></polygon>
    </svg>
);

const CustomKeyIcon = () => (
    <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
        <polygon points="12 2 2 7 12 12 22 7 12 2"></polygon>
        <polyline points="2 17 12 22 22 17"></polyline>
        <polyline points="2 12 12 17 22 12"></polyline>
    </svg>
);

const SystemIcon = () => (
    <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
        <circle cx="12" cy="12" r="3"></circle>
        <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"></path>
    </svg>
);

type MenuType = 'key' | 'macro' | 'custom' | 'system';

export default function SearchableKeyModal({ currentValue, macros, customKeys, onSelect, onClose }: SearchableKeyModalProps) {
    const [selectedMenu, setSelectedMenu] = useState<MenuType | null>(null);
    const [searchTerm, setSearchTerm] = useState('');
    const inputRef = useRef<HTMLInputElement>(null);
    const layouts = useLayoutStore(state => state.layoutMetas);

    // Focus input when mounted
    useEffect(() => {
        setTimeout(() => inputRef.current?.focus(), 100);
    }, []);

    const macroOptions = getMacroKeyOptions(macros);
    const ckeyOptions  = getCKeyOptions(customKeys || []);
    const layerOptions = getLayerKeyOptions(layouts);
    const combinedKeys = [...ALL_KEYS, ...macroOptions, ...ckeyOptions, ...layerOptions];

    const filteredKeys = combinedKeys.filter(k => {
        const primary = k.label.toLowerCase();
        const secondary = (getSecondaryKeyName(k.value) || '').toLowerCase();
        const term = searchTerm.toLowerCase();
        return primary.includes(term) || secondary.includes(term);
    });

    const CATEGORIES = [
        { name: 'Custom Keys', filter: (v: number) => v >= CKEY_BASE && v <= 0x3FFF, menu: 'custom' },
        { name: 'Macros',      filter: (v: number) => v >= MACRO_BASE && v <= 0x40FF, menu: 'macro' },
        { name: 'Special',     filter: (v: number) => v === TRANSPARENT || v === 0, menu: 'key' },
        { name: 'Letters',     filter: (v: number) => v >= 0x04 && v <= 0x1D, menu: 'key' },
        { name: 'Numbers',     filter: (v: number) => (v >= 0x1E && v <= 0x27), menu: 'key' },
        { name: 'Symbols',     filter: (v: number) => (v >= 0x2D && v <= 0x38) || (v >= 0x28 && v <= 0x2C), menu: 'key' },
        { name: 'Navigation',  filter: (v: number) => (v >= 0x46 && v <= 0x52) || v === 0x65 || v === 0x39, menu: 'key' },
        { name: 'F-Keys',      filter: (v: number) => v >= 0x3A && v <= 0x45, menu: 'key' },
        { name: 'Modifiers',   filter: (v: number) => v >= 0xE0 && v <= 0xE7, menu: 'key' },
        { name: 'Layers',      filter: (v: number) => v >= LAYER_ACTION_MIN && v <= LAYER_ACTION_MAX, menu: 'system' },
        { name: 'System / BLE',filter: (v: number) => v >= 0x2000 && v <= 0x20FF, menu: 'system' },
    ];

    const [mouseDownOnOverlay, setMouseDownOnOverlay] = useState(false);

    const handleOverlayMouseDown = (e: React.MouseEvent) => {
        if (e.target === e.currentTarget) {
            setMouseDownOnOverlay(true);
        } else {
            setMouseDownOnOverlay(false);
        }
    };

    const handleOverlayMouseUp = (e: React.MouseEvent) => {
        if (mouseDownOnOverlay && e.target === e.currentTarget) {
            onClose();
        }
        setMouseDownOnOverlay(false);
    };

    const handleMenuSelect = (menu: MenuType) => {
        if (selectedMenu === menu) {
            setSelectedMenu(null);
        } else {
            setSelectedMenu(menu);
            setSearchTerm('');
        }
    };

    const isMacrosDisabled = macros.length === 0;
    const isCustomKeysAllowed = customKeys !== undefined;
    const isCustomKeysDisabled = !isCustomKeysAllowed || customKeys.length === 0;
    
    const customKeyTooltip = !isCustomKeysAllowed 
        ? 'Custom Keys are not allowed here' 
        : (isCustomKeysDisabled ? 'No custom keys were defined' : '');
    
    const isSearching = searchTerm.trim().length > 0;
    const isExpanded = isSearching || selectedMenu !== null;

    // Ensure typing always goes to the search input even if it loses focus
    useEffect(() => {
        const handleGlobalKeyDown = (e: KeyboardEvent) => {
            if (e.key === 'Escape') {
                onClose();
                return;
            }
            // If the active element is not our input, and the user presses a printable character or backspace
            if (
                document.activeElement !== inputRef.current &&
                (e.key === 'Backspace' || (e.key.length === 1 && !e.ctrlKey && !e.metaKey && !e.altKey))
            ) {
                inputRef.current?.focus();
            }
        };

        window.addEventListener('keydown', handleGlobalKeyDown);
        return () => window.removeEventListener('keydown', handleGlobalKeyDown);
    }, []);


    return createPortal(
        <div
            className="modal-overlay"
            onMouseDown={handleOverlayMouseDown}
            onMouseUp={handleOverlayMouseUp}
        >
            <div className={`modal-content accordion-modal ${isExpanded ? 'expanded' : ''}`} onClick={e => e.stopPropagation()}>
                <div className="accordion-header">
                    <div className="accordion-header-top">
                        <div style={{ flex: 1 }}></div>
                        <button className="btn-close-inline" onClick={onClose} title="Close">
                            <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                                <line x1="18" y1="6" x2="6" y2="18"></line>
                                <line x1="6" y1="6" x2="18" y2="18"></line>
                            </svg>
                        </button>
                    </div>
                    <div className="accordion-menu-row">
                        <button className={`accordion-btn ${selectedMenu === 'key' ? 'active' : ''}`} onClick={() => handleMenuSelect('key')}>
                            <KeyboardIcon /> <span>Keys</span>
                        </button>
                        <button 
                            className={`accordion-btn ${selectedMenu === 'macro' ? 'active' : ''} ${isMacrosDisabled ? 'disabled' : ''}`} 
                            onClick={() => !isMacrosDisabled && handleMenuSelect('macro')}
                            title={isMacrosDisabled ? 'No macros were defined' : ''}
                        >
                            <MacroIcon /> <span>Macros</span>
                            {isMacrosDisabled && (
                                <svg className="lock-icon" viewBox="0 0 24 24" width="12" height="12" fill="none" stroke="currentColor" strokeWidth="2.5">
                                    <rect x="3" y="11" width="18" height="11" rx="2" ry="2"></rect>
                                    <path d="M7 11V7a5 5 0 0 1 10 0v4"></path>
                                </svg>
                            )}
                        </button>
                        <button 
                            className={`accordion-btn ${selectedMenu === 'custom' ? 'active' : ''} ${isCustomKeysDisabled ? 'disabled' : ''}`} 
                            onClick={() => !isCustomKeysDisabled && handleMenuSelect('custom')}
                            title={customKeyTooltip}
                        >
                            <CustomKeyIcon /> <span>Custom Keys</span>
                            {isCustomKeysDisabled && (
                                <svg className="lock-icon" viewBox="0 0 24 24" width="12" height="12" fill="none" stroke="currentColor" strokeWidth="2.5">
                                    <rect x="3" y="11" width="18" height="11" rx="2" ry="2"></rect>
                                    <path d="M7 11V7a5 5 0 0 1 10 0v4"></path>
                                </svg>
                            )}
                        </button>
                        <button className={`accordion-btn ${selectedMenu === 'system' ? 'active' : ''}`} onClick={() => handleMenuSelect('system')}>
                            <SystemIcon /> <span>System Actions</span>
                        </button>
                    </div>

                    <div className="accordion-search">
                        <input
                            ref={inputRef}
                            type="text"
                            placeholder="Type to search anything..."
                            value={searchTerm}
                            onChange={e => setSearchTerm(e.target.value)}
                            onKeyDown={e => {
                                if (e.key === 'Escape') onClose();
                                if (e.key === 'Enter' && filteredKeys.length > 0) onSelect(filteredKeys[0].value);
                            }}
                        />
                    </div>
                </div>

                <SmoothHeight isExpanded={isExpanded}>
                    <div key={searchTerm || selectedMenu || 'empty'}>
                        {isExpanded && CATEGORIES.map(cat => {
                            if (selectedMenu !== null && cat.menu !== selectedMenu) return null;
                            
                            const catKeys = filteredKeys.filter(k => cat.filter(k.value));
                            if (catKeys.length === 0) return null;
                            
                            return (
                                <div key={cat.name} className="key-category">
                                    <h5>{cat.name}</h5>
                                    <div className="key-option-grid">
                                        {catKeys.map((k, index) => (
                                            <button
                                                key={k.value}
                                                className={`key-option key-option-anim ${k.value === currentValue ? 'active' : ''} ${getKeyClass(k.value)}`}
                                                onClick={() => onSelect(k.value)}
                                                title={k.label}
                                                style={{ animationDelay: `${Math.min(index * 0.015, 0.4)}s` }}
                                            >
                                                <span className="key-option-label">{k.label}</span>
                                                {getSecondaryKeyName(k.value) && (
                                                    <span className="key-option-secondary-label">{getSecondaryKeyName(k.value)}</span>
                                                )}
                                            </button>
                                        ))}
                                    </div>
                                </div>
                            );
                        })}
                        {isExpanded && filteredKeys.length === 0 && (
                            <div className="no-results">No matches found for "{searchTerm}"</div>
                        )}
                    </div>
                </SmoothHeight>
            </div>
        </div>,
        document.body
    );
}
