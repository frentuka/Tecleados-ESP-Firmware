import React from 'react';
import {
  SPLIT_STATE_CONNECTED,
  SPLIT_STATE_PAIRING,
  SPLIT_STATE_CONNECTING,
  SPLIT_STATE_DISCONNECTED,
  SPLIT_ROLE_MASTER,
  SPLIT_ROLE_SLAVE,
} from './types/protocol';

interface StatusWidgetProps {
  isConnected: boolean;
  transportMode: number; // 0: USB, 1: BLE
  selectedProfile: number; // 0-8 (displayed as 1-9)
  pairingProfile: number; // 0-8, or -1 if none
  connectedBitmap: number; // 16-bit bitmap
  splitState?: number;  // split_state_t
  splitRole?: number;   // split_role_t
  onOfflineClick?: () => void;
  // BLE action callbacks
  onBleToggleRouting?: () => void;
  onBleConnect?: (profileId: number) => void;
  onBleToggleConn?: (profileId: number) => void;
  onBlePair?: (profileId: number) => void;
}

const StatusWidget: React.FC<StatusWidgetProps> = ({ 
  isConnected, 
  transportMode, 
  selectedProfile, 
  pairingProfile, 
  connectedBitmap, 
  splitState = 0, 
  splitRole = 0, 
  onOfflineClick, 
  onBleToggleRouting, 
  onBleConnect, 
  onBleToggleConn, 
  onBlePair 
}) => {
  const isBle = transportMode === 1;

  // Split state helpers
  const isSplitConnected = splitState === SPLIT_STATE_CONNECTED;
  const isSplitPairing   = splitState === SPLIT_STATE_PAIRING || splitState === SPLIT_STATE_CONNECTING;
  const isSplitDisconnected = splitState === SPLIT_STATE_DISCONNECTED;
  const splitActive = isSplitConnected || isSplitPairing || isSplitDisconnected;
  const splitColor = isSplitConnected ? '#2ecc71' : isSplitPairing ? '#f39c12' : isSplitDisconnected ? '#e74c3c' : 'rgba(255,255,255,0.3)';
  const splitRoleLabel = splitRole === SPLIT_ROLE_MASTER ? 'M' : splitRole === SPLIT_ROLE_SLAVE ? 'S' : '';
  const profileRange = Array.from({ length: 9 }, (_, i) => i); // Indexes 0-8

  return (
    <div className={`status-widget ${isConnected ? 'connected' : 'disconnected'}`}>
      <div 
        className="status-badge" 
        onClick={!isConnected ? onOfflineClick : undefined} 
        title={isConnected ? "Connected" : "Disconnected"}
      >
        <span className="status-dot"></span>
        <span className="status-text">{isConnected ? 'LIVE' : 'OFFLINE'}</span>
      </div>

      {isConnected && (
        <div className="status-content">
          <div className="status-divider-v"></div>
          
          <div className="status-section mode-section">
            <div className={`mode-icon ${!isBle ? 'active' : ''}`} title="USB Mode (always active when connected via USB)">
              <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                <polyline points="16 18 22 12 16 6"></polyline>
                <polyline points="8 6 2 12 8 18"></polyline>
              </svg>
            </div>
            <div className="mode-separator">/</div>
            <div
              className={`mode-icon ${isBle ? 'active' : ''}`}
              title={isBle ? 'BLE active — click to disable' : 'BLE inactive — click to enable'}
              onClick={e => { e.stopPropagation(); onBleToggleRouting?.(); }}
              style={{ cursor: onBleToggleRouting ? 'pointer' : 'default' }}
            >
              <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                <path d="M7 7l10 10-5 5V2l5 5L7 17"></path>
              </svg>
            </div>
          </div>

          {isBle && (
            <>
              <div className="status-divider-v"></div>
              <div className="status-section profiles-section">
                <div className="profiles-grid">
                  {profileRange.map((p) => {
                      const isSelected = selectedProfile === p;
                      const isConnectedProfile = (connectedBitmap & (1 << p)) !== 0;
                      const isPairing = pairingProfile === p;
                      const canClick = !!(onBleConnect || onBleToggleConn || onBlePair);
                      const tooltip = [
                          `Profile ${p + 1}: ${isPairing ? 'Pairing…' : isConnectedProfile ? 'Connected' : 'Disconnected'}${isSelected ? ' (active)' : ''}`,
                          canClick ? 'Click=connect  Dbl-click=toggle  Right-click=pair' : '',
                      ].filter(Boolean).join(' · ');
                      return (
                        <div
                          key={p}
                          className={`profile-indicator ${isSelected ? 'selected' : ''} ${isConnectedProfile ? 'connected-p' : ''} ${isPairing ? 'pairing' : ''}`}
                          title={tooltip}
                          style={{ cursor: canClick ? 'pointer' : 'default' }}
                          onClick={e => { e.stopPropagation(); onBleConnect?.(p); }}
                          onDoubleClick={e => { e.stopPropagation(); onBleToggleConn?.(p); }}
                          onContextMenu={e => { e.preventDefault(); e.stopPropagation(); onBlePair?.(p); }}
                        >
                          {p + 1}
                        </div>
                      );
                  })}
                </div>
              </div>
            </>
          )}

          {/* Split keyboard indicator */}
          {splitActive && (
            <>
              <div className="status-divider-v"></div>
              <div
                className="status-section"
                title={`Split: ${isSplitConnected ? 'Connected' : isSplitPairing ? 'Pairing' : 'Disconnected'}${splitRoleLabel ? ` (${splitRoleLabel === 'M' ? 'Master' : 'Slave'})` : ''}`}
                style={{ gap: 4 }}
              >
                <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke={splitColor} strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                  <rect x="2" y="7" width="20" height="14" rx="2" ry="2"/>
                  <path d="M16 3h-4M12 3v4M8 3h4"/>
                  <line x1="12" y1="7" x2="12" y2="21"/>
                </svg>
                {splitRoleLabel && (
                  <span style={{ fontSize: 10, fontWeight: 800, color: splitColor, letterSpacing: 0.5 }}>
                    {splitRoleLabel}
                  </span>
                )}
              </div>
            </>
          )}
        </div>
      )}

      <style>{`
        .status-widget {
          display: flex;
          align-items: center;
          height: 36px;
          padding: 0 14px;
          background: rgba(255, 255, 255, 0.03);
          border: 1px solid rgba(255, 255, 255, 0.05);
          border-radius: 10px;
          backdrop-filter: blur(10px);
          user-select: none;
          gap: 4px;
          transition: all 0.3s ease;
        }

        .status-badge {
          display: flex;
          align-items: center;
          gap: 8px;
          white-space: nowrap;
          cursor: default;
        }
        
        .disconnected .status-badge {
          cursor: pointer;
        }

        .status-dot {
          width: 8px;
          height: 8px;
          border-radius: 50%;
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
        }

        .status-content {
          display: flex;
          align-items: center;
          animation: fadeIn 0.3s cubic-bezier(0.16, 1, 0.3, 1);
        }

        @keyframes fadeIn {
          from { opacity: 0; transform: translateX(-10px); }
          to { opacity: 1; transform: translateX(0); }
        }

        .status-divider-v {
          width: 1px;
          height: 16px;
          background: rgba(255, 255, 255, 0.15);
          margin: 0 10px;
          flex-shrink: 0;
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
          background: rgba(89, 167, 255, 0.1);
        }

        .mode-separator {
          font-size: 10px;
          opacity: 0.3;
          margin: 0 4px;
        }

        .profiles-grid {
          display: flex;
          gap: 4px;
        }

        .profile-indicator {
          width: 20px;
          height: 20px;
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
            box-shadow: 0 0 8px rgba(88, 166, 255, 0.8);
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
