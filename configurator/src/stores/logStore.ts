/**
 * Log store — global communication log state managed by Zustand.
 */

import { create } from 'zustand';
import type { LogMessage } from '../types/device';

interface LogState {
    logs: LogMessage[];
    nextLogId: number;

    // Actions
    addLog: (data: Uint8Array, text: string) => void;
    clearLogs: () => void;
}

const MAX_LOGS = 200;

export const useLogStore = create<LogState>((set, get) => ({
    logs: [],
    nextLogId: 0,

    addLog: (data, text) => {
        const { nextLogId, logs } = get();
        const entry: LogMessage = {
            id: nextLogId,
            timestamp: new Date(),
            data,
            text,
        };
        const updated = [...logs, entry];
        if (updated.length > MAX_LOGS) {
            updated.splice(0, updated.length - MAX_LOGS);
        }
        set({ logs: updated, nextLogId: nextLogId + 1 });
    },

    clearLogs: () => set({ logs: [], nextLogId: 0 }),
}));
