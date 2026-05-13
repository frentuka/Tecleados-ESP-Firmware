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
}));
