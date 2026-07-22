import { useEffect, useState, useRef } from 'react';
import { createPortal } from 'react-dom';
import { MACRO_BASE, CKEY_BASE, getKeyName, getSecondaryKeyName } from '../KeyDefinitions';
import { useLayoutStore } from '../stores/layoutStore';
import type { Macro } from '../types/macros';
import type { CustomKey } from '../types/customKeys';
import '../assets/css/key-action-popover.css';

interface KeyActionPopoverProps {
    code: number;
    anchorEl: HTMLElement;
    macros: Macro[];
    customKeys: CustomKey[];
    onEditEntity: (code: number) => void;
    onMouseEnter: () => void;
    onMouseLeave: () => void;
}

export default function KeyActionPopover({ code, anchorEl, macros, customKeys, onEditEntity, onMouseEnter, onMouseLeave }: KeyActionPopoverProps) {
    const [isVisible, setIsVisible] = useState(false);
    const [pos, setPos] = useState({ top: -1000, left: -1000 }); // Render off-screen initially
    const popoverRef = useRef<HTMLDivElement>(null);
    const layouts = useLayoutStore(state => state.layoutMetas);

    useEffect(() => {
        // Compute position initially and on resize/scroll
        const updatePos = () => {
            if (!anchorEl || !popoverRef.current) return;
            const rect = anchorEl.getBoundingClientRect();
            const popRect = popoverRef.current.getBoundingClientRect();
            
            // Try to place it above the key, centered
            let top = rect.top - popRect.height - 10;
            let left = rect.left + (rect.width / 2) - (popRect.width / 2);
            
            // Adjust bounds
            if (top < 10) top = rect.bottom + 10; // place below if no space above
            if (left < 10) left = 10;
            if (left + popRect.width > window.innerWidth - 10) left = window.innerWidth - popRect.width - 10;
            
            setPos({ top, left });
        };
        
        // Small delay to let the DOM paint, then update pos and fade in
        const timer = requestAnimationFrame(() => {
            updatePos();
            setIsVisible(true);
        });

        window.addEventListener('resize', updatePos);
        window.addEventListener('scroll', updatePos, true);
        
        return () => {
            cancelAnimationFrame(timer);
            window.removeEventListener('resize', updatePos);
            window.removeEventListener('scroll', updatePos, true);
        };
    }, [anchorEl, code]);

    let entityType: 'standard' | 'macro' | 'ckey' = 'standard';
    let entityName = '';
    let icon = null;

    if (code >= MACRO_BASE && code <= 0x40FF) {
        entityType = 'macro';
        const m = macros.find(m => m.id === code - MACRO_BASE);
        entityName = m ? m.name : `Macro ${code - MACRO_BASE}`;
        icon = (
            <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                <circle cx="12" cy="12" r="10"></circle>
                <polygon points="10 8 16 12 10 16 10 8"></polygon>
            </svg>
        );
    } else if (code >= CKEY_BASE && code <= 0x3FFF) {
        entityType = 'ckey';
        const ck = customKeys.find(c => c.id === code - CKEY_BASE);
        entityName = ck ? ck.name : `Custom Key ${code - CKEY_BASE}`;
        icon = (
            <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                <path d="M12 20h9"></path>
                <path d="M16.5 3.5a2.121 2.121 0 0 1 3 3L7 19l-4 1 1-4L16.5 3.5z"></path>
            </svg>
        );
    } else {
        icon = (
            <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                <rect x="3" y="11" width="18" height="11" rx="2" ry="2"></rect>
                <path d="M7 11V7a5 5 0 0 1 10 0v4"></path>
            </svg>
        );
    }

    const keyLabel = getKeyName(code, macros, customKeys, layouts);
    const secLabel = getSecondaryKeyName(code);

    return createPortal(
        <div 
            ref={popoverRef}
            className={`key-action-popover ${isVisible ? 'visible' : ''}`}
            style={{ top: pos.top, left: pos.left }}
            onMouseEnter={onMouseEnter}
            onMouseLeave={onMouseLeave}
            onMouseDown={(e) => e.stopPropagation()} // Prevent clicking through to layout grid
        >
            <div className="key-action-popover-header">
                <div className={`key-action-popover-icon ${entityType}`}>
                    {icon}
                </div>
                <div className="key-action-popover-info">
                    <div className="key-action-popover-title" title={entityType === 'standard' ? keyLabel : entityName}>
                        {entityType === 'standard' ? keyLabel : entityName}
                    </div>
                    <div className="key-action-popover-subtitle" title={entityType === 'standard' ? (secLabel || '') : keyLabel}>
                        {entityType === 'standard' ? (secLabel ? `HID: ${secLabel}` : 'Standard Key') : keyLabel}
                    </div>
                </div>
            </div>

            {entityType !== 'standard' && (
                <div className="key-action-popover-actions">
                    <button 
                        className="key-action-popover-btn primary"
                        onClick={() => onEditEntity(code)}
                    >
                        <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                            <path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"></path>
                            <path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"></path>
                        </svg>
                        {entityType === 'macro' ? 'Edit Macro' : 'Edit Custom Key'}
                    </button>
                </div>
            )}
        </div>,
        document.body
    );
}
