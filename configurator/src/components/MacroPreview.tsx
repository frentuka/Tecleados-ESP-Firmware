import React, { useMemo, useState, useLayoutEffect, useEffect, useRef } from 'react';
import type { Macro } from '../types/macros';
import { getKeyName } from '../KeyDefinitions';

interface MacroPreviewProps {
    m: Macro;
    macros: Macro[];
}

export const MacroPreview: React.FC<MacroPreviewProps> = ({ m, macros }) => {
    const containerRef = useRef<HTMLDivElement>(null);
    const [showCount, setShowCount] = useState(0);
    const [measuredSig, setMeasuredSig] = useState<string | null>(null);

    const items = useMemo(() => {
        const arr: any[] = [];
        if (!m.elements) return arr;
        for (let j = 0; j < m.elements.length; j++) {
            const el = m.elements[j];
            if (el.type === 'key') {
                if (el.inlineSleep !== undefined && el.inlineSleep > 0) {
                    arr.push({ kind: 'key', action: el.action || 'tap', keyCode: el.key, sleepMs: el.inlineSleep });
                } else {
                    arr.push({ kind: 'key', action: el.action || 'tap', keyCode: el.key });
                }
            } else {
                arr.push({ kind: 'sleep', duration: el.duration });
            }
        }
        return arr;
    }, [m.elements, macros]);

    const itemsSig = useMemo(() => JSON.stringify(items), [items]);
    const isMeasured = measuredSig === itemsSig;

    useEffect(() => {
        if (!containerRef.current || items.length === 0) return;

        // Observe the parent container (macro-card-content) because it has a stable width.
        // Observing our own div creates infinite loops if items line-wrap differently upon slicing.
        const parent = containerRef.current.parentElement;
        if (!parent) return;

        let lastWidth = parent.offsetWidth;

        const ro = new ResizeObserver((entries) => {
            if (!entries[0]) return;
            const w = (entries[0].target as HTMLElement).offsetWidth;
            // Only force remeasure if the parent card actually resizes (e.g., window resize)
            if (Math.abs(w - lastWidth) > 2) {
                lastWidth = w;
                setMeasuredSig(null);
            }
        });

        ro.observe(parent);
        return () => ro.disconnect();
    }, [itemsSig]);

    useLayoutEffect(() => {
        if (!isMeasured && containerRef.current && items.length > 0) {
            const container = containerRef.current;
            const children = Array.from(container.children) as HTMLElement[];
            if (children.length === 0) return;

            let rowCount = 1;
            let currentY = children[0].offsetTop;
            let breakIdx = items.length;

            for (let i = 0; i < children.length; i++) {
                // Allow a small tolerance for vertical flex misalignment
                if (children[i].offsetTop > currentY + 5) {
                    currentY = children[i].offsetTop;
                    rowCount++;
                }
                if (rowCount > 3) {
                    breakIdx = i;
                    break;
                }
            }

            if (breakIdx < items.length) {
                setShowCount(Math.max(1, breakIdx - 1));
            } else {
                setShowCount(items.length);
            }

            // Mark as measured for this precise content state
            setMeasuredSig(itemsSig);
        }
    }, [isMeasured, items, itemsSig]);

    if (!m.elements) {
        return (
            <div className="macro-preview-sequence">
                <span className="preview-more" style={{ opacity: 0.5, fontStyle: 'italic' }}>
                    Loading...
                </span>
            </div>
        );
    }

    if (m.elements.length === 0) {
        return (
            <div className="macro-preview-sequence">
                <span className="preview-more" style={{ opacity: 0.5, fontStyle: 'italic' }}>
                    empty
                </span>
            </div>
        );
    }

    const renderItem = (it: any, i: number) => {
        if (it.kind === 'key') {
            return (
                <span key={i} className={`preview-el${it.action !== 'tap' ? ` preview-el-${it.action}` : ''}`}>
                    <span className="preview-el-action">
                        {it.action === 'press' ? '↓' : it.action === 'release' ? '↑' : '↕'}
                    </span>
                    {getKeyName(it.keyCode, macros)}
                    {it.sleepMs !== undefined && (
                        <span className="preview-el-sleep">· {it.sleepMs}</span>
                    )}
                </span>
            );
        } else {
            return (
                <span key={i} className="preview-el preview-sleep">
                    <span className="preview-sleep-icon">🌙</span>
                    <span className="preview-sleep-val">{it.duration}</span>
                </span>
            );
        }
    };

    return (
        <div ref={containerRef} className="macro-preview-sequence" style={!isMeasured ? { opacity: 0 } : undefined}>
            {!isMeasured ? items.map(renderItem) : items.slice(0, showCount).map(renderItem)}
            {isMeasured && items.length > showCount && (
                <span className="preview-more">+{items.length - showCount}</span>
            )}
        </div>
    );
};
