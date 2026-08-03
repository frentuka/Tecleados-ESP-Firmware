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
    <div className="modal-overlay" onClick={onClose}>
      <div className="modal-content connect-modal" onClick={e => e.stopPropagation()}>
        <div className="modal-header">
          <h2>Connect to Keyboard</h2>
          <button className="btn-close" onClick={onClose} title="Close">
            <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
              <line x1="18" y1="6" x2="6" y2="18"></line>
              <line x1="6" y1="6" x2="18" y2="18"></line>
            </svg>
          </button>
        </div>
        
        <div className="modal-body">
          <div className="transport-options">
            {hasWebHID && (
              <button className="btn-transport" onClick={() => onSelectTransport('usb')}>
                <div className="transport-icon">🔌</div>
                <div className="transport-label">USB</div>
              </button>
            )}
            {hasWebBluetooth && (
              <button className="btn-transport" onClick={() => onSelectTransport('ble')}>
                <div className="transport-icon">📶</div>
                <div className="transport-label">Bluetooth</div>
              </button>
            )}
            {!hasWebHID && !hasWebBluetooth && (
                <div className="no-transport-error">
                    Your browser does not support WebHID or WebBluetooth. Please use a compatible browser like Chrome, Edge, or Opera.
                </div>
            )}
          </div>
          
          <div className="transport-info">
            <p><strong>USB</strong> requires Chrome desktop.</p>
            <p><strong>Bluetooth</strong> works on Android, iOS (Bluefy), and desktop.</p>
          </div>
        </div>
      </div>
    </div>
  );
};

export default ConnectModal;
