import React, { useState, useEffect } from 'react';

interface StatusWidgetProps {
  isConnected: boolean;
  transportMode: number; // 0: USB, 1: BLE
  selectedProfile: number; // 0-8 (displayed as 1-9)
  pairingProfile: number; // 0-8, or -1 if none
  connectedBitmap: number; // 16-bit bitmap
  onExpandChange?: (isExpanded: boolean) => void;
  onOfflineClick?: () => void;
}

const StatusWidget: React.FC<StatusWidgetProps> = ({ isConnected, transportMode, selectedProfile, pairingProfile, connectedBitmap, onExpandChange, onOfflineClick }) => {
  const [isPersistent, setIsPersistent] = useState(false);
  const [isHovered, setIsHovered] = useState(false);
  const isBle = transportMode === 1;
  const profileRange = Array.from({ length: 9 }, (_, i) => i); // Indexes 0-8

  // Track expansion state for collision avoidance
  useEffect(() => {
    if (onExpandChange) {
      onExpandChange(isPersistent || isHovered);
    }
  }, [isPersistent, isHovered, onExpandChange]);

  // Reset persistence if disconnected
  useEffect(() => {
    if (!isConnected) {
      setIsPersistent(false);
    }
  }, [isConnected]);

  const togglePersistent = (e: React.MouseEvent) => {
    e.stopPropagation(); // Avoid triggering any parent handlers
    if (!isConnected) {
      if (onOfflineClick) onOfflineClick();
      return;
    }
    setIsPersistent(!isPersistent);
  };

  return (
    <div
      className={`status-pill ${isConnected ? 'connected' : 'disconnected'} ${isPersistent ? 'persistent' : ''}`}
      onClick={togglePersistent}
      onMouseEnter={() => setIsHovered(true)}
      onMouseLeave={() => setIsHovered(false)}
      title={isConnected ? (isPersistent ? "Click to unlock" : "Click to keep expanded") : "Disconnected"}
    >
      <div className="status-badge">
        <span className="status-dot"></span>
        <span className="status-text">{isConnected ? 'LIVE' : 'OFFLINE'}</span>
      </div>

      {isConnected && (
        <div className="status-expandable">
          <div className="status-divider-v"></div>
          <div className="status-section mode-section">
            <div className={`mode-icon ${!isBle ? 'active' : ''}`} title="USB Mode">
              <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                <polyline points="16 18 22 12 16 6"></polyline>
                <polyline points="8 6 2 12 8 18"></polyline>
              </svg>
            </div>
            <div className="mode-separator">/</div>
            <div className={`mode-icon ${isBle ? 'active' : ''}`} title="BLE Mode">
              <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                <path d="M7 7l10 10-5 5V2l5 5L7 17"></path>
              </svg>
            </div>
          </div>

          <div className={`ble-profiles-wrapper ${isBle ? 'visible' : 'hidden'}`}>
            <div className="status-divider-v"></div>

            <div className="status-section profiles-section">
              <div className="profiles-grid">
                {profileRange.map((p) => {
                    const isSelected = selectedProfile === p;
                    const isConnectedProfile = (connectedBitmap & (1 << p)) !== 0;
                    const isPairing = pairingProfile === p;
                    return (
                      <div
                        key={p}
                        className={`profile-indicator ${isSelected ? 'selected' : ''} ${isConnectedProfile ? 'connected-p' : ''} ${isPairing ? 'pairing' : ''}`}
                        title={`Profile ${p + 1}: ${isPairing ? 'Pairing...' : isConnectedProfile ? 'Connected' : 'Disconnected'}${isSelected ? ' (Selected)' : ''}`}
                      >
                        {p + 1}
                      </div>
                    );
                })}
              </div>
            </div>
          </div>
        </div>
      )}

      <style>{`
        .status-pill {
          display: flex;
          align-items: center;
          height: 36px;
          padding: 1px 0px 0px 10px;
          background: rgba(255, 255, 255, 0.05);
          border: 1px solid rgba(255, 255, 255, 0.1);
          border-radius: 20px;
          backdrop-filter: blur(10px);
          transition: all 0.5s cubic-bezier(0.4, 0, 0.2, 1);
          overflow: hidden;
          cursor: pointer;
          user-select: none;
          width: fit-content;
          box-sizing: border-box;
        }

        .status-pill.connected:hover,
        .status-pill.connected.persistent {
          background: rgba(255, 255, 255, 0.08);
          border-color: rgba(255, 255, 255, 0.2);
          padding-right: 14px;
        }

        .status-pill.persistent {
          border-color: rgba(88, 166, 255, 0.4);
          box-shadow: 0 0 10px rgba(88, 166, 255, 0.1);
        }

        .status-badge {
          display: flex;
          align-items: center;
          align-self: center;
          gap: 6px;
          flex-shrink: 0;
          white-space: nowrap;
        }

        .status-dot {
          width: 8px;
          height: 8px;
          border-radius: 50%;
          transition: background 0.3s ease;
          flex-shrink: 0;
        }

        .connected .status-dot {
          background: #2ecc71;
          box-shadow: 0 0 8px rgba(46, 204, 113, 0.6);
        }

        .disconnected .status-dot {
          background: #e74c3c;
          box-shadow: 0 0 8px rgba(231, 76, 60, 0.6);
        }

        .status-text {
          font-size: 11px;
          font-weight: 800;
          letter-spacing: 0.5px;
          color: rgba(255, 255, 255, 0.9);
          line-height: 1;
          display: flex;
          align-items: center;
          margin-top: 0px;
          margin-right: 12px;
          transition: margin-right 0.5s cubic-bezier(0.4, 0, 0.2, 1);
        }

        .status-pill.connected:hover .status-text,
        .status-pill.connected.persistent .status-text {
          margin-right: 4px;
        }

        .status-expandable {
          display: flex;
          align-items: center;
          opacity: 0;
          max-width: 0;
          transform: translateX(-10px);
          transition: all 0.5s cubic-bezier(0.4, 0, 0.2, 1);
          pointer-events: none;
          overflow: hidden;
        }

        .status-pill:hover .status-expandable,
        .status-pill.persistent .status-expandable {
          opacity: 1;
          max-width: 500px;
          transform: translateX(0);
          pointer-events: auto;
        }

        .status-divider-v {
          width: 1px;
          height: 16px;
          background: rgba(255, 255, 255, 0.15);
          margin: 6px;
          flex-shrink: 0;
        }

        .ble-profiles-wrapper {
          display: flex;
          align-items: center;
          gap: 4px;
          overflow: hidden;
          transition: all 0.5s cubic-bezier(0.4, 0, 0.2, 1);
          opacity: 0;
          max-width: 0;
          transform: scaleX(0.9);
          transform-origin: left;
        }

        .ble-profiles-wrapper.visible {
          opacity: 1;
          max-width: 240px;
          transform: scaleX(1);
        }

        .ble-profiles-wrapper.hidden {
          opacity: 0;
          max-width: 0;
          transform: scaleX(0.8);
          margin-left: 0;
          pointer-events: none;
        }

        .status-section {
          display: flex;
          align-items: center;
        }

        .mode-icon {
          display: flex;
          align-items: center;
          justify-content: center;
          width: 24px;
          height: 24px;
          border-radius: 6px;
          color: rgba(255, 255, 255, 0.3);
          transition: all 0.2s ease;
        }

        .mode-icon.active {
          color: #59a7ffff;
        }

        .mode-separator {
          font-size: 10px;
          opacity: 0.3;
          margin: 8px;
        }

        .profiles-grid {
          display: flex;
          gap: 4px;
          margin: 4px;
        }

        .profile-indicator {
          width: 18px;
          height: 18px;
          display: flex;
          align-items: center;
          justify-content: center;
          font-size: 10px;
          font-weight: 700;
          border-radius: 4px;
          background: rgba(255, 255, 255, 0.05);
          color: rgba(255, 255, 255, 0.3);
          transition: all 0.2s ease;
        }

        .profile-indicator.connected-p {
          color: #2ecc71;
          background: rgba(46, 204, 113, 0.1);
        }

        .profile-indicator.selected {
          color: #fff;
          background: #58a6ff3f;
          box-shadow: 0 0 4px rgba(88, 166, 255, 1);
        }

        .profile-indicator.selected.connected-p {
          color: #67ff95ff;
          background: #2ecc70a1;
          box-shadow: 0 0 8px rgba(46, 180, 204, 0.62);
        }

        .profile-indicator.pairing {
          color: #fff;
          background: rgba(88, 166, 255, 0.2);
          border: 1px solid rgba(88, 166, 255, 0.5);
          animation: profile-pulse 1.5s infinite ease-in-out;
        }

        @keyframes profile-pulse {
          0% {
            box-shadow: 0 0 0px rgba(88, 166, 255, 0.4);
            border-color: rgba(88, 166, 255, 0.5);
          }
          50% {
            box-shadow: 0 0 10px rgba(88, 166, 255, 0.8);
            border-color: rgba(88, 166, 255, 1);
          }
          100% {
            box-shadow: 0 0 0px rgba(88, 166, 255, 0.4);
            border-color: rgba(88, 166, 255, 0.5);
          }
        }
      `}</style>
    </div>
  );
};

export default StatusWidget;
