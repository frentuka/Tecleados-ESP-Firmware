/**
 * Central source of truth for key colors across the configurator.
 * Uses HSL values to allow easy brightness and saturation modifications.
 */

export type KeyCategory = 
  | 'key-normal'
  | 'key-modifier'
  | 'key-system'
  | 'key-macro'
  | 'key-ckey'
  | 'key-fkey'
  | 'key-transparent'
  | 'key-none';

export interface HSL {
  h: number;
  s: number;
  l: number;
}

export const KEY_BASE_COLORS: Record<KeyCategory, HSL> = {
  'key-normal':      { h: 215, s: 20, l: 20 },  // Dark Gray
  'key-modifier':    { h: 212, s: 100, l: 67 }, // Blue (#58a6ff)
  'key-system':      { h: 262, s: 90, l: 70 },  // Purple (#a371f7)
  'key-macro':       { h: 32, s: 91, l: 65 },   // Orange (#f6ad55)
  'key-ckey':        { h: 142, s: 57, l: 62 },  // Green (#68d391)
  'key-fkey':        { h: 155, s: 50, l: 32 },  // Dark Green
  'key-transparent': { h: 215, s: 15, l: 40 },  // Muted Gray
  'key-none':        { h: 215, s: 15, l: 10 },  // Very Dark Gray
};

/**
 * Returns a CSS HSL string for a given category and optional multipliers.
 */
export function getCategoryColor(category: KeyCategory, lMult: number = 1, sMult: number = 1, alpha: number = 1): string {
  const { h, s, l } = KEY_BASE_COLORS[category] || KEY_BASE_COLORS['key-normal'];
  const finalS = Math.min(100, s * sMult);
  const finalL = Math.min(100, l * lMult);
  
  if (alpha < 1) {
    return `hsla(${h}, ${finalS}%, ${finalL}%, ${alpha})`;
  }
  return `hsl(${h}, ${finalS}%, ${finalL}%)`;
}

/**
 * Returns a color for a specific key code.
 */
export function getKeyColor(code: number, lMult: number = 1, sMult: number = 1, alpha: number = 1): string {
  // Use a temporary function to avoid circular dependency if we were to import from KeyDefinitions
  const category = getCategoryFromCode(code);
  return getCategoryColor(category, lMult, sMult, alpha);
}

// Helper duplicated from KeyDefinitions to avoid circular imports if needed, 
// but we'll eventually move the logic here or keep it in KeyDefinitions.
export function getCategoryFromCode(code: number): KeyCategory {
    if (code === 0xFFFF) return 'key-transparent';
    if (code === 0) return 'key-none';
    if (code >= 0x3A && code <= 0x45) return 'key-fkey';
    if (code >= 0x2000 && code <= 0x20FF) return 'key-system';
    if (code >= 0x3000 && code <= 0x3FFF) return 'key-ckey';
    if (code >= 0x4000 && code <= 0x40FF) return 'key-macro';
    const isModifier = code >= 0xE0 && code <= 0xE7;
    const isAction = (code >= 0x28 && code <= 0x2B) || 
        (code >= 0x49 && code <= 0x52) || 
        code === 0x39 || 
        code === 0x65;
    if (isModifier || isAction) return 'key-modifier';
    return 'key-normal';
}
