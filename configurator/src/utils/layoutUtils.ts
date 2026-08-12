/**
 * layoutUtils.ts — Physical keyboard layout helpers.
 */

import type { PhysKey } from '../types/device';
import { PREDEFINED_LAYOUTS } from './predefinedLayouts';

/**
 * Get a physical layout by its ID (e.g. "stag-65p").
 * Falls back to "stag-65p" if the layout ID is not found.
 */
export function getPredefinedLayout(layoutId: string): PhysKey[][] {
    // Clean null bytes from firmware string
    const cleanId = layoutId.replace(/\0/g, '');
    
    if (PREDEFINED_LAYOUTS[cleanId]) {
        return PREDEFINED_LAYOUTS[cleanId];
    }
    
    console.warn(`Physical layout ID "${cleanId}" not found, falling back to default.`);
    return PREDEFINED_LAYOUTS['stag-65p'] || [];
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
