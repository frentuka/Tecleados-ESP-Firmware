import React, { useState, useRef, useEffect } from 'react';
import { createPortal } from 'react-dom';
import { LAYER_ACTION_MOMENTARY, LAYER_ACTION_TOGGLE, LAYER_ACTION_ON, LAYER_ACTION_OFF } from '../KeyDefinitions';

interface LayerKeyButtonProps {
    layoutId: number;
    layoutName: string;
    currentValue: number;
    onSelect: (value: number) => void;
    animationDelay?: string;
}

export default function LayerKeyButton({ layoutId, layoutName, currentValue, onSelect, animationDelay }: LayerKeyButtonProps) {
    // const [isHovered, setIsHovered] = useState(false);
    const [showMenu, setShowMenu] = useState(false);
    const [menuPosition, setMenuPosition] = useState({ top: 0, left: 0 });
    const [tooltipPosition, setTooltipPosition] = useState({ top: 0, left: 0, show: false });
    const buttonRef = useRef<HTMLButtonElement>(null);
    const menuRef = useRef<HTMLDivElement>(null);

    const handleOpenMenu = (clientX: number, clientY: number) => {
        let top = clientY;
        let left = clientX;

        if (left > window.innerWidth - 220) {
            left = window.innerWidth - 220;
        }
        if (top > window.innerHeight - 260) {
            top = window.innerHeight - 260;
        }

        setMenuPosition({ top, left });
        setShowMenu(true);
        setTooltipPosition(prev => ({ ...prev, show: false }));
    };

    const handleContextMenu = (e: React.MouseEvent) => {
        e.preventDefault();
        e.stopPropagation();
        handleOpenMenu(e.clientX, e.clientY);
    };

    const handleMouseDown = (e: React.MouseEvent) => {
        if (e.button === 2) {
            e.preventDefault();
            e.stopPropagation();
            handleOpenMenu(e.clientX, e.clientY);
        }
    };

    useEffect(() => {
        if (!showMenu) return;
        
        const handleClickOutside = (e: MouseEvent) => {
            if (menuRef.current && menuRef.current.contains(e.target as Node)) {
                return;
            }
            setShowMenu(false);
        };
        
        const timer = setTimeout(() => {
            document.addEventListener('mousedown', handleClickOutside, true);
        }, 10);
        
        return () => {
            clearTimeout(timer);
            document.removeEventListener('mousedown', handleClickOutside, true);
        };
    }, [showMenu]);

    const handleMouseEnter = () => {
        if (showMenu) return;
        if (buttonRef.current) {
            const rect = buttonRef.current.getBoundingClientRect();
            // Position tooltip centrally above the button
            setTooltipPosition({
                top: rect.top - 8,
                left: rect.left + rect.width / 2,
                show: true
            });
        }
    };

    const handleMouseLeave = () => {
        setTooltipPosition(prev => ({ ...prev, show: false }));
    };

    const isAnyActive = [
        LAYER_ACTION_MOMENTARY + layoutId,
        LAYER_ACTION_TOGGLE + layoutId,
        LAYER_ACTION_ON + layoutId,
        LAYER_ACTION_OFF + layoutId
    ].includes(currentValue);

    return (
        <>
            <button
                ref={buttonRef}
                className={`key-option key-option-anim key-system ${isAnyActive ? 'active' : ''} layer-key-btn`}
                onClick={() => onSelect(LAYER_ACTION_MOMENTARY + layoutId)}
                onContextMenu={handleContextMenu}
                onMouseDown={handleMouseDown}
                onMouseEnter={handleMouseEnter}
                onMouseLeave={handleMouseLeave}
                style={{ animationDelay }}
            >
                <span className="key-option-label layer-btn-label" title={`Layer: ${layoutName}`}>Layer: {layoutName}</span>
            </button>

            {tooltipPosition.show && createPortal(
                <div 
                    className="layer-key-tooltip"
                    style={{ 
                        top: tooltipPosition.top, 
                        left: tooltipPosition.left,
                        transform: 'translate(-50%, -100%)' // Move up by its own height
                    }}
                >
                    <div className="tooltip-desc">Activates the layer while held.</div>
                    <div className="tooltip-legend"><i>right-click for advanced actions</i></div>
                </div>,
                document.body
            )}

            {showMenu && createPortal(
                <div 
                    ref={menuRef}
                    className="layer-context-menu"
                    style={{ top: menuPosition.top, left: menuPosition.left }}
                    onClick={(e) => e.stopPropagation()}
                >
                    <div className="layer-context-header">
                        Layer: {layoutName}
                    </div>
                    
                    <button className="layer-ctx-btn ctx-hold" onClick={() => { onSelect(LAYER_ACTION_MOMENTARY + layoutId); setShowMenu(false); }}>
                        <div className="ctx-icon">
                            <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M14 9V5a3 3 0 0 0-3-3l-4 9v11h11.28a2 2 0 0 0 2-1.7l1.38-9a2 2 0 0 0-2-2.3zM7 22H4a2 2 0 0 1-2-2v-7a2 2 0 0 1 2-2h3"></path></svg>
                        </div>
                        <div className="ctx-text">
                            <span className="ctx-title">Hold</span>
                            <span className="ctx-desc">Activates while held</span>
                        </div>
                    </button>
                    
                    <button className="layer-ctx-btn ctx-toggle" onClick={() => { onSelect(LAYER_ACTION_TOGGLE + layoutId); setShowMenu(false); }}>
                        <div className="ctx-icon">
                            <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><polyline points="23 4 23 10 17 10"></polyline><polyline points="1 20 1 14 7 14"></polyline><path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"></path></svg>
                        </div>
                        <div className="ctx-text">
                            <span className="ctx-title">Toggle</span>
                            <span className="ctx-desc">Switches on/off permanently</span>
                        </div>
                    </button>
                    
                    <button className="layer-ctx-btn ctx-on" onClick={() => { onSelect(LAYER_ACTION_ON + layoutId); setShowMenu(false); }}>
                        <div className="ctx-icon">
                            <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="10"></circle><line x1="12" y1="8" x2="12" y2="16"></line><line x1="8" y1="12" x2="16" y2="12"></line></svg>
                        </div>
                        <div className="ctx-text">
                            <span className="ctx-title">Turn On</span>
                            <span className="ctx-desc">Forces layer on</span>
                        </div>
                    </button>
                    
                    <button className="layer-ctx-btn ctx-off" onClick={() => { onSelect(LAYER_ACTION_OFF + layoutId); setShowMenu(false); }}>
                        <div className="ctx-icon">
                            <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="10"></circle><line x1="8" y1="12" x2="16" y2="12"></line></svg>
                        </div>
                        <div className="ctx-text">
                            <span className="ctx-title">Turn Off</span>
                            <span className="ctx-desc">Forces layer off</span>
                        </div>
                    </button>
                </div>,
                document.body
            )}
        </>
    );
}
