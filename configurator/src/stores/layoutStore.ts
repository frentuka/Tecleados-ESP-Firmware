import { create } from 'zustand';
import type { PhysKey, LayerData } from '../types/device';

export interface LayoutMeta {
    id: number;
    name: string;
}

interface LayoutState {
    physicalLayout: PhysKey[][] | null;
    setPhysicalLayout: (layout: PhysKey[][] | null) => void;
    
    layoutMetas: LayoutMeta[];
    setLayoutMetas: (metas: LayoutMeta[]) => void;
    
    layerDataCache: Record<number, LayerData>;
    setLayerDataCache: (updater: Record<number, LayerData> | ((prev: Record<number, LayerData>) => Record<number, LayerData>)) => void;
    
    activeLayerId: number;
    setActiveLayerId: (id: number) => void;
    
    maxLayouts: number;
    setMaxLayouts: (max: number) => void;
    
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
    
    layoutMetas: [],
    setLayoutMetas: (layoutMetas) => set({ layoutMetas }),
    
    layerDataCache: {},
    setLayerDataCache: (updater) => set((state) => ({
        layerDataCache: typeof updater === 'function' ? updater(state.layerDataCache) : updater
    })),
    
    activeLayerId: 0,
    setActiveLayerId: (activeLayerId) => set({ activeLayerId }),
    
    maxLayouts: 4,
    setMaxLayouts: (maxLayouts) => set({ maxLayouts }),
    
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
