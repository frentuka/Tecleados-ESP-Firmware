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
 *   {
 *     "rows": 6, "cols": 18,
 *     "layout": [[row,col,w100,h100,x100,y100,...], ...],
 *     "rotation": { "row-col": [r10, rx100, ry100], ... }   // optional
 *   }
 *
 * Each key occupies 6 consecutive integers in a row array.
 * Dimensions are scaled by 100 to avoid floating point in firmware storage.
 * The optional "rotation" map stores rotation data keyed by "row-col" strings;
 * r is stored scaled by 10 (preserving one decimal), rx/ry scaled by 100.
 */
export function parsePhysicalLayoutJson(jsonText: string): PhysKey[][] | null {
    try {
        // The firmware pads the JSON payload with null bytes up to 4096 bytes.
        // We must strip them out, otherwise JSON.parse will throw a SyntaxError.
        const cleanJson = jsonText.replace(/\0/g, '');
        const parsed = JSON.parse(cleanJson);
        if (!parsed.layout || !Array.isArray(parsed.layout)) return null;

        // Optional rotation side-map: { "row-col": [r10, rx100, ry100] }
        const rotMap = parsed.rotation as Record<string, [number, number, number]> | undefined;

        return parsed.layout.map((visualRow: number[]) => {
            const keys: PhysKey[] = [];
            for (let i = 0; i + 5 < visualRow.length; i += 6) {
                const row = visualRow[i];
                const col = visualRow[i + 1];
                const key: PhysKey = {
                    row,
                    col,
                    w:  visualRow[i + 2] / 100,
                    h:  visualRow[i + 3] / 100,
                    x:  visualRow[i + 4] / 100,
                    y:  visualRow[i + 5] / 100,
                };
                const rotEntry = rotMap?.[`${row}-${col}`];
                if (rotEntry) {
                    key.r  = rotEntry[0] / 10;
                    key.rx = rotEntry[1] / 100;
                    key.ry = rotEntry[2] / 100;
                }
                keys.push(key);
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
 *
 * Keys with non-zero rotation are recorded in a separate "rotation" map:
 *   { "row-col": [r10, rx100, ry100] }
 * where r is scaled by 10 (one decimal preserved) and rx/ry by 100.
 * This keeps the base layout array backward-compatible with 6 ints per key.
 */
export function serializePhysicalLayout(
    layout: PhysKey[][],
    matrixRows: number,
    matrixCols: number,
): string {
    const rotationMap: Record<string, [number, number, number]> = {};

    const flat = layout.map(visualRow =>
        visualRow.flatMap(k => {
            if (k.r !== undefined && k.r !== 0) {
                rotationMap[`${k.row}-${k.col}`] = [
                    Math.round(k.r  * 10),
                    Math.round((k.rx ?? 0) * 100),
                    Math.round((k.ry ?? 0) * 100),
                ];
            }
            return [
                Math.round(k.row ?? 0),
                Math.round(k.col ?? 0),
                Math.round((k.w ?? 1) * 100),
                Math.round((k.h ?? 1) * 100),
                Math.round((k.x ?? 0) * 100),
                Math.round((k.y ?? 0) * 100),
            ];
        })
    );

    const result: Record<string, unknown> = { rows: matrixRows, cols: matrixCols, layout: flat };
    if (Object.keys(rotationMap).length > 0) result.rotation = rotationMap;
    return JSON.stringify(result);
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
