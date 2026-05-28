import React from 'react';
import { useLayoutStore } from '../stores/layoutStore';
import type { Combo } from '../types/combos';
import { DEFAULT_PHYSICAL_LAYOUT } from './ComboKeySelector';

interface ComboPreviewProps {
    combo: Combo;
}

export function ComboPreview({ combo }: ComboPreviewProps) {
    const { physicalLayout } = useLayoutStore();
    const layout = physicalLayout || DEFAULT_PHYSICAL_LAYOUT;
    
    const selectedSet = new Set(combo.keys.map(k => `${k.row}-${k.col}`));

    const rotatePoint = (x: number, y: number, cx: number, cy: number, deg: number): [number, number] => {
        const rad = deg * Math.PI / 180;
        const cos = Math.cos(rad), sin = Math.sin(rad);
        const dx = x - cx, dy = y - cy;
        return [cx + dx * cos - dy * sin, cy + dx * sin + dy * cos];
    };

    let minKeyX = Infinity, minKeyY = Infinity, maxKeyX = -Infinity, maxKeyY = -Infinity;
    
    layout.forEach(row => {
        row.forEach(pk => {
            if (pk.r && pk.rx !== undefined && pk.ry !== undefined) {
                const corners: [number, number][] = [
                    [pk.x, pk.y], [pk.x + pk.w, pk.y],
                    [pk.x, pk.y + pk.h], [pk.x + pk.w, pk.y + pk.h],
                ];
                corners.forEach(([cx, cy]) => {
                    const [rx, ry] = rotatePoint(cx, cy, pk.rx!, pk.ry!, pk.r!);
                    minKeyX = Math.min(minKeyX, rx);
                    minKeyY = Math.min(minKeyY, ry);
                    maxKeyX = Math.max(maxKeyX, rx);
                    maxKeyY = Math.max(maxKeyY, ry);
                });
            } else {
                minKeyX = Math.min(minKeyX, pk.x);
                minKeyY = Math.min(minKeyY, pk.y);
                maxKeyX = Math.max(maxKeyX, pk.x + pk.w);
                maxKeyY = Math.max(maxKeyY, pk.y + pk.h);
            }
        });
    });

    if (!isFinite(minKeyX)) { minKeyX = 0; minKeyY = 0; maxKeyX = 17; maxKeyY = 5; }
    const MARGIN = 0.5;
    minKeyX -= MARGIN; minKeyY -= MARGIN;
    maxKeyX += MARGIN; maxKeyY += MARGIN;
    const gridW = maxKeyX - minKeyX;
    const gridH = maxKeyY - minKeyY;

    return (
        <svg viewBox={`${minKeyX} ${minKeyY} ${gridW} ${gridH}`} style={{ width: '100%', height: '100%', display: 'block' }}>
            {layout.map((row, ri) => (
                <React.Fragment key={ri}>
                    {row.map((pk) => {
                        const isSelected = selectedSet.has(`${pk.row}-${pk.col}`);
                        let orderIndex = -1;
                        if (isSelected && combo.strictOrder) {
                            orderIndex = combo.keys.findIndex(k => k.row === pk.row && k.col === pk.col);
                        }
                        
                        const transform = pk.r ? `rotate(${pk.r}, ${pk.rx}, ${pk.ry})` : undefined;
                        
                        return (
                            <g key={`${pk.row}-${pk.col}`} transform={transform}>
                                <rect 
                                    x={pk.x + (isSelected ? 0.08 : 0.05)} 
                                    y={pk.y + (isSelected ? 0.08 : 0.05)} 
                                    width={pk.w - (isSelected ? 0.16 : 0.1)} 
                                    height={pk.h - (isSelected ? 0.16 : 0.1)} 
                                    rx={isSelected ? "0.12" : "0.15"} 
                                    fill={isSelected ? "var(--accent-color)" : "rgba(0, 0, 0, 0.6)"}
                                    stroke={isSelected ? "none" : "rgba(255, 255, 255, 0.05)"}
                                    strokeWidth={isSelected ? undefined : "0.05"}
                                    opacity={isSelected ? 1 : 0.8}
                                />
                                {isSelected && combo.strictOrder && orderIndex >= 0 && (
                                    <text 
                                        x={pk.x + pk.w/2} 
                                        y={pk.y + pk.h/2} 
                                        fill="white" 
                                        fontSize="0.45" 
                                        fontWeight="bold"
                                        textAnchor="middle" 
                                        dominantBaseline="central"
                                    >
                                        {orderIndex + 1}
                                    </text>
                                )}
                            </g>
                        );
                    })}
                </React.Fragment>
            ))}
        </svg>
    );
}
