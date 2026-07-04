export interface Combo {
    id: number;
    name: string;
    keys: { row: number; col: number }[];
    action: number;
    activeLayers: number[]; // Array of layers where combo is active, e.g., [0, 1]
    strictOrder: boolean;   // If true, keys must be pressed in the exact order defined
    cancelKeys: boolean;    // Default true: release individual keys when combo fires
    delayedPress: boolean;  // Default false: suppress keys during timeout window
    delayMs: number;        // Default 50: suppression window in ms (only when delayedPress=true)
    releaseOnFirstKey: boolean; // Default true: release combo action when first key is released
}

export interface ComboLimits {
    maxCombos: number;
    maxKeys: number;
}
