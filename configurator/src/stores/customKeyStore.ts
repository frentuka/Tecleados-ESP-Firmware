/**
 * Custom Key store — global custom key state managed by Zustand.
 */

import { create } from 'zustand';
import type { CustomKey } from '../types/customKeys';
import type { DeviceController } from '../services/DeviceController';

interface CustomKeyState {
    customKeys: CustomKey[];
    isLoading: boolean;

    // Actions
    fetchCustomKeys: (controller: DeviceController) => Promise<void>;
    saveCustomKey: (controller: DeviceController, key: CustomKey) => Promise<boolean>;
    deleteCustomKey: (controller: DeviceController, id: number) => Promise<boolean>;
    setCustomKeys: (keys: CustomKey[]) => void;
}

export const useCustomKeyStore = create<CustomKeyState>((set, get) => ({
    customKeys: [],
    isLoading: false,

    setCustomKeys: (keys) => set({ customKeys: keys }),

    fetchCustomKeys: async (controller) => {
        set({ isLoading: true });
        try {
            const keys = await controller.fetchCustomKeys();
            set({ customKeys: keys });
        } catch (e) {
            console.error('[CustomKeyStore] fetchCustomKeys error:', e);
        } finally {
            set({ isLoading: false });
        }
    },

    saveCustomKey: async (controller, key) => {
        try {
            const ok = await controller.saveCustomKey(key);
            if (ok) {
                await get().fetchCustomKeys(controller);
            }
            return ok;
        } catch (e) {
            console.error('[CustomKeyStore] saveCustomKey error:', e);
            return false;
        }
    },

    deleteCustomKey: async (controller, id) => {
        try {
            const ok = await controller.deleteCustomKey(id);
            if (ok) {
                await get().fetchCustomKeys(controller);
            }
            return ok;
        } catch (e) {
            console.error('[CustomKeyStore] deleteCustomKey error:', e);
            return false;
        }
    },
}));
