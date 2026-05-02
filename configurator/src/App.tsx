import { useState, useEffect, useCallback, useRef } from 'react';
import { hidService } from './HIDService';
import KeyboardLayoutEditor from './KeyboardLayoutEditor';
import MacrosDashboard from './MacrosDashboard';
import CustomKeysDashboard from './CustomKeysDashboard';
import SplitDashboard from './SplitDashboard';
import StatusWidget from './StatusWidget';
import DevControlsPanel from './components/DevControlsPanel';
import DeviceIdentityDashboard from './DeviceIdentityDashboard';
import { useConfirm } from './hooks/useConfirm';
import { useMacros } from './hooks/useMacros';
import { useCustomKeys } from './hooks/useCustomKeys';
import { getFlagsString } from './utils/packetUtils';
import type { DeviceStatus, LogMessage } from './types/device';
import { 
  PAYLOAD_FLAG_FIRST, 
  PAYLOAD_FLAG_LAST,
  PAYLOAD_FLAG_ACK,
  PAYLOAD_FLAG_NAK,
  PAYLOAD_FLAG_OK,
  PAYLOAD_FLAG_ERR,
  PAYLOAD_FLAG_ABORT,
  MODULE_STATUS,
  MODULE_CONFIG,
  CFG_KEY_LAYER_0,
  CFG_KEY_LAYER_1,
  CFG_KEY_LAYER_2,
  CFG_KEY_LAYER_3,
  CFG_KEY_PHYSICAL_LAYOUT
} from './types/protocol';
import './index.css';

import { LayoutIcon, MacrosIcon, SplitIcon, IdentityIcon } from './components/SidebarIcons';

// Re-export types for backward compatibility — consumers can import from './App'
export type { Macro, MacroElement, MacroAction } from './types/macros';
export type { CustomKey } from './types/customKeys';

type ActiveSection = 'layout' | 'macrosCkeys' | 'split' | 'identity';

function App() {
  const [activeSection, setActiveSection] = useState<ActiveSection>('layout');
  const [isConnected, setIsConnected] = useState(false);
  const [deviceStatus, setDeviceStatus] = useState<DeviceStatus | null>(null);
  const [isDeveloperMode, setIsDeveloperMode] = useState<boolean>(() => {
    return localStorage.getItem('isDeveloperMode') === 'true';
  });
  const [logs, setLogs] = useState<LogMessage[]>([]);

  const { confirm } = useConfirm();

  const logIdCounter = useRef<number>(0);

  const getNextLogId = useCallback(() => {
    logIdCounter.current += 1;
    return logIdCounter.current;
  }, []);

  const addLog = useCallback((text: string) => {
    setLogs(prev => [...prev, { id: getNextLogId(), timestamp: new Date(), data: new Uint8Array(0), text }]);
  }, [getNextLogId]);

  const {
    macros,
    macroLimits,
    fetchMacroLimits,
    fetchSingleMacro,
    fetchMacros,
    saveMacro: handleSaveMacro,
    deleteMacro: handleDeleteMacro,
  } = useMacros(isConnected, addLog, confirm);

  const {
    customKeys,
    fetchCustomKeys,
    saveCustomKey: handleSaveCustomKey,
    deleteCustomKey: handleDeleteCustomKey,
  } = useCustomKeys(isConnected, addLog, confirm);

  // Persist Developer Mode
  useEffect(() => {
    localStorage.setItem('isDeveloperMode', isDeveloperMode.toString());
  }, [isDeveloperMode]);

  // Secret code for Developer Mode
  useEffect(() => {
    const secretCode = ['ArrowUp', 'ArrowUp', 'ArrowDown', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'ArrowLeft', 'ArrowRight', 'b', 'a'];
    let codeIndex = 0;

    const handleKeyDown = (e: KeyboardEvent) => {
      // Ignore keydowns if user is typing in an input field
      if (e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement) {
        return;
      }

      const key = e.key;
      // Allow lowercase 'b' and 'a' to match
      const isMatch = key === secretCode[codeIndex] || key.toLowerCase() === secretCode[codeIndex];
      
      if (isMatch) {
        codeIndex++;
        if (codeIndex === secretCode.length) {
          setIsDeveloperMode(prev => !prev);
          codeIndex = 0;
        }
      } else {
        codeIndex = 0;
        // Check if the current key is the start of the sequence
        if (key === secretCode[0]) {
          codeIndex = 1;
        }
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, []);

  // Subscribe to HIDService connection state (auto-reconnect, disconnect detection)
  useEffect(() => {
    const handler = (connected: boolean) => {
      setIsConnected(connected);
      if (!connected) setDeviceStatus(null);
      
      // Clear logs and add connection status as the first entry
      setLogs([{
        id: getNextLogId(),
        timestamp: new Date(),
        data: new Uint8Array(64),
        text: connected ? "Device connected" : "Device disconnected"
      }]);
    };
    hidService.onConnectionChange(handler);

    // Also listen for status updates (pushed from ESP)
    const statusHandler = (status: DeviceStatus) => {
      setDeviceStatus(status);
    };
    hidService.onStatusUpdate(statusHandler);

    return () => {
      hidService.offConnectionChange(handler);
      hidService.offStatusUpdate(statusHandler);
    };
  }, []);

  const fetchStatus = useCallback(async () => {
    if (!isConnected) return;
    const status = await hidService.fetchStatus();
    if (status) {
      setDeviceStatus(status);
    }
  }, [isConnected]);

  // Trigger data fetch on connect and maintain a 5s heartbeat poll for status
  useEffect(() => {
    if (isConnected) {
      fetchStatus();
      fetchMacroLimits();
      fetchCustomKeys();
      fetchMacros();

      const interval = setInterval(fetchStatus, 5000);
      return () => clearInterval(interval);
    }
  }, [isConnected, fetchStatus, fetchMacroLimits, fetchCustomKeys, fetchMacros]);


  // Raw packet logging (display only — ACKs and reassembly are handled by HIDService)
  const handleLogReceived = useCallback((data: Uint8Array) => {
    if (data.length < 48) return;

    const flags = data[0];

    // ── 1. Check for Aggregated Summary (Virtual Packet 0xFF) ──
    if (flags === 0xFF) {
      const text = new TextDecoder().decode(data.slice(4)).replace(/\0/g, '');
      setLogs(prev => [...prev, { id: getNextLogId(), timestamp: new Date(), data, text }]);
      return;
    }

    // ── 2. Handle Noise Filtering ──
    const isFirst = !!(flags & PAYLOAD_FLAG_FIRST);
    const isLast = !!(flags & PAYLOAD_FLAG_LAST);

    // Filter out ACK, NAK, or Process flags (OK/ERR/ABORT) as they are noise
    const isNoise = (flags & (PAYLOAD_FLAG_ACK | PAYLOAD_FLAG_NAK | PAYLOAD_FLAG_OK | PAYLOAD_FLAG_ERR | PAYLOAD_FLAG_ABORT));
    if (isNoise && !(isFirst && isLast)) return;

    // ── 3. High-Level Interpretation ──
    const payloadLen = data[3];
    const safeLen = Math.min(payloadLen, 43);
    const payloadBytes = data.slice(4, 4 + safeLen);

    if (isFirst && isLast && safeLen >= 3) {
      const module = payloadBytes[0];
      // const cmd = payloadBytes[1];
      const keyId = payloadBytes[2];

      if (module === MODULE_STATUS) {
        const json = new TextDecoder().decode(payloadBytes.slice(7)).replace(/\0/g, '');
        const text = `[StatusWidget] Received update${json ? ': ' + json : ''}`;
        setLogs(prev => [...prev, { id: getNextLogId(), timestamp: new Date(), data, text }]);
        return;
      }

      if (module === MODULE_CONFIG) {
        let keyName = `Key ${keyId}`;
        switch (keyId) {
          case CFG_KEY_LAYER_0: keyName = 'Layer 0 (Base)'; break;
          case CFG_KEY_LAYER_1: keyName = 'Layer 1 (FN1)'; break;
          case CFG_KEY_LAYER_2: keyName = 'Layer 2 (FN2)'; break;
          case CFG_KEY_LAYER_3: keyName = 'Layer 3 (FN3)'; break;
          case CFG_KEY_PHYSICAL_LAYOUT: keyName = 'Physical Layout'; break;
        }
        const text = `[Config] Received data for ${keyName}`;
        setLogs(prev => [...prev, { id: getNextLogId(), timestamp: new Date(), data, text }]);
        return;
      }
    }

    // Fallback for single packets that aren't filtered (handshakes or other modules)
    const remaining = data[1] | (data[2] << 8);
    let text = `${getFlagsString(flags)} Len: ${safeLen}, Rem: ${remaining}`;

    if (safeLen > 0) {
      const payloadText = new TextDecoder().decode(payloadBytes).replace(/\0/g, '');
      if (payloadText.trim() === '' || Array.from(payloadBytes).some(b => b < 32 && b !== 9 && b !== 10 && b !== 13)) {
        text += ` => Hex: ${Array.from(payloadBytes).map(b => b.toString(16).padStart(2, '0')).join(' ')}`;
      } else {
        text += ` => Payload: "${payloadText}"`;
      }
    }

    setLogs((prev) => [
      ...prev,
      {
        id: getNextLogId(),
        timestamp: new Date(),
        data,
        text,
      },
    ]);
  }, []);

  useEffect(() => {
    hidService.onLogReceived(handleLogReceived);
    return () => hidService.offLogReceived(handleLogReceived);
  }, [handleLogReceived]);

  const handleConnect = async () => {
    await hidService.requestDevice();
  };

  const handleDisconnect = async () => {
    await hidService.disconnect();
  };

  return (
    <div className="app-container">
      <header className="main-header">
        <div className="header-left">
          <div className="header-brand">
            <div className="brand-logo">TC</div>
            <span className="brand-title">Configurator</span>
          </div>
        </div>

        <div className="header-center">
          <div className={`center-container ${isConnected ? 'connected' : 'disconnected'}`}>
            <button 
              className={`btn-connection ${isConnected ? 'btn-disconnect' : 'btn-connect'}`}
              onClick={isConnected ? handleDisconnect : handleConnect}
              title={isConnected ? 'Disconnect' : 'Connect'}
            >
              {isConnected ? (
                <svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                  <path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"></path>
                  <polyline points="16 17 21 12 16 7"></polyline>
                  <line x1="21" y1="12" x2="9" y2="12"></line>
                </svg>
              ) : (
                <svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                  <path d="M15 3h4a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2h-4"></path>
                  <polyline points="10 17 15 12 10 7"></polyline>
                  <line x1="15" y1="12" x2="3" y2="12"></line>
                </svg>
              )}
            </button>
            <nav className={`header-nav ${isConnected ? 'visible' : 'hidden'}`}>
              <button 
                className={`header-nav-item ${activeSection === 'layout' ? 'active' : ''}`}
                onClick={() => setActiveSection('layout')}
              >
                <LayoutIcon /> <span className="nav-label">Layout</span>
              </button>
              <button 
                className={`header-nav-item ${activeSection === 'macrosCkeys' ? 'active' : ''}`}
                onClick={() => setActiveSection('macrosCkeys')}
              >
                <MacrosIcon /> <span className="nav-label">Macros & CKs</span>
              </button>
              <button 
                className={`header-nav-item ${activeSection === 'split' ? 'active' : ''}`}
                onClick={() => setActiveSection('split')}
              >
                <SplitIcon /> <span className="nav-label">Split</span>
              </button>
              {isDeveloperMode && (
                <button 
                  className={`header-nav-item ${activeSection === 'identity' ? 'active' : ''}`}
                  onClick={() => setActiveSection('identity')}
                >
                  <IdentityIcon /> <span className="nav-label">Identity</span>
                </button>
              )}
            </nav>
          </div>
        </div>

        <div className="header-right">
          <div className={`status-wrapper ${isConnected ? 'visible' : 'hidden'}`}>
            <StatusWidget
              isConnected={isConnected}
              transportMode={deviceStatus?.mode ?? 0}
              selectedProfile={deviceStatus?.profile ?? 0}
              pairingProfile={deviceStatus?.pairing ?? -1}
              connectedBitmap={deviceStatus?.bitmap ?? 0}
              splitState={deviceStatus?.split_state ?? 0}
              splitRole={deviceStatus?.split_role ?? 0}
              onOfflineClick={handleConnect}
              onBleToggleRouting={() => hidService.bleToggleRouting()}
              onBleConnect={p => hidService.bleConnect(p)}
              onBleToggleConn={p => hidService.bleToggleConn(p)}
              onBlePair={p => hidService.blePair(p)}
            />
          </div>
        </div>

      </header>

      {isConnected && (
        <div className="app-layout">
          <div className="app-main-content">
            <div className="app-sections-area">
              <div className={`section-container ${activeSection === 'layout' ? 'active' : ''}`}>
                {activeSection === 'layout' && (
                  <KeyboardLayoutEditor
                    isConnected={isConnected}
                    isDeveloperMode={isDeveloperMode}
                    macros={macros}
                    customKeys={customKeys}
                    onLog={addLog}
                  />
                )}
              </div>

              <div className={`section-container ${activeSection === 'macrosCkeys' ? 'active' : ''}`}>
                {activeSection === 'macrosCkeys' && (
                  <div className="macros-ckeys-split-view">
                    <div className="list-column">
                      <MacrosDashboard
                        macros={macros}
                        macroLimits={macroLimits}
                        isDeveloperMode={isDeveloperMode}
                        onSaveMacro={handleSaveMacro}
                        onDeleteMacro={handleDeleteMacro}
                        onReload={fetchMacros}
                        onFetchSingleMacro={fetchSingleMacro}
                      />
                    </div>
                    <div className="list-column">
                      <CustomKeysDashboard
                        customKeys={customKeys}
                        macros={macros}
                        isDeveloperMode={isDeveloperMode}
                        onSave={handleSaveCustomKey}
                        onDelete={handleDeleteCustomKey}
                        onReload={fetchCustomKeys}
                      />
                    </div>
                  </div>
                )}
              </div>

              <div className={`section-container ${activeSection === 'split' ? 'active' : ''}`}>
                {activeSection === 'split' && (
                  <SplitDashboard
                    isConnected={isConnected}
                    deviceStatus={deviceStatus}
                    isDeveloperMode={isDeveloperMode}
                    onLog={addLog}
                  />
                )}
              </div>

              {isDeveloperMode && (
                <div className={`section-container ${activeSection === 'identity' ? 'active' : ''}`}>
                  {activeSection === 'identity' && (
                    <DeviceIdentityDashboard
                      isConnected={isConnected}
                      onLog={addLog}
                    />
                  )}
                </div>
              )}
            </div>

            {isDeveloperMode && (
              <DevControlsPanel
                isConnected={isConnected}
                logs={logs}
                onClearLogs={() => setLogs([])}
                onAddLog={addLog}
              />
            )}
          </div>
        </div>
      )}
    </div>
  );
}

export default App;
