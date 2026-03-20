import { useState, useEffect, useCallback, useRef } from 'react';
import { hidService } from './HIDService';
import KeyboardLayoutEditor from './KeyboardLayoutEditor';
import MacrosDashboard from './MacrosDashboard';
import CustomKeysDashboard from './CustomKeysDashboard';
import StatusWidget from './StatusWidget';
import DevControlsPanel from './components/DevControlsPanel';
import { useConfirm } from './hooks/useConfirm';
import { useMacros } from './hooks/useMacros';
import { useCustomKeys } from './hooks/useCustomKeys';
import { getFlagsString } from './utils/packetUtils';
import type { LogMessage } from './types/device';
import './index.css';

// Re-export types for backward compatibility — consumers can import from './App'
export type { Macro, MacroElement, MacroAction } from './types/macros';
export type { CustomKey } from './types/customKeys';


function App() {
  const [isConnected, setIsConnected] = useState(false);
  const [deviceStatus, setDeviceStatus] = useState<{ mode: number; profile: number; pairing: number; bitmap: number } | null>(null);
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

  // Subscribe to HIDService connection state (auto-reconnect, disconnect detection)
  useEffect(() => {
    const handler = (connected: boolean) => {
      setIsConnected(connected);
      if (!connected) setDeviceStatus(null);
      addLog(connected ? "Device connected" : "Device disconnected");
    };
    hidService.onConnectionChange(handler);

    // Also listen for status updates (pushed from ESP)
    const statusHandler = (status: { mode: number; profile: number; pairing: number; bitmap: number }) => {
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

  // Trigger data fetch on connect
  useEffect(() => {
    if (isConnected) {
      fetchStatus();
      fetchMacroLimits();
      fetchCustomKeys();
      fetchMacros();
    }
  }, [isConnected, fetchStatus, fetchMacroLimits, fetchCustomKeys, fetchMacros]);

  // Raw packet logging (display only — ACKs and reassembly are handled by HIDService)
  const handleLogReceived = useCallback((data: Uint8Array) => {
    if (data.length < 48) return;

    const flags = data[0];
    const remaining = data[1] | (data[2] << 8);
    const payloadLen = data[3];
    const safeLen = Math.min(payloadLen, 43);
    const payloadBytes = data.slice(4, 4 + safeLen);

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
          {!isConnected ? (
            <button className="btn btn-success header-btn" onClick={handleConnect}>
              <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <rect x="3" y="11" width="18" height="11" rx="2" ry="2"></rect>
                <path d="M7 11V7a5 5 0 0 1 10 0v4"></path>
              </svg>
              Connect
            </button>
          ) : (
            <button className="btn btn-danger header-btn" onClick={handleDisconnect}>
              Disconnect
            </button>
          )}
        </div>

        <div className="header-center">
          <StatusWidget
            isConnected={isConnected}
            transportMode={deviceStatus?.mode ?? 0}
            selectedProfile={deviceStatus?.profile ?? 0}
            pairingProfile={deviceStatus?.pairing ?? -1}
            connectedBitmap={deviceStatus?.bitmap ?? 0}
            onOfflineClick={handleConnect}
          />
        </div>

        <div className="header-right">
          <div
            className="header-dev-toggle"
            onClick={() => setIsDeveloperMode(!isDeveloperMode)}
            title="Enable developer options"
          >
            <span style={{ fontSize: '0.75rem', fontWeight: 600, color: isDeveloperMode ? 'var(--accent-color)' : 'var(--text-secondary)' }}>
              DEV MODE
            </span>
            <div
              style={{
                width: '32px',
                height: '16px',
                background: isDeveloperMode ? 'var(--accent-color)' : '#333',
                borderRadius: '16px',
                position: 'relative',
                transition: 'all 0.2s',
              }}
            >
              <div style={{
                width: '10px',
                height: '10px',
                background: '#fff',
                borderRadius: '50%',
                position: 'absolute',
                top: '3px',
                left: isDeveloperMode ? '19px' : '3px',
                transition: 'all 0.2s',
              }} />
            </div>
          </div>
        </div>
      </header>

      {isConnected && (
        <>
          <div className="glass-panel">
            <KeyboardLayoutEditor
              isConnected={isConnected}
              isDeveloperMode={isDeveloperMode}
              macros={macros}
              customKeys={customKeys}
              onLog={addLog}
            />
          </div>

          <div className="glass-panel">
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

          <div className="glass-panel">
            <CustomKeysDashboard
              customKeys={customKeys}
              macros={macros}
              isDeveloperMode={isDeveloperMode}
              onSave={handleSaveCustomKey}
              onDelete={handleDeleteCustomKey}
              onReload={fetchCustomKeys}
            />
          </div>

          {isDeveloperMode && (
            <DevControlsPanel
              isConnected={isConnected}
              logs={logs}
              onClearLogs={() => setLogs([])}
              onAddLog={addLog}
            />
          )}
        </>
      )}
    </div>
  );
}

export default App;
