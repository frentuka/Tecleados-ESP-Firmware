import { create } from 'zustand';
import type { ConnectionNotification, NotificationType } from '../types/device';

interface NotificationState {
    notification: ConnectionNotification | null;
    setNotification: (notification: ConnectionNotification | null) => void;
    showNotification: (message: string, type?: NotificationType) => void;
    clearNotification: () => void;
}

export const useNotificationStore = create<NotificationState>((set) => ({
    notification: null,
    setNotification: (notification) => set({ notification }),
    showNotification: (message, type = 'info') => set({ notification: { message, type } }),
    clearNotification: () => set({ notification: null }),
}));
