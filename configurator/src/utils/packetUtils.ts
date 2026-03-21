/**
 * Packet display utilities — moved from App.tsx.
 */

import {
    PAYLOAD_FLAG_FIRST,
    PAYLOAD_FLAG_MID,
    PAYLOAD_FLAG_LAST,
    PAYLOAD_FLAG_ACK,
    PAYLOAD_FLAG_NAK,
    PAYLOAD_FLAG_OK,
    PAYLOAD_FLAG_ERR,
    PAYLOAD_FLAG_ABORT,
} from '../types/protocol';

/**
 * Returns a human-readable string of active flag names for a given flags byte.
 */
export function getFlagsString(flags: number): string {
    const parts: string[] = [];
    if (flags & PAYLOAD_FLAG_FIRST) parts.push('FIRST');
    if (flags & PAYLOAD_FLAG_MID) parts.push('MID');
    if (flags & PAYLOAD_FLAG_LAST) parts.push('LAST');
    if (flags & PAYLOAD_FLAG_ACK) parts.push('ACK');
    if (flags & PAYLOAD_FLAG_NAK) parts.push('NAK');
    if (flags & PAYLOAD_FLAG_OK) parts.push('OK');
    if (flags & PAYLOAD_FLAG_ERR) parts.push('ERR');
    if (flags & PAYLOAD_FLAG_ABORT) parts.push('ABORT');
    return parts.length > 0 ? `[${parts.join('|')}]` : '[NONE]';
}

/**
 * Format a Uint8Array as hex string for display.
 */
export function formatHex(data: Uint8Array, maxBytes: number = 16): string {
    const hex = Array.from(data.slice(0, maxBytes))
        .map(b => b.toString(16).padStart(2, '0'))
        .join(' ');
    return data.length > maxBytes ? `${hex} …` : hex;
}
