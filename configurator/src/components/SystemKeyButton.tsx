import React, { useState, useRef } from 'react';
import { createPortal } from 'react-dom';

interface SystemKeyButtonProps {
    value: number;
    label: string;
    description: string;
    isActive: boolean;
    keyClass: string;
    onSelect: (value: number) => void;
    animationDelay?: string;
}

export default function SystemKeyButton({ value, label, description, isActive, keyClass, onSelect, animationDelay }: SystemKeyButtonProps) {
    const [tooltipPosition, setTooltipPosition] = useState({ top: 0, left: 0, show: false });
    const buttonRef = useRef<HTMLButtonElement>(null);

    const handleMouseEnter = () => {
        if (buttonRef.current) {
            const rect = buttonRef.current.getBoundingClientRect();
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

    return (
        <>
            <button
                ref={buttonRef}
                className={`key-option key-option-anim ${isActive ? 'active' : ''} ${keyClass}`}
                onClick={() => onSelect(value)}
                onMouseEnter={handleMouseEnter}
                onMouseLeave={handleMouseLeave}
                style={{ animationDelay }}
            >
                <span className="key-option-label">{label}</span>
            </button>

            {tooltipPosition.show && createPortal(
                <div 
                    className="layer-key-tooltip"
                    style={{ 
                        top: tooltipPosition.top, 
                        left: tooltipPosition.left
                    }}
                >
                    <div className="tooltip-desc">{description}</div>
                </div>,
                document.body
            )}
        </>
    );
}
