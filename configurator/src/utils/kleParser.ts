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
 * Rotation properties (r, rx, ry):
 *   r  — rotation angle in degrees (positive = clockwise)
 *   rx — sets the rotation origin X AND resets currentX to rx
 *   ry — sets the rotation origin Y AND resets currentY to ry
 * When rx/ry are set, any x/y in the same object are offsets FROM rx/ry.
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

        let currentY = 0;

        // r / rx / ry are PERSISTENT across KLE rows — they only change when
        // explicitly set by a property object.  (KLE spec: "rotation is sticky".)
        let currentR  = 0;
        let currentRx = 0;
        let currentRy = 0;

        const allKeys: PhysKey[] = [];

        for (const row of data) {
            if (!Array.isArray(row)) continue;

            // Only x, w, h reset per KLE row; y/r/rx/ry are persistent.
            let currentX = 0;
            let currentW = 1;
            let currentH = 1;

            for (const item of row) {
                if (typeof item === 'string') {
                    allKeys.push({
                        row: Math.round(currentY),
                        col: Math.round(currentX),
                        w: currentW,
                        h: currentH,
                        x: currentX,
                        y: currentY,
                        ...(currentR !== 0 ? { r: currentR, rx: currentRx, ry: currentRy } : {}),
                    });
                    currentX += currentW;
                    // Reset per-key properties back to defaults
                    currentW = 1;
                    currentH = 1;
                } else if (typeof item === 'object' && item !== null) {
                    // KLE property-object processing order matters:
                    //   rx  → resets currentX to rx AND resets currentY to currentRy
                    //   ry  → then overrides currentY (and updates currentRy)
                    //   x/y → then applied as offsets
                    if (item.rx !== undefined) {
                        currentRx = item.rx;
                        currentX  = item.rx;
                        currentY  = currentRy; // Y snaps back to the stored rotation origin
                    }
                    if (item.ry !== undefined) {
                        currentRy = item.ry;
                        currentY  = item.ry;
                    }
                    if (item.r !== undefined) currentR = item.r;
                    if (item.x !== undefined) currentX += item.x;
                    if (item.y !== undefined) currentY += item.y;
                    if (item.w !== undefined) currentW = item.w;
                    if (item.h !== undefined) currentH = item.h;
                }
            }
            currentY += 1; // advance Y by one row
        }

        if (allKeys.length === 0) return null;

        // Auto-anchor: shift layout so minimum visual x/y across all (unrotated) keys starts at 0.
        // For rotated keys the actual screen position after rotation may differ, but we anchor
        // on the declared x/y origins to keep the math simple.
        let minX = Infinity;
        let minY = Infinity;
        allKeys.forEach(k => {
            if (k.x < minX) minX = k.x;
            if (k.y < minY) minY = k.y;
        });
        if (minX === Infinity) minX = 0;
        if (minY === Infinity) minY = 0;

        if (minX !== 0 || minY !== 0) {
            allKeys.forEach(k => {
                k.x  -= minX;
                k.y  -= minY;
                k.col = Math.round(k.x);
                k.row = Math.round(k.y);
                // Shift the rotation origin by the same amount so the rotation
                // stays geometrically correct relative to the shifted keys.
                if (k.rx !== undefined) k.rx -= minX;
                if (k.ry !== undefined) k.ry -= minY;
            });
        }

        // Resolve any row/col collisions.
        // Math.round can assign the same {row, col} to two visually distinct keys
        // (common with rotated thumb clusters overlapping the main key grid).
        // When a collision is detected, nudge col upward until the slot is free.
        const seenIds = new Set<string>();
        allKeys.forEach(k => {
            while (seenIds.has(`${k.row}-${k.col}`)) {
                k.col++;
            }
            seenIds.add(`${k.row}-${k.col}`);
        });

        // Re-group into PhysKey[][] by visual row for the editor's renderer.
        const grouped: { [key: number]: PhysKey[] } = {};
        allKeys.forEach(k => {
            if (!grouped[k.row]) grouped[k.row] = [];
            grouped[k.row].push(k);
        });

        const result: PhysKey[][] = Object.keys(grouped)
            .map(Number)
            .sort((a, b) => a - b)
            .map(r => grouped[r].sort((a, b) => a.col - b.col));

        return result;
    } catch (e) {
        console.error('[kleParser] Parse error:', e);
        return null;
    }
}
