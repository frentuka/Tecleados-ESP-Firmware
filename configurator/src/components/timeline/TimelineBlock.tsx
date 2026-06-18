import React, { useRef } from 'react';
import type { TimelineBlock } from '../../types/timeline';

interface TimelineBlockProps {
    block: TimelineBlock;
    pxPerMs: number;
    onChange: (id: string, newStartTime: number, newDuration: number) => void;
    onMove: (ids: string[], deltaMs: number) => void;
    onSelect: (id: string, multi: boolean) => void;
    isSelected: boolean;
    selectedIds: Set<string>;
}

export default function TimelineBlockComponent({ block, pxPerMs, onChange, onMove, onSelect, isSelected, selectedIds }: TimelineBlockProps) {
    const blockRef = useRef<HTMLDivElement>(null);
    const leftPx = block.startTime * pxPerMs;
    const rawWidthPx = block.duration * pxPerMs;
    const widthPx = Math.max(rawWidthPx, 8); // Minimum 8px wide

    const snapDelta = (ms: number) => {
        const step = pxPerMs > 0.5 ? 10 : 50;
        return Math.round(ms / step) * step;
    };

    const handlePointerDown = (e: React.PointerEvent) => {
        e.stopPropagation();
        
        const isMulti = e.shiftKey || e.ctrlKey || e.metaKey;
        if (!isSelected) {
            onSelect(block.id, isMulti);
        }

        const startClientX = e.clientX;
        const targetIds = isSelected || isMulti ? Array.from(selectedIds.has(block.id) ? selectedIds : new Set([...selectedIds, block.id])) : [block.id];

        let accumulatedDeltaMs = 0;

        const onPointerMove = (moveEvent: PointerEvent) => {
            const deltaX = moveEvent.clientX - startClientX;
            const deltaMs = snapDelta(deltaX / pxPerMs);
            
            if (deltaMs !== accumulatedDeltaMs) {
                const step = deltaMs - accumulatedDeltaMs;
                accumulatedDeltaMs = deltaMs;
                onMove(targetIds, step);
            }
        };

        const onPointerUp = () => {
            window.removeEventListener('pointermove', onPointerMove);
            window.removeEventListener('pointerup', onPointerUp);
        };

        window.addEventListener('pointermove', onPointerMove);
        window.addEventListener('pointerup', onPointerUp);
    };

    const handleResizePointerDown = (e: React.PointerEvent) => {
        if (block.type === 'press' || block.type === 'release') return;
        
        e.stopPropagation();
        if (!isSelected) {
            onSelect(block.id, false);
        }

        const startClientX = e.clientX;
        const initialDuration = block.duration;

        const onPointerMove = (moveEvent: PointerEvent) => {
            const deltaX = moveEvent.clientX - startClientX;
            const deltaMs = snapDelta(deltaX / pxPerMs);
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

    // Icons
    const renderIcon = () => {
        switch (block.type) {
            case 'tap':
                return <svg viewBox="0 0 24 24" width="12" height="12" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="10"></circle><circle cx="12" cy="12" r="3"></circle></svg>;
            case 'hold':
                return <svg viewBox="0 0 24 24" width="12" height="12" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"></rect></svg>;
            case 'press':
                return <svg viewBox="0 0 24 24" width="12" height="12" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><path d="M12 5v14M5 12l7 7 7-7"></path></svg>;
            case 'release':
                return <svg viewBox="0 0 24 24" width="12" height="12" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><path d="M12 19V5M5 12l7-7 7 7"></path></svg>;
            default:
                return null;
        }
    };

    return (
        <div 
            id={block.id}
            ref={blockRef}
            className={`timeline-block type-${block.type} ${isSelected ? 'selected' : ''}`}
            style={{
                left: `${leftPx}px`,
                width: `${widthPx}px`
            }}
            onPointerDown={handlePointerDown}
            title={`${block.type} (${block.startTime}ms - ${block.startTime + block.duration}ms)`}
        >
            <div className="timeline-block-content">
                {renderIcon()}
                {widthPx > 30 && (
                    <span style={{ textTransform: 'capitalize' }}>{block.type}</span>
                )}
            </div>
            
            {(block.type === 'tap' || block.type === 'hold') && (
                <div 
                    className="timeline-block-resize-handle"
                    onPointerDown={handleResizePointerDown}
                />
            )}
        </div>
    );
}
