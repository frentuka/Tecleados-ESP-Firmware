import React, { useRef } from 'react';
import type { TimelineBlock } from '../../types/timeline';

interface TimelineBlockProps {
    block: TimelineBlock;
    pxPerMs: number;
    onChange: (id: string, newStartTime: number, newDuration: number) => void;
    onSelect?: (id: string) => void;
    isSelected?: boolean;
}

export default function TimelineBlockComponent({ block, pxPerMs, onChange, onSelect, isSelected }: TimelineBlockProps) {
    const blockRef = useRef<HTMLDivElement>(null);
    const leftPx = block.startTime * pxPerMs;
    // For visual purposes, we give 'press' and 'release' a minimum visual width, 
    // but tap and hold get their actual width (with a minimum so they are clickable)
    const rawWidthPx = block.duration * pxPerMs;
    const widthPx = Math.max(rawWidthPx, 8); // Minimum 8px wide

    const handlePointerDown = (e: React.PointerEvent) => {
        if (onSelect) onSelect(block.id);
        
        e.stopPropagation();
        const startClientX = e.clientX;
        const initialStartTime = block.startTime;

        const onPointerMove = (moveEvent: PointerEvent) => {
            const deltaX = moveEvent.clientX - startClientX;
            const deltaMs = deltaX / pxPerMs;
            const newStartTime = Math.max(0, initialStartTime + deltaMs);
            onChange(block.id, newStartTime, block.duration);
        };

        const onPointerUp = () => {
            window.removeEventListener('pointermove', onPointerMove);
            window.removeEventListener('pointerup', onPointerUp);
        };

        window.addEventListener('pointermove', onPointerMove);
        window.addEventListener('pointerup', onPointerUp);
    };

    const handleResizePointerDown = (e: React.PointerEvent) => {
        if (block.type === 'press' || block.type === 'release') return; // Cannot resize these
        
        e.stopPropagation();
        if (onSelect) onSelect(block.id);

        const startClientX = e.clientX;
        const initialDuration = block.duration;

        const onPointerMove = (moveEvent: PointerEvent) => {
            const deltaX = moveEvent.clientX - startClientX;
            const deltaMs = deltaX / pxPerMs;
            const newDuration = Math.max(0, initialDuration + deltaMs); // Min 0ms
            onChange(block.id, block.startTime, newDuration);
        };

        const onPointerUp = () => {
            window.removeEventListener('pointermove', onPointerMove);
            window.removeEventListener('pointerup', onPointerUp);
        };

        window.addEventListener('pointermove', onPointerMove);
        window.addEventListener('pointerup', onPointerUp);
    };

    let bgColor = 'var(--accent-color, #6436b5)';
    if (block.type === 'press') bgColor = '#2a7a3b';
    if (block.type === 'release') bgColor = '#d94141';

    return (
        <div 
            id={block.id}
            ref={blockRef}
            className={`timeline-block ${isSelected ? 'selected' : ''}`}
            style={{
                position: 'absolute',
                left: `${leftPx}px`,
                width: `${widthPx}px`,
                height: '24px',
                top: '4px',
                backgroundColor: bgColor,
                borderRadius: '4px',
                cursor: 'grab',
                opacity: 0.9,
                display: 'flex',
                alignItems: 'center',
                boxShadow: isSelected ? '0 0 0 2px #fff' : 'none',
                userSelect: 'none'
            }}
            onPointerDown={handlePointerDown}
        >
            <div style={{ overflow: 'hidden', whiteSpace: 'nowrap', fontSize: '10px', color: '#fff', padding: '0 4px', pointerEvents: 'none' }}>
                {block.type === 'tap' && 'Tap'}
                {block.type === 'hold' && 'Hold'}
                {block.type === 'press' && 'Press'}
                {block.type === 'release' && 'Release'}
            </div>
            
            {(block.type === 'tap' || block.type === 'hold') && (
                <div 
                    className="timeline-block-resize-handle"
                    style={{
                        position: 'absolute',
                        right: 0,
                        top: 0,
                        bottom: 0,
                        width: '8px',
                        cursor: 'ew-resize',
                        backgroundColor: 'rgba(255,255,255,0.2)',
                        borderTopRightRadius: '4px',
                        borderBottomRightRadius: '4px'
                    }}
                    onPointerDown={handleResizePointerDown}
                />
            )}
        </div>
    );
}
