import { createPortal } from 'react-dom';
import DeviceDashboard from './DeviceDashboard';
import type { DeviceStatus } from './types/device';

interface SettingsModalProps {
    isOpen: boolean;
    onClose: () => void;
    isConnected: boolean;
    isDeveloperMode: boolean;
    deviceStatus: DeviceStatus | null;
    onLog: (text: string) => void;
}

export default function SettingsModal({
    isOpen,
    onClose,
    isConnected,
    isDeveloperMode,
    deviceStatus,
    onLog,
}: SettingsModalProps) {
    if (!isOpen) return null;

    return createPortal(
        <div className="settings-modal-overlay" onClick={onClose}>
            <div className="settings-modal-content" onClick={e => e.stopPropagation()}>
                <div className="settings-modal-header">
                    <div className="settings-modal-title">
                        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ opacity: 0.6 }}>
                            <circle cx="12" cy="12" r="3" />
                            <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z" />
                        </svg>
                        <span>Device Settings</span>
                    </div>
                    <button className="btn-close" onClick={onClose}>×</button>
                </div>
                <div className="settings-modal-body">
                    <DeviceDashboard
                        isConnected={isConnected}
                        isDeveloperMode={isDeveloperMode}
                        deviceStatus={deviceStatus}
                        onLog={onLog}
                        onClose={onClose}
                    />
                </div>
            </div>
        </div>,
        document.body
    );
}
