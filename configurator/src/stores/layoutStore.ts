import { create } from 'zustand';
import type { PhysKey, LayerData } from '../types/device';

interface LayoutState {
    physicalLayout: PhysKey[][] | null;
    setPhysicalLayout: (layout: PhysKey[][] | null) => void;
    layers: (LayerData | null)[];
    setLayers: (updater: (LayerData | null)[] | ((prev: (LayerData | null)[]) => (LayerData | null)[])) => void;
    activeLayer: number;
    setActiveLayer: (layer: number) => void;
    isConnected: boolean;
    setIsConnected: (connected: boolean) => void;
    pressedCodes: Set<number>;
    setPressedCodes: (codes: Set<number> | ((prev: Set<number>) => Set<number>)) => void;
    heldTestKeys: Set<string>;
    setHeldTestKeys: (keys: Set<string> | ((prev: Set<string>) => Set<string>)) => void;
}

export const useLayoutStore = create<LayoutState>((set) => ({
    physicalLayout: null,
    setPhysicalLayout: (physicalLayout) => set({ physicalLayout }),
    layers: [null, null, null, null],
    setLayers: (updater) => set((state) => ({
        layers: typeof updater === 'function' ? updater(state.layers) : updater
    })),
    activeLayer: 0,
    setActiveLayer: (activeLayer) => set({ activeLayer }),
    isConnected: false,
    setIsConnected: (isConnected) => set({ isConnected }),
    pressedCodes: new Set(),
    setPressedCodes: (updater) => set((state) => ({
        pressedCodes: typeof updater === 'function' ? updater(state.pressedCodes) : updater
    })),
    heldTestKeys: new Set(),
    setHeldTestKeys: (updater) => set((state) => ({
        heldTestKeys: typeof updater === 'function' ? updater(state.heldTestKeys) : updater
    })),
}));
