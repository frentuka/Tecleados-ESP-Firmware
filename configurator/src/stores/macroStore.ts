/**
 * Macro store — global macro state managed by Zustand.
 */

import { create } from 'zustand';
import type { Macro, MacroLimits } from '../types/macros';
import type { DeviceController } from '../services/DeviceController';

interface MacroState {
    macros: Macro[];
    macroLimits: MacroLimits | null;
    macroCache: Map<number, Macro>;
    isLoading: boolean;

    // Actions
    fetchMacros: (controller: DeviceController) => Promise<void>;
    fetchMacroLimits: (controller: DeviceController) => Promise<void>;
    fetchSingleMacro: (controller: DeviceController, id: number) => Promise<Macro | null>;
    saveMacro: (controller: DeviceController, macro: Macro) => Promise<boolean>;
    deleteMacro: (controller: DeviceController, id: number) => Promise<boolean>;
    setMacros: (macros: Macro[]) => void;
    clearCache: () => void;
}

export const useMacroStore = create<MacroState>((set, get) => ({
    macros: [],
    macroLimits: null,
    macroCache: new Map(),
    isLoading: false,

    setMacros: (macros) => set({ macros }),
    clearCache: () => set({ macroCache: new Map() }),

    fetchMacros: async (controller) => {
        set({ isLoading: true });
        try {
            const macros = await controller.fetchMacroOutline();
            set({ macros });
        } catch (e) {
            console.error('[MacroStore] fetchMacros error:', e);
        } finally {
            set({ isLoading: false });
        }
    },

    fetchMacroLimits: async (controller) => {
        try {
            const limits = await controller.fetchMacroLimits();
            if (limits) set({ macroLimits: limits });
        } catch (e) {
            console.error('[MacroStore] fetchMacroLimits error:', e);
        }
    },

    fetchSingleMacro: async (controller, id) => {
        const { macroCache } = get();
        if (macroCache.has(id)) return macroCache.get(id)!;

        try {
            const macro = await controller.fetchSingleMacro(id);
            if (macro) {
                const newCache = new Map(get().macroCache);
                newCache.set(id, macro);
                set({ macroCache: newCache });
            }
            return macro;
        } catch (e) {
            console.error('[MacroStore] fetchSingleMacro error:', e);
            return null;
        }
    },

    saveMacro: async (controller, macro) => {
        try {
            const ok = await controller.saveMacro(macro);
            if (ok) {
                // Invalidate cache and refresh list
                const newCache = new Map(get().macroCache);
                newCache.delete(macro.id);
                set({ macroCache: newCache });
                await get().fetchMacros(controller);
            }
            return ok;
        } catch (e) {
            console.error('[MacroStore] saveMacro error:', e);
            return false;
        }
    },

    deleteMacro: async (controller, id) => {
        try {
            const ok = await controller.deleteMacro(id);
            if (ok) {
                const newCache = new Map(get().macroCache);
                newCache.delete(id);
                set({ macroCache: newCache });
                await get().fetchMacros(controller);
            }
            return ok;
        } catch (e) {
            console.error('[MacroStore] deleteMacro error:', e);
            return false;
        }
    },
}));
