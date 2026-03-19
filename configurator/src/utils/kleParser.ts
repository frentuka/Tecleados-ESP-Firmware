/**
 * kleParser.ts — Keyboard Layout Editor (KLE) JSON parser.
 *
 * KLE uses a relaxed JSON format with unquoted property keys and
 * per-row instruction objects that carry state for the following keys.
 * This parser normalises the input, then extracts a PhysKey[][] grid.
 */

import type { PhysKey } from '../types/device';

/**
 * Parse raw KLE JSON text (copy-pasted from keyboard-layout-editor.com)
 * into a two-dimensional array of physical key descriptors.
 *
 * KLE format summary
 * ------------------
 * The top-level value is an array of rows.
 * Each row is an array of alternating property objects and key strings:
 *   [ {w:1.5}, "Tab", "Q", {w:2}, "Enter" ]
 * property objects (with non-standard unquoted keys like `{w:2}`) carry
 * state that applies to the next key then reset to defaults.
 *
 * Returns null on any parse failure.
 */
export function parseKleJson(kleText: string): PhysKey[][] | null {
    try {
        let text = kleText.trim();

        // Wrap bare rows (not already wrapped in [[ ]]) into an array-of-arrays
        if (!text.startsWith('[[')) {
            text = '[' + text + ']';
        }

        // Fix unquoted object keys: {w:2} → {"w":2}
        text = text.replace(/({|,)\s*([a-zA-Z_]\w*)\s*:/g, '$1"$2":');

        const data = JSON.parse(text);
        if (!Array.isArray(data)) return null;

        const result: PhysKey[][] = [];
        let matrixRow = 0;
        let currentY = 0;

        for (const row of data) {
            if (!Array.isArray(row)) continue;

            const physRow: PhysKey[] = [];
            let currentX = 0;
            let currentW = 1;
            let currentH = 1;
            let matrixCol = 0;

            for (const item of row) {
                if (typeof item === 'string') {
                    // String entries are key labels — emit a key at current state
                    physRow.push({
                        row: matrixRow,
                        col: matrixCol,
                        w: currentW,
                        h: currentH,
                        x: currentX,
                        y: currentY,
                    });
                    currentX += currentW;
                    matrixCol += Math.ceil(currentW);
                    // Reset per-key properties back to defaults
                    currentW = 1;
                    currentH = 1;
                } else if (typeof item === 'object' && item !== null) {
                    // Property objects modify state for the next key
                    if (item.w !== undefined) currentW = item.w;
                    if (item.h !== undefined) currentH = item.h;
                    if (item.x !== undefined) { currentX += item.x; matrixCol += Math.round(item.x); }
                    if (item.y !== undefined) currentY += item.y;
                }
            }

            if (physRow.length > 0) {
                result.push(physRow);
                matrixRow++;
                currentY += 1; // advance Y by one row
            }
        }

        return result.length > 0 ? result : null;
    } catch (e) {
        console.error('[kleParser] Parse error:', e);
        return null;
    }
}
