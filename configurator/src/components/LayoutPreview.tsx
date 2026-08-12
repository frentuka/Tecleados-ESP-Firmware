import { PREDEFINED_LAYOUTS } from '../utils/predefinedLayouts';
import type { PhysKey } from '../types/device';

interface LayoutPreviewProps {
    layoutId: string;
}

const UNIT_SIZE = 16; // 16px per 1u for a miniature preview

export default function LayoutPreview({ layoutId }: LayoutPreviewProps) {
    const layout: PhysKey[][] = PREDEFINED_LAYOUTS[layoutId];

    if (!layout || layout.length === 0) {
        return (
            <div style={{
                padding: '1rem',
                color: 'rgba(255,255,255,0.5)',
                fontSize: '0.8rem',
                textAlign: 'center',
                border: '1px dashed rgba(255,255,255,0.2)',
                borderRadius: '4px'
            }}>
                No preview available
            </div>
        );
    }

    // Calculate bounding box
    let maxX = 0;
    let maxY = 0;
    layout.forEach(row => {
        row.forEach(key => {
            const right = key.x + (key.w || 1);
            const bottom = key.y + (key.h || 1);
            if (right > maxX) maxX = right;
            if (bottom > maxY) maxY = bottom;
        });
    });

    const width = maxX * UNIT_SIZE;
    const height = maxY * UNIT_SIZE;

    return (
        <div style={{
            position: 'relative',
            width: `${width}px`,
            height: `${height}px`,
            background: 'rgba(0,0,0,0.3)',
            borderRadius: '4px',
            padding: '4px', // Slight padding around the board
            boxSizing: 'content-box'
        }}>
            {layout.flatMap((row, rIdx) => 
                row.map((key, cIdx) => {
                    const w = key.w || 1;
                    const h = key.h || 1;
                    return (
                        <div
                            key={`${rIdx}-${cIdx}`}
                            style={{
                                position: 'absolute',
                                left: `${key.x * UNIT_SIZE + 4}px`,
                                top: `${key.y * UNIT_SIZE + 4}px`,
                                width: `${w * UNIT_SIZE - 2}px`,
                                height: `${h * UNIT_SIZE - 2}px`,
                                background: 'rgba(255,255,255,0.8)',
                                borderRadius: '2px',
                                boxShadow: 'inset 0 0 0 1px rgba(0,0,0,0.5)'
                            }}
                        />
                    );
                })
            )}
        </div>
    );
}
