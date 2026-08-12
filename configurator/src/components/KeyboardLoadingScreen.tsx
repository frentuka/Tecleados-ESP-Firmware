
import type { PhysKey } from '../types/device';

interface KeyboardLoadingScreenProps {
    physicalLayout: PhysKey[][];
}

export default function KeyboardLoadingScreen({ physicalLayout }: KeyboardLoadingScreenProps) {
    // We compute the true bounding box, accounting for rotated keys, just like the actual editor.
    const rotatePoint = (x: number, y: number, cx: number, cy: number, deg: number): [number, number] => {
        const rad = deg * Math.PI / 180;
        const cos = Math.cos(rad), sin = Math.sin(rad);
        const dx = x - cx, dy = y - cy;
        return [cx + dx * cos - dy * sin, cy + dx * sin + dy * cos];
    };

    let minKeyX = Infinity, minKeyY = Infinity, maxKeyX = -Infinity, maxKeyY = -Infinity;
    physicalLayout.forEach(row => {
        row.forEach(pk => {
            if (pk.r && pk.rx !== undefined && pk.ry !== undefined) {
                const corners: [number, number][] = [
                    [pk.x, pk.y],
                    [pk.x + pk.w, pk.y],
                    [pk.x, pk.y + pk.h],
                    [pk.x + pk.w, pk.y + pk.h],
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
        <div className="loading-screen-container" style={{ position: 'relative' }}>
            <div style={{ width: '100%', overflowX: 'auto', padding: '1rem 0', opacity: 0.85, pointerEvents: 'none' }}>
                <div className="keyboard-grid"
                    style={{
                        position: 'relative',
                        width: `${gridW * 3.2}rem`,
                        height: `${gridH * 3.2}rem`,
                        padding: 0,
                        margin: '0 auto',
                    }}>
                    {physicalLayout.map((physRow: PhysKey[], ri: number) => (
                        <div key={ri} className="keyboard-row">
                            {physRow.map((pk: PhysKey, ci: number) => (
                                <div
                                    key={`${ri}-${ci}`}
                                    style={{
                                        position: 'absolute',
                                        left: `${(pk.x - minKeyX) * 3.2}rem`,
                                        top: `${(pk.y - minKeyY) * 3.2}rem`,
                                        width: `${pk.w * 3.2 - 0.25}rem`,
                                        height: `${pk.h * 3.2 - 0.25}rem`,
                                        transform: pk.r ? `rotate(${pk.r}deg)` : undefined,
                                        transformOrigin: pk.r ? `${(pk.rx! - pk.x) * 3.2}rem ${(pk.ry! - pk.y) * 3.2}rem` : undefined,
                                    }}
                                >
                                    <div className="skeleton-key"></div>
                                </div>
                            ))}
                        </div>
                    ))}
                </div>
            </div>
        </div>
    );
}
