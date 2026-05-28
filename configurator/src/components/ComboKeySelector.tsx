// No React hooks needed here directly.
import { useLayoutStore } from '../stores/layoutStore';
import type { PhysKey } from '../types/device';
import '../assets/css/keyboard-layout.css';

// Fallback layout if none loaded
export const DEFAULT_PHYSICAL_LAYOUT: PhysKey[][] = [
    [{ row: 0, col: 0, w: 1, h: 1, x: 0, y: 0 }, { row: 0, col: 1, w: 1, h: 1, x: 1, y: 0 }, { row: 0, col: 2, w: 1, h: 1, x: 2, y: 0 }, { row: 0, col: 3, w: 1, h: 1, x: 3, y: 0 }, { row: 0, col: 4, w: 1, h: 1, x: 4, y: 0 }, { row: 0, col: 5, w: 1, h: 1, x: 5, y: 0 }, { row: 0, col: 6, w: 1, h: 1, x: 6, y: 0 }, { row: 0, col: 7, w: 1, h: 1, x: 7, y: 0 }, { row: 0, col: 8, w: 1, h: 1, x: 8, y: 0 }, { row: 0, col: 9, w: 1, h: 1, x: 9, y: 0 }, { row: 0, col: 10, w: 1, h: 1, x: 10, y: 0 }, { row: 0, col: 11, w: 1, h: 1, x: 11, y: 0 }, { row: 0, col: 12, w: 1, h: 1, x: 12, y: 0 }, { row: 0, col: 13, w: 2, h: 1, x: 13, y: 0 }, { row: 0, col: 14, w: 1, h: 1, x: 15, y: 0 }],
    [{ row: 1, col: 0, w: 1.5, h: 1, x: 0, y: 1 }, { row: 1, col: 1, w: 1, h: 1, x: 1.5, y: 1 }, { row: 1, col: 2, w: 1, h: 1, x: 2.5, y: 1 }, { row: 1, col: 3, w: 1, h: 1, x: 3.5, y: 1 }, { row: 1, col: 4, w: 1, h: 1, x: 4.5, y: 1 }, { row: 1, col: 5, w: 1, h: 1, x: 5.5, y: 1 }, { row: 1, col: 6, w: 1, h: 1, x: 6.5, y: 1 }, { row: 1, col: 7, w: 1, h: 1, x: 7.5, y: 1 }, { row: 1, col: 8, w: 1, h: 1, x: 8.5, y: 1 }, { row: 1, col: 9, w: 1, h: 1, x: 9.5, y: 1 }, { row: 1, col: 10, w: 1, h: 1, x: 10.5, y: 1 }, { row: 1, col: 11, w: 1, h: 1, x: 11.5, y: 1 }, { row: 1, col: 12, w: 1, h: 1, x: 12.5, y: 1 }, { row: 1, col: 13, w: 1.5, h: 1, x: 13.5, y: 1 }, { row: 1, col: 14, w: 1, h: 1, x: 15, y: 1 }],
    [{ row: 2, col: 0, w: 1.75, h: 1, x: 0, y: 2 }, { row: 2, col: 1, w: 1, h: 1, x: 1.75, y: 2 }, { row: 2, col: 2, w: 1, h: 1, x: 2.75, y: 2 }, { row: 2, col: 3, w: 1, h: 1, x: 3.75, y: 2 }, { row: 2, col: 4, w: 1, h: 1, x: 4.75, y: 2 }, { row: 2, col: 5, w: 1, h: 1, x: 5.75, y: 2 }, { row: 2, col: 6, w: 1, h: 1, x: 6.75, y: 2 }, { row: 2, col: 7, w: 1, h: 1, x: 7.75, y: 2 }, { row: 2, col: 8, w: 1, h: 1, x: 8.75, y: 2 }, { row: 2, col: 9, w: 1, h: 1, x: 9.75, y: 2 }, { row: 2, col: 10, w: 1, h: 1, x: 10.75, y: 2 }, { row: 2, col: 11, w: 1, h: 1, x: 11.75, y: 2 }, { row: 2, col: 12, w: 2.25, h: 1, x: 12.75, y: 2 }, { row: 2, col: 14, w: 1, h: 1, x: 15, y: 2 }],
    [{ row: 3, col: 0, w: 2.25, h: 1, x: 0, y: 3 }, { row: 3, col: 2, w: 1, h: 1, x: 2.25, y: 3 }, { row: 3, col: 3, w: 1, h: 1, x: 3.25, y: 3 }, { row: 3, col: 4, w: 1, h: 1, x: 4.25, y: 3 }, { row: 3, col: 5, w: 1, h: 1, x: 5.25, y: 3 }, { row: 3, col: 6, w: 1, h: 1, x: 6.25, y: 3 }, { row: 3, col: 7, w: 1, h: 1, x: 7.25, y: 3 }, { row: 3, col: 8, w: 1, h: 1, x: 8.25, y: 3 }, { row: 3, col: 9, w: 1, h: 1, x: 9.25, y: 3 }, { row: 3, col: 10, w: 1, h: 1, x: 10.25, y: 3 }, { row: 3, col: 11, w: 1, h: 1, x: 11.25, y: 3 }, { row: 3, col: 12, w: 1.75, h: 1, x: 12.25, y: 3 }, { row: 3, col: 13, w: 1, h: 1, x: 14, y: 3 }, { row: 3, col: 14, w: 1, h: 1, x: 15, y: 3 }],
    [{ row: 4, col: 0, w: 1.25, h: 1, x: 0, y: 4 }, { row: 4, col: 1, w: 1.25, h: 1, x: 1.25, y: 4 }, { row: 4, col: 2, w: 1.25, h: 1, x: 2.5, y: 4 }, { row: 4, col: 5, w: 6.25, h: 1, x: 3.75, y: 4 }, { row: 4, col: 9, w: 1, h: 1, x: 10, y: 4 }, { row: 4, col: 10, w: 1, h: 1, x: 11, y: 4 }, { row: 4, col: 11, w: 1, h: 1, x: 12, y: 4 }, { row: 4, col: 12, w: 1, h: 1, x: 13, y: 4 }, { row: 4, col: 13, w: 1, h: 1, x: 14, y: 4 }, { row: 4, col: 14, w: 1, h: 1, x: 15, y: 4 }],
    []
];

interface ComboKeySelectorProps {
    selectedKeys: { row: number, col: number }[];
    onChange: (keys: { row: number, col: number }[]) => void;
    strictOrder: boolean;
}

export default function ComboKeySelector({ selectedKeys, onChange, strictOrder }: ComboKeySelectorProps) {
    const { physicalLayout } = useLayoutStore();
    const layout = physicalLayout || DEFAULT_PHYSICAL_LAYOUT;
    
    // Create a Set of strings "r-c" for fast lookup
    const selectedSet = new Set(selectedKeys.map(k => `${k.row}-${k.col}`));

    // Compute bounding box
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

    // Scale down a bit for the combos view
    const SCALE = 2.0; 

    const handleKeyClick = (r: number, c: number) => {
        const id = `${r}-${c}`;
        if (selectedSet.has(id)) {
            // Remove
            onChange(selectedKeys.filter(k => k.row !== r || k.col !== c));
        } else {
            // Add
            if (selectedKeys.length >= 8) return; // Limit combos to 8 keys
            onChange([...selectedKeys, { row: r, col: c }]);
        }
    };

    return (
        <div style={{ width: '100%', overflowX: 'auto', padding: '1rem 0' }}>
            <div className="keyboard-grid" style={{
                position: 'relative',
                width: `${gridW * SCALE}rem`,
                height: `${gridH * SCALE}rem`,
                padding: 0,
                margin: '0 auto',
            }}>
                {layout.map((physRow, ri) => (
                    <div key={ri} className="keyboard-row">
                        {physRow.map((pk) => {
                            const isSelected = selectedSet.has(`${pk.row}-${pk.col}`);
                            let orderIndex = -1;
                            if (isSelected && strictOrder) {
                                orderIndex = selectedKeys.findIndex(k => k.row === pk.row && k.col === pk.col);
                            }

                            return (
                                <div
                                    key={`${pk.row}-${pk.col}`}
                                    style={{
                                        position: 'absolute',
                                        left: `${(pk.x - minKeyX) * SCALE}rem`,
                                        top: `${(pk.y - minKeyY) * SCALE}rem`,
                                        width: `${pk.w * SCALE - 0.2}rem`,
                                        height: `${pk.h * SCALE - 0.2}rem`,
                                        transform: pk.r ? `rotate(${pk.r}deg)` : undefined,
                                        transformOrigin: pk.r ? `${(pk.rx! - pk.x) * SCALE}rem ${(pk.ry! - pk.y) * SCALE}rem` : undefined,
                                    }}
                                >
                                    <button
                                        type="button"
                                        className={`keyboard-key ${isSelected ? 'key-selected' : ''}`}
                                        style={{
                                            width: '100%',
                                            height: '100%',
                                            cursor: 'pointer',
                                            display: 'flex',
                                            alignItems: 'center',
                                            justifyContent: 'center',
                                            fontSize: '0.75rem',
                                            position: 'relative',
                                        }}
                                        onClick={(e) => {
                                            e.preventDefault();
                                            e.stopPropagation();
                                            handleKeyClick(pk.row, pk.col);
                                        }}
                                    >
                                        {isSelected && strictOrder && orderIndex >= 0 && (
                                            <span style={{
                                                fontSize: '0.9rem',
                                                fontWeight: 'bold',
                                            }}>
                                                {orderIndex + 1}
                                            </span>
                                        )}
                                    </button>
                                </div>
                            );
                        })}
                    </div>
                ))}
            </div>
        </div>
    );
}
