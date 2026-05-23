export interface Combo {
    id: number;
    name: string;
    keys: { row: number; col: number }[];
    action: number;
    activeLayers: number[]; // Array of layers where combo is active, e.g., [0, 1]
    strictOrder: boolean;   // If true, keys must be pressed in the exact order defined
}
