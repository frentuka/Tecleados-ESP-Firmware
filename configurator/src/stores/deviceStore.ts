/**
 * Device store — global connection state managed by Zustand.
 */

import { create } from 'zustand';
import type { DeviceStatus } from '../types/device';
import type { DeviceController } from '../services/DeviceController';

interface DeviceState {
    isConnected: boolean;
    deviceStatus: DeviceStatus | null;
    isDeveloperMode: boolean;
    controlsEnabled: boolean;
    controller: DeviceController | null;

    // Actions
    setController: (controller: DeviceController) => void;
    setConnected: (connected: boolean) => void;
    setDeviceStatus: (status: DeviceStatus | null) => void;
    setDeveloperMode: (enabled: boolean) => void;
    setControlsEnabled: (enabled: boolean) => void;
    connect: () => Promise<boolean>;
    disconnect: () => Promise<void>;
}

export const useDeviceStore = create<DeviceState>((set, get) => ({
    isConnected: false,
    deviceStatus: null,
    isDeveloperMode: false,
    controlsEnabled: true,
    controller: null,

    setController: (controller) => set({ controller }),
    setConnected: (connected) => set({ isConnected: connected }),
    setDeviceStatus: (status) => set({ deviceStatus: status }),
    setDeveloperMode: (enabled) => set({ isDeveloperMode: enabled }),
    setControlsEnabled: (enabled) => set({ controlsEnabled: enabled }),

    connect: async () => {
        const { controller } = get();
        if (!controller) return false;
        return await controller.requestDevice();
    },

    disconnect: async () => {
        const { controller } = get();
        if (!controller) return;
        await controller.disconnect(true);
        set({ isConnected: false, deviceStatus: null });
    },
}));
