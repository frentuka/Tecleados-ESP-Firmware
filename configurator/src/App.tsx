import { useState, useEffect, useCallback, useRef } from 'react';
import { hidService, HIDTransport } from './HIDService';
import KeyboardLayoutEditor from './KeyboardLayoutEditor';
import MacrosDashboard from './MacrosDashboard';
import CustomKeysDashboard from './CustomKeysDashboard';
import CombosDashboard from './CombosDashboard';
import StatusWidget from './StatusWidget';
import Sidebar from './Sidebar';
import type { SidebarTab } from './Sidebar';
import SettingsModal from './SettingsModal';
import DevConsoleModal from './DevConsoleModal';
import { useConfirm } from './hooks/useConfirm';
import { useMacros } from './hooks/useMacros';
import { useCustomKeys } from './hooks/useCustomKeys';
import { useCombos } from './hooks/useCombos';
import { useNotificationStore } from './stores/notificationStore';
import { getFlagsString } from './utils/packetUtils';
import type { DeviceStatus, LogMessage, ConnectionNotification } from './types/device';
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
import './assets/css/sidebar.css';

import Background3D from './components/Background3D';
import { useLayoutStore } from './stores/layoutStore';
import { useOnboardingStore } from './stores/onboardingStore';
import OnboardingWizard from './components/onboarding/OnboardingWizard';
import { MACRO_BASE, CKEY_BASE } from './KeyDefinitions';

// Re-export types for backward compatibility — consumers can import from './App'
export type { Macro, MacroElement, MacroAction } from './types/macros';
export type { CustomKey } from './types/customKeys';


function App() {
  const [sidebarTab, setSidebarTab] = useState<SidebarTab>(null);
  const [isSettingsOpen, setIsSettingsOpen] = useState(false);
  const [isConsoleOpen, setIsConsoleOpen] = useState(false);
  const [highlightMacroId, setHighlightMacroId] = useState<number | null>(null);
  const [highlightCkeyId, setHighlightCkeyId] = useState<number | null>(null);
  
  // State for directly opening the editor modal without changing sidebar tab
  const [editMacroId, setEditMacroId] = useState<number | null>(null);
  const [editCkeyId, setEditCkeyId] = useState<number | null>(null);

  const [isConnected, setIsConnected] = useState(false);
  const setLayoutIsConnected = useLayoutStore(state => state.setIsConnected);
  const hasCompletedOnboarding = useOnboardingStore(state => state.hasCompleted);
  const { notification, setNotification, showNotification } = useNotificationStore();
  const [displayedNotification, setDisplayedNotification] = useState<ConnectionNotification | null>(null);
  const [isNotificationHovered, setIsNotificationHovered] = useState(false);
  const dismissTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const [notificationVisible, setNotificationVisible] = useState(false);
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

  const {
    combos,
    comboLimits,
    fetchCombos,
    fetchComboLimits,
    saveCombo: handleSaveCombo,
    deleteCombo: handleDeleteCombo,
  } = useCombos(isConnected, addLog, confirm);

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

  // Sidebar Keyboard Shortcuts
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        if (e.defaultPrevented) return;

        // Close modals managed by App.tsx first
        let handled = false;
        if (isConsoleOpen) {
          setIsConsoleOpen(false);
          handled = true;
        }
        if (isSettingsOpen) {
          setIsSettingsOpen(false);
          handled = true;
        }
        if (handled) return;

        // Don't close sidebar if a child modal or popover is open
        if (document.querySelector('.modal-overlay') || document.querySelector('.key-action-popover')) {
          return;
        }

        // Close sidebar
        if (sidebarTab !== null) {
          e.preventDefault();
          setSidebarTab(null);
        }
        return;
      }

      const isModifier = e.ctrlKey || e.metaKey;
      if (!isModifier) return;

      if (e.key === '1') {
        e.preventDefault();
        setSidebarTab('macros');
      } else if (e.key === '2') {
        e.preventDefault();
        setSidebarTab('ckeys');
      } else if (e.key === '3') {
        e.preventDefault();
        setSidebarTab('combos');
      } else if (e.key.toLowerCase() === 'f') {
        // Only trigger if sidebar is already open
        if (sidebarTab !== null) {
          e.preventDefault();
          const searchInput = document.querySelector('.sidebar-tab-content.active .sidebar-search-input') as HTMLInputElement | null;
          if (searchInput) {
            searchInput.focus();
            searchInput.select();
          }
        }
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [sidebarTab, isConsoleOpen, isSettingsOpen]);

  // Subscribe to HIDService connection state (auto-reconnect, disconnect detection)
  useEffect(() => {
    const handler = (connected: boolean) => {
      setIsConnected(connected);
      setLayoutIsConnected(connected);
      if (!connected) setDeviceStatus(null);

      // Auto-advance onboarding wizard from Step 0 → Step 1 on connect
      if (connected && !useOnboardingStore.getState().hasCompleted) {
        const { step, nextStep } = useOnboardingStore.getState();
        if (step === 0) {
          setTimeout(nextStep, 600);
        }
      }

      // Clear logs and add connection status as the first entry
      setLogs([{
        id: getNextLogId(),
        timestamp: new Date(),
        data: new Uint8Array(64),
        text: connected ? "Device connected" : "Device disconnected"
      }]);
    };
    hidService.onConnectionChange(handler);

    if (isConnected) {
      setNotification(null);
    }

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

  // Auto-dismiss logic with hover protection
  useEffect(() => {
    // 1. Handle clearing (Store notification is null)
    if (!notification) {
      if (notificationVisible) {
        setNotificationVisible(false);
        const clearTimer = setTimeout(() => setDisplayedNotification(null), 500);
        return () => clearTimeout(clearTimer);
      }
      return;
    }

    if (typeof notification.message === 'string' && notification.message === 'COPIED') return;

    // 2. Handle replacement (Visible but content is wrong)
    if (notificationVisible && displayedNotification && displayedNotification.message !== notification.message) {
      setNotificationVisible(false);
      // Wait for fade out before syncing content in next run
      return;
    }

    // 3. Sync content (Hidden but content is wrong or missing)
    if (!notificationVisible && (!displayedNotification || displayedNotification.message !== notification.message)) {
      const syncTimer = setTimeout(() => setDisplayedNotification(notification), 150);
      return () => clearTimeout(syncTimer);
    }

    // 4. Manage visible state and timers (Content is correct)
    if (displayedNotification && displayedNotification.message === notification.message) {
      // Trigger fade in if hidden
      if (!notificationVisible) {
        const showTimer = setTimeout(() => setNotificationVisible(true), 50);
        return () => clearTimeout(showTimer);
      }

      // Manage dismissal timer
      const startDismissTimer = () => {
        if (dismissTimerRef.current) clearTimeout(dismissTimerRef.current);

        const isLinuxFix = typeof notification.message === 'string' && (notification.message === 'PERMISSION_DENIED' || notification.message.includes('System lock'));
        const isSuccess = notification.type === 'success';
        const defaultDuration = isLinuxFix ? 20000 : (isSuccess ? 2500 : 6000);
        const duration = notification.duration ?? defaultDuration;

        dismissTimerRef.current = setTimeout(() => {
          setNotification(null); // This will trigger the "Handle clearing" block in next run
        }, duration);
      };

      if (!isNotificationHovered) {
        startDismissTimer();
      } else {
        if (dismissTimerRef.current) clearTimeout(dismissTimerRef.current);
      }

      return () => {
        if (dismissTimerRef.current) clearTimeout(dismissTimerRef.current);
      };
    }
  }, [notification, displayedNotification, notificationVisible, isNotificationHovered, setNotification]);

  const handleMouseEnter = () => setIsNotificationHovered(true);
  const handleMouseLeave = () => {
    setIsNotificationHovered(false);
    // Timer will be restarted by the useEffect when isNotificationHovered changes
  };

  const handleDismissNotification = () => {
    if (dismissTimerRef.current) clearTimeout(dismissTimerRef.current);
    setNotification(null);
  };

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
      fetchComboLimits();
      fetchCustomKeys();
      fetchMacros();
      fetchCombos();

      const interval = setInterval(fetchStatus, 5000);
      return () => clearInterval(interval);
    }
  }, [isConnected, fetchStatus, fetchMacroLimits, fetchComboLimits, fetchCustomKeys, fetchMacros, fetchCombos]);


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
    setNotification(null);
    const result = await hidService.requestDevice();
    if (!result.ok && result.notification) {
      setNotification(result.notification);
    }
  };

  const handleDisconnect = async () => {
    await hidService.disconnect();
  };

  // ── Contextual Key Selection: auto-open sidebar to the relevant item ──
  const handleKeySelected = useCallback((code: number) => {
    if (code >= MACRO_BASE && code <= 0x40FF) {
      const macroId = code - MACRO_BASE;
      setEditMacroId(null);
      requestAnimationFrame(() => setEditMacroId(macroId));
    } else if (code >= CKEY_BASE && code <= 0x3FFF) {
      const ckeyId = code - CKEY_BASE;
      setEditCkeyId(null);
      requestAnimationFrame(() => setEditCkeyId(ckeyId));
    }
    // For normal HID codes (not macros/ckeys) do nothing — just let the modal open normally
  }, []);


  const DISCONNECTED_MESSAGES = [
    "Disconnected... for now.",
    "Waiting for the spark.",
    "Your keys are resting.",
    "Silence is golden.",
    "Awaiting signal...",
    "Looking for its better half.",
    "Keyboard out of office.",
    "The keys are quiet.",
    "One click away from magic.",
    "Wake up, little keyboard.",
    "Ready to sync.",
    "Tap into the power.",
    "Waiting for a sign.",
    "Your custom layout awaits.",
    "Type-less... temporarily.",
    "In a world of its own.",
    "Awaiting your command.",
    "The board is dormant.",
    "Connection pending...",
    "Ready for a fresh start.",
    "Keyboard.exe is not responding.",
    "Quiet on the set!",
    "Awaiting the digital handshake.",
    "Expecto Connection!",
    "The Chamber of Keys is closed...",
    "A Keyboard is never late, nor is it early."
  ];

  const [disconnectedMessage] = useState(() =>
    DISCONNECTED_MESSAGES[Math.floor(Math.random() * DISCONNECTED_MESSAGES.length)]
  );

  const LINUX_HID_PERMS_FIX_COMMAND = 'echo \'KERNEL==\"hidraw*\", ATTRS{idVendor}==\"303a\", ATTRS{idProduct}==\"1324\", MODE=\"0666\"\' | sudo tee /etc/udev/rules.d/99-tecleados.rules && sudo udevadm control --reload-rules && sudo udevadm trigger';

  return (
    <div className="app-container">
      <Background3D />



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

      {isConnected ? (
        <div className="app-layout">
          <div className="app-main-content">
            <KeyboardLayoutEditor
              isConnected={isConnected}
              isDeveloperMode={isDeveloperMode}
              macros={macros}
              customKeys={customKeys}
              onLog={addLog}
              onEditEntity={handleKeySelected}
            />
          </div>

          <Sidebar
            activeTab={sidebarTab}
            onTabChange={setSidebarTab}
            onSettingsClick={() => setIsSettingsOpen(true)}
            onConsoleClick={() => setIsConsoleOpen(true)}
            isDeveloperMode={isDeveloperMode}
          >
            <div className={`sidebar-tab-content ${sidebarTab === 'macros' ? 'active' : ''}`}>
              <MacrosDashboard
                macros={macros}
                macroLimits={macroLimits}
                isDeveloperMode={isDeveloperMode}
                onSaveMacro={handleSaveMacro}
                onDeleteMacro={handleDeleteMacro}
                onReload={fetchMacros}
                onFetchSingleMacro={fetchSingleMacro}
                highlightId={highlightMacroId}
                editId={editMacroId}
                onClearEditId={() => setEditMacroId(null)}
                isActive={sidebarTab === 'macros'}
              />
            </div>

            <div className={`sidebar-tab-content ${sidebarTab === 'ckeys' ? 'active' : ''}`}>
              <CustomKeysDashboard
                customKeys={customKeys}
                macros={macros}
                isDeveloperMode={isDeveloperMode}
                onSave={handleSaveCustomKey}
                onDelete={handleDeleteCustomKey}
                onReload={fetchCustomKeys}
                highlightId={highlightCkeyId}
                editId={editCkeyId}
                onClearEditId={() => setEditCkeyId(null)}
                isActive={sidebarTab === 'ckeys'}
              />
            </div>

            <div className={`sidebar-tab-content ${sidebarTab === 'combos' ? 'active' : ''}`}>
              <CombosDashboard
                combos={combos}
                comboLimits={comboLimits}
                macros={macros}
                isDeveloperMode={isDeveloperMode}
                onSave={handleSaveCombo}
                onDelete={handleDeleteCombo}
                onReload={fetchCombos}
                isActive={sidebarTab === 'combos'}
              />
            </div>
          </Sidebar>

          <SettingsModal
            isOpen={isSettingsOpen}
            onClose={() => setIsSettingsOpen(false)}
            isConnected={isConnected}
            isDeveloperMode={isDeveloperMode}
            deviceStatus={deviceStatus}
            onLog={addLog}
          />

          <DevConsoleModal
            isOpen={isConsoleOpen}
            onClose={() => setIsConsoleOpen(false)}
            logEntries={logs.map(l => l.text)}
            onClearLog={() => setLogs([])}
          />
        </div>
      ) : hasCompletedOnboarding ? (
        <div className="disconnected-overlay">
          <div className="disconnected-content">
            <div className="disconnected-icon-wrapper">
              <svg className="disconnected-icon" xmlns="http://www.w3.org/2000/svg" width="120" height="120" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1" strokeLinecap="round" strokeLinejoin="round">
                <rect x="2" y="4" width="20" height="16" rx="2" ry="2"></rect>
                <line x1="6" y1="8" x2="6.01" y2="8" strokeWidth="2"></line>
                <line x1="10" y1="8" x2="10.01" y2="8" strokeWidth="2"></line>
                <line x1="14" y1="8" x2="14.01" y2="8" strokeWidth="2"></line>
                <line x1="18" y1="8" x2="18.01" y2="8" strokeWidth="2"></line>
                <line x1="6" y1="12" x2="6.01" y2="12" strokeWidth="2"></line>
                <line x1="10" y1="12" x2="10.01" y2="12" strokeWidth="2"></line>
                <line x1="14" y1="12" x2="14.01" y2="12" strokeWidth="2"></line>
                <line x1="18" y1="12" x2="18.01" y2="12" strokeWidth="2"></line>
                <line x1="7" y1="16" x2="17" y2="16" strokeWidth="2"></line>
              </svg>
              <div className="pulse-ring"></div>
            </div>
            <h2>{disconnectedMessage}</h2>
            <p>Tap the <strong style={{ color: 'var(--success-color)' }}>Connect</strong> button above<br />to start configuring your keyboard.</p>

          </div>
        </div>
      ) : null}

      {/* Global Floating Notifications */}
      {((typeof displayedNotification?.message === 'string' && displayedNotification.message === 'PERMISSION_DENIED') || (typeof displayedNotification?.message === 'string' && displayedNotification.message.includes('System lock'))) && HIDTransport.isLinux() && (
        <div
          className={`permissions-help ${notificationVisible ? 'visible' : ''}`}
          onMouseEnter={handleMouseEnter}
          onMouseLeave={handleMouseLeave}
        >
          <button
            className="btn-notification-close"
            onClick={handleDismissNotification}
            title="Dismiss"
          >
            <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
              <line x1="18" y1="6" x2="6" y2="18"></line>
              <line x1="6" y1="6" x2="18" y2="18"></line>
            </svg>
          </button>
          <h3>
            <svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"></path>
            </svg>
            Linux Configuration Required
          </h3>
          <p>WebHID needs permission to access your keyboard. Run this command in your terminal to fix it:</p>
          <div className="code-block-wrapper">
            <pre className="code-block" style={{ fontSize: '0.75rem' }}>
              {LINUX_HID_PERMS_FIX_COMMAND}
            </pre>
            <button
              className={`btn-copy ${notification?.message === 'COPIED' ? 'copied' : ''}`}
              onClick={() => {
                navigator.clipboard.writeText(LINUX_HID_PERMS_FIX_COMMAND);
                const originalNotification = notification;
                showNotification('COPIED', 'info');
                setTimeout(() => setNotification(originalNotification), 2000);
              }}
              title="Copy command"
            >
              {notification?.message === 'COPIED' ? (
                <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                  <polyline points="20 6 9 17 4 12"></polyline>
                </svg>
              ) : (
                <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                  <rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect>
                  <path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path>
                </svg>
              )}
            </button>
          </div>
          <p style={{ marginTop: '0.75rem', fontSize: '0.8rem', opacity: 0.7 }}>
            After running the command, try connecting again.
          </p>
        </div>
      )}

      {displayedNotification &&
        !(typeof displayedNotification.message === 'string' && displayedNotification.message === 'PERMISSION_DENIED') &&
        !(typeof displayedNotification.message === 'string' && displayedNotification.message.includes('System lock')) &&
        !(typeof displayedNotification.message === 'string' && displayedNotification.message === 'COPIED') && (
          <div
            className={`notification-toast ${displayedNotification.type} ${notificationVisible ? 'visible' : ''} ${displayedNotification.title === 'Benchmark Complete' ? 'bench-toast' : ''}`}
            onMouseEnter={handleMouseEnter}
            onMouseLeave={handleMouseLeave}
          >
            {displayedNotification.title !== 'Benchmark Complete' && displayedNotification.type === 'error' && (
              <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <circle cx="12" cy="12" r="10"></circle>
                <line x1="12" y1="8" x2="12" y2="12"></line>
                <line x1="12" y1="16" x2="12.01" y2="16"></line>
              </svg>
            )}
            {displayedNotification.title !== 'Benchmark Complete' && displayedNotification.type === 'warning' && (
              <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"></path>
                <line x1="12" y1="9" x2="12" y2="13"></line>
                <line x1="12" y1="17" x2="12.01" y2="17"></line>
              </svg>
            )}
            {displayedNotification.title !== 'Benchmark Complete' && displayedNotification.type === 'info' && (
              <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <circle cx="12" cy="12" r="10"></circle>
                <line x1="12" y1="16" x2="12" y2="12"></line>
                <line x1="12" y1="8" x2="12.01" y2="8"></line>
              </svg>
            )}
            {displayedNotification.title !== 'Benchmark Complete' && displayedNotification.type === 'success' && (
              <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                <circle cx="12" cy="12" r="10"></circle>
                <polyline points="9 11 12 14 22 4"></polyline>
                <path d="M22 12A10 10 0 1 1 12 2"></path>
              </svg>
            )}
            <div className={displayedNotification.title === 'Benchmark Complete' ? 'notification-body-bench' : 'notification-body'}>
              {displayedNotification.title && displayedNotification.title !== 'Benchmark Complete' && <div className="notification-title">{displayedNotification.title}</div>}
              <div className="notification-message">{displayedNotification.message}</div>
            </div>
            {typeof displayedNotification.message === 'string' && displayedNotification.message.includes('System lock') && (
              <button
                className="btn-notification-action"
                onClick={(e) => {
                  e.stopPropagation();
                  window.location.reload();
                }}
              >
                Refresh Now
              </button>
            )}
            <button
              className="btn-notification-close"
              onClick={(e) => {
                e.stopPropagation();
                handleDismissNotification();
              }}
              title="Dismiss"
            >
              <svg xmlns="http://www.w3.org/2000/svg" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                <line x1="18" y1="6" x2="6" y2="18"></line>
                <line x1="6" y1="6" x2="18" y2="18"></line>
              </svg>
            </button>
          </div>
        )}

      {/* Onboarding Wizard (renders above everything via portal) */}
      {!hasCompletedOnboarding && (
        <OnboardingWizard
          isConnected={isConnected}
          onConnect={handleConnect}
        />
      )}
    </div>
  );
}

export default App;
