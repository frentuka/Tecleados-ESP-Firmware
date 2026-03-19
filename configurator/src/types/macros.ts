/**
 * Macro-related types — previously defined in App.tsx.
 */

export type MacroAction = 'tap' | 'press' | 'release';

export type MacroElement =
    | { type: 'key'; key: number; action?: MacroAction; inlineSleep?: number; pressTime?: number }
    | { type: 'sleep'; duration: number };

export interface Macro {
    id: number;
    name: string;
    elements: MacroElement[];
    execMode?: number;
    stackMax?: number;
    repeatCount?: number;
}

export interface ImportableMacro extends Macro {
    tempId: string;
}

export interface MacroLimits {
    maxEvents: number;
    maxMacros: number;
}

export type ModeCategory = 'once' | 'repeat' | 'burst';
