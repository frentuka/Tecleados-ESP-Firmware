import { create } from 'zustand';
import type { ConnectionNotification, NotificationType } from '../types/device';

interface NotificationState {
    notification: ConnectionNotification | null;
    setNotification: (notification: ConnectionNotification | null) => void;
    showNotification: (message: string, type?: NotificationType, title?: string, duration?: number) => void;
    clearNotification: () => void;
}

export const useNotificationStore = create<NotificationState>((set) => ({
    notification: null,
    setNotification: (notification) => set({ notification }),
    showNotification: (message, type = 'info', title, duration) => set({ notification: { message, type, title, duration } }),
    clearNotification: () => set({ notification: null }),
}));
