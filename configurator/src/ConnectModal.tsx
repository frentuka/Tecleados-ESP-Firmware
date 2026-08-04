import React from 'react';
import './assets/css/connectModal.css';

interface ConnectModalProps {
  isOpen: boolean;
  onClose: () => void;
  onSelectTransport: (transport: 'usb' | 'ble') => void;
}

const ConnectModal: React.FC<ConnectModalProps> = ({ isOpen, onClose, onSelectTransport }) => {
  if (!isOpen) return null;

  const hasWebHID = 'hid' in navigator;
  const hasWebBluetooth = 'bluetooth' in navigator;

  return (
    <div className="modal-overlay connect-overlay" onClick={onClose}>
      <div className="connect-modal" onClick={e => e.stopPropagation()}>
        <div className="transport-options">
          {hasWebHID && (
            <button className="btn-transport" onClick={() => onSelectTransport('usb')} title="Connect via USB (Chrome Desktop)">
              <div className="transport-icon">
                <svg xmlns="http://www.w3.org/2000/svg" width="22" height="22" viewBox="0 0 24 24" fill="none">
                  <path d="M12 21.5 V 3" stroke="currentColor" strokeWidth="2" />
                  <path d="M12 0 L 9 5 H 15 Z" fill="currentColor" />
                  <path d="M12 13 L 18 9 V 5" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" />
                  <circle cx="6" cy="6" r="2" fill="currentColor" />
                  <path d="M12 15 L 6 10 V 8" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" />
                  <rect x="16.2" y="2.75" width="3.7" height="3.7" fill="currentColor" />
                  <circle cx="12" cy="21.5" r="2" fill="currentColor" />
                </svg>
              </div>
            </button>
          )}
          {hasWebBluetooth && (
            <button className="btn-transport" onClick={() => onSelectTransport('ble')} title="Connect via Bluetooth (Android, iOS, Desktop)">
              <div className="transport-icon">
                <svg xmlns="http://www.w3.org/2000/svg" width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                  <polyline points="6.5 6.5 17.5 17.5 12 23 12 1 17.5 6.5 6.5 17.5"></polyline>
                </svg>
              </div>
            </button>
          )}
          {!hasWebHID && !hasWebBluetooth && (
            <div className="no-transport-error" title="Browser not supported">
              ❌
            </div>
          )}
        </div>
      </div>
    </div>
  );
};

export default ConnectModal;
