/**
 * layoutUtils.ts — Physical keyboard layout helpers.
 *
 * Handles serialisation/deserialisation of the compact 6-tuple layout
 * format used between the device firmware and the configurator.
 */

import type { PhysKey } from '../types/device';

/**
 * Parse a physical layout JSON string received from the device.
 *
 * Device format:
 *   { "rows": 6, "cols": 18, "layout": [[row,col,w100,h100,x100,y100,...], ...] }
 *
 * Each key occupies 6 consecutive integers in a row array.
 * Dimensions are scaled by 100 to avoid floating point in firmware storage.
 */
export function parsePhysicalLayoutJson(jsonText: string): PhysKey[][] | null {
    try {
        const parsed = JSON.parse(jsonText);
        if (!parsed.layout || !Array.isArray(parsed.layout)) return null;

        return parsed.layout.map((visualRow: number[]) => {
            const keys: PhysKey[] = [];
            for (let i = 0; i + 5 < visualRow.length; i += 6) {
                keys.push({
                    row:  visualRow[i],
                    col:  visualRow[i + 1],
                    w:    visualRow[i + 2] / 100,
                    h:    visualRow[i + 3] / 100,
                    x:    visualRow[i + 4] / 100,
                    y:    visualRow[i + 5] / 100,
                });
            }
            return keys;
        });
    } catch {
        return null;
    }
}

/**
 * Serialise a PhysKey[][] to the compact device JSON format.
 *
 * Dimensions are rounded to nearest integer after multiplying by 100,
 * matching the firmware's fixed-point storage.
 */
export function serializePhysicalLayout(
    layout: PhysKey[][],
    matrixRows: number,
    matrixCols: number,
): string {
    const flat = layout.map(visualRow =>
        visualRow.flatMap(k => [
            k.row,
            k.col,
            Math.round(k.w * 100),
            Math.round(k.h * 100),
            Math.round(k.x * 100),
            Math.round(k.y * 100),
        ])
    );
    return JSON.stringify({ rows: matrixRows, cols: matrixCols, layout: flat });
}

/**
 * Return the maximum X extent (in KLE units) across all visual rows.
 * Used to compute the scale factor for rendering keys to CSS pixels.
 */
export function computeLayoutWidth(layout: PhysKey[][]): number {
    let maxX = 0;
    for (const row of layout) {
        for (const k of row) {
            maxX = Math.max(maxX, k.x + k.w);
        }
    }
    return maxX;
}

/**
 * Return the maximum Y extent across all visual rows.
 */
export function computeLayoutHeight(layout: PhysKey[][]): number {
    let maxY = 0;
    for (const row of layout) {
        for (const k of row) {
            maxY = Math.max(maxY, k.y + k.h);
        }
    }
    return maxY;
}
