import { useState, useEffect, useCallback, useRef } from 'react';
import {
  hidService,
  PAYLOAD_FLAG_FIRST,
  PAYLOAD_FLAG_MID,
  PAYLOAD_FLAG_LAST,
  PAYLOAD_FLAG_ACK,
  PAYLOAD_FLAG_NAK,
  PAYLOAD_FLAG_OK,
  PAYLOAD_FLAG_ERR,
  PAYLOAD_FLAG_ABORT,
  MODULE_CONFIG,
  MODULE_SYSTEM,
  CFG_CMD_GET,
  CFG_CMD_SET,
  CFG_KEY_TEST,
  CFG_KEY_HELLO,
  CFG_KEY_PHYSICAL_LAYOUT,
  CFG_KEY_LAYER_0,
  CFG_KEY_LAYER_1,
  CFG_KEY_LAYER_2,
  CFG_KEY_LAYER_3,
  CFG_KEY_MACROS,
  CFG_KEY_MACRO_LIMITS,
  CFG_KEY_MACRO_SINGLE,
  type CustomKey,
} from './HIDService';
import KeyboardLayoutEditor from './KeyboardLayoutEditor';
import MacrosDashboard from './MacrosDashboard';
import CustomKeysDashboard from './CustomKeysDashboard';
import StatusWidget from './StatusWidget';
import { useConfirm } from './hooks/useConfirm';
import './index.css';

export type MacroAction = 'tap' | 'press' | 'release';

export type MacroElement =
  | { type: 'key'; key: number; action?: MacroAction; inlineSleep?: number; pressTime?: number }
  | { type: 'sleep'; duration: number };

export interface Macro {
  id: number;
  name: string;
  elements: MacroElement[];
  execMode?: number;
  stackMax?: number;
  repeatCount?: number;
}

interface LogMessage {
  id: number;
  timestamp: Date;
  data: Uint8Array;
  text: string;
}

function getFlagsString(flags: number): string {
  const active: string[] = [];
  if (flags & PAYLOAD_FLAG_FIRST) active.push('FIRST');
  if (flags & PAYLOAD_FLAG_MID) active.push('MID');
  if (flags & PAYLOAD_FLAG_LAST) active.push('LAST');
  if (flags & PAYLOAD_FLAG_ACK) active.push('ACK');
  if (flags & PAYLOAD_FLAG_NAK) active.push('NAK');
  if (flags & PAYLOAD_FLAG_OK) active.push('OK');
  if (flags & PAYLOAD_FLAG_ERR) active.push('ERR');
  if (flags & PAYLOAD_FLAG_ABORT) active.push('ABORT');
  return active.length > 0 ? `[${active.join('|')}]` : '[NONE]';
}

function App() {
  const [isConnected, setIsConnected] = useState(false);
  const [deviceStatus, setDeviceStatus] = useState<{ mode: number; profile: number; pairing: number; bitmap: number } | null>(null);
  const [controlsEnabled, setControlsEnabled] = useState(false);
  const [isDeveloperMode, setIsDeveloperMode] = useState<boolean>(() => {
    return localStorage.getItem('isDeveloperMode') === 'true';
  });
  const [logs, setLogs] = useState<LogMessage[]>([]);
  const [macros, setMacros] = useState<Macro[]>([]);
  const [macroLimits, setMacroLimits] = useState<{ maxEvents: number; maxMacros: number } | null>(null);
  const macroCache = useRef<Record<number, Macro>>({});
  const macrosRef = useRef<Macro[]>([]);

  // Custom Keys state
  const [customKeys, setCustomKeys] = useState<CustomKey[]>([]);

  // Sync macrosRef with macros state (backup)
  // This helps when macros is set from outside (like onReload)
  useEffect(() => {
    macrosRef.current = macros;
  }, [macros]);

  const { confirm } = useConfirm();

  // Persist Developer Mode
  useEffect(() => {
    localStorage.setItem('isDeveloperMode', isDeveloperMode.toString());
  }, [isDeveloperMode]);

  // Subscribe to HIDService connection state (auto-reconnect, disconnect detection)
  useEffect(() => {
    const handler = (connected: boolean) => {
      setIsConnected(connected);
      if (!connected) setDeviceStatus(null);
      setLogs(prev => [...prev, { id: getNextLogId(), timestamp: new Date(), data: new Uint8Array(0), text: connected ? "Device connected" : "Device disconnected" }]);
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

  // Remove polling logic (no longer needed with push updates)

  const [selectedModule, setSelectedModule] = useState<number>(MODULE_CONFIG);
  // Default to SET because the dynamic UI implicitly wants to SET what the GET retrieved.
  // We'll auto request a GET under the hood when selectedModule/selectedKey changes.
  const selectedCmd: number = CFG_CMD_SET;
  const [selectedKey, setSelectedKey] = useState<number>(CFG_KEY_TEST);

  // Dynamic configuration data state
  const [configData, setConfigData] = useState<Record<string, any> | null>(null);
  const [isFetching, setIsFetching] = useState<boolean>(false);

  const logsEndRef = useRef<HTMLDivElement>(null);
  const logIdCounter = useRef<number>(0);

  const getNextLogId = useCallback(() => {
    logIdCounter.current += 1;
    return logIdCounter.current;
  }, []);


  // logsEndRef kept for optional manual scroll, but no auto-scroll

  // Handle requesting config data when module, key, or connection status changes
  const fetchConfigData = useCallback(async (module: number, key: number) => {
    if (!isConnected || !controlsEnabled || module !== MODULE_CONFIG) {
      setConfigData(null);
      return;
    }

    setIsFetching(true);
    setConfigData(null);

    const buf = new Uint8Array(3);
    buf[0] = module;
    buf[1] = CFG_CMD_GET;
    buf[2] = key;

    const resp = await hidService.sendCommand(buf);
    if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
      try {
        setConfigData(JSON.parse(resp.jsonText));
      } catch (e) {
        console.error('Config JSON Parse Error:', e);
        setConfigData({});
      }
    } else {
      setConfigData({});
    }
    setIsFetching(false);
  }, [isConnected]);

  const fetchStatus = useCallback(async () => {
    if (!isConnected) return;
    const status = await hidService.fetchStatus();
    if (status) {
      setDeviceStatus(status);
    }
  }, [isConnected]);

  const fetchMacroLimits = useCallback(async () => {
    if (!isConnected) return;

    const buf = new Uint8Array(3);
    buf[0] = MODULE_CONFIG;
    buf[1] = CFG_CMD_GET;
    buf[2] = CFG_KEY_MACRO_LIMITS;

    const resp = await hidService.sendCommand(buf);
    if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
      try {
        const parsed = JSON.parse(resp.jsonText);
        if (parsed.maxEvents && parsed.maxMacros) {
          setMacroLimits({ maxEvents: parsed.maxEvents, maxMacros: parsed.maxMacros });
          console.log(`Macro limits: maxEvents=${parsed.maxEvents}, maxMacros=${parsed.maxMacros}`);
        }
      } catch (e) {
        console.error('Macro Limits JSON Parse Error:', e);
      }
    }
  }, [isConnected]);

  const fetchSingleMacro = useCallback(async (id: number): Promise<Macro | null> => {
    if (!isConnected) return null;
    if (macroCache.current[id]) {
      // Hydrate from cache if we somehow have it
      const cached = macroCache.current[id];
      const newList = macrosRef.current.map(m => m.id === id ? cached : m);
      macrosRef.current = newList;
      setMacros(newList);
      return cached;
    }

    const jsonStr = JSON.stringify({ id });
    const jsonBytes = new TextEncoder().encode(jsonStr);
    const buf = new Uint8Array(3 + jsonBytes.length);
    buf[0] = MODULE_CONFIG;
    buf[1] = CFG_CMD_GET;
    buf[2] = CFG_KEY_MACRO_SINGLE;
    buf.set(jsonBytes, 3);

    const resp = await hidService.sendCommand(buf);
    if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
      try {
        setLogs(prev => [...prev, { id: getNextLogId(), timestamp: new Date(), data: new Uint8Array(0), text: `Fetching details for macro ID ${id}...` }]);
        const parsed = JSON.parse(resp.jsonText) as Macro;
        macroCache.current[id] = parsed; // Cache it

        // Update state to hydrate UI elements
        const base = macrosRef.current;
        const newList = base.map(m => m.id === id ? parsed : m);

        macrosRef.current = newList; // Synchronous update for microtask safety
        setMacros(newList);

        setLogs(prev => [...prev, { id: getNextLogId(), timestamp: new Date(), data: new Uint8Array(0), text: `Details for macro "${parsed.name}" loaded.` }]);

        return parsed;
      } catch (e) {
        console.error('Single Macro JSON Parse Error:', e);
      }
    }
    return null;
  }, [isConnected]);

  const fetchMacros = useCallback(async () => {
    if (!isConnected) return;

    setIsFetching(true);
    const buf = new Uint8Array(3);
    buf[0] = MODULE_CONFIG;
    buf[1] = CFG_CMD_GET;
    buf[2] = CFG_KEY_MACROS;

    const resp = await hidService.sendCommand(buf);
    if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
      try {
        const parsed = JSON.parse(resp.jsonText);
        let list: Macro[] = [];
        if (Array.isArray(parsed)) {
          list = parsed;
        } else if (parsed.macros && Array.isArray(parsed.macros)) {
          list = parsed.macros;
        }
        setLogs(prev => [...prev, { id: getNextLogId(), timestamp: new Date(), data: new Uint8Array(0), text: `Found ${list.length} macros on device` }]);

        // Sort alphabetically by name before setting and fetching details
        list.sort((a, b) => (a.name || '').localeCompare(b.name || ''));

        macrosRef.current = list;
        setMacros(list);
        macroCache.current = {}; // Reset cache on full list fetch
        setLogs(prev => [...prev, { id: getNextLogId(), timestamp: new Date(), data: new Uint8Array(0), text: `Initialized ${list.length} macros. Fetching details...` }]);

        // Sequentially fetch elements for each macro to respect USB limitations
        for (const m of list) {
          let retries = 3;
          let success = false;
          while (retries > 0 && !success) {
            const result = await fetchSingleMacro(m.id);
            if (result) {
              success = true;
            } else {
              retries--;
              if (retries > 0) {
                console.warn(`[App] Macro ID ${m.id} fetch failed, retrying... (${retries} left)`);
                // Wait a bit longer on retry
                await new Promise(r => setTimeout(r, 1000));
              } else {
                console.error(`[App] Macro ID ${m.id} failed to fetch after multiple attempts.`);
              }
            }
          }
        }

      } catch (e) {
        console.error('Macros JSON Parse Error:', e);
      }
    }
    setIsFetching(false);
  }, [isConnected, fetchSingleMacro]);

  const handleSaveMacro = async (newMacro: Macro) => {
    let macroToSave = newMacro;
    let isNew = false;
    const maxAllowedId = macroLimits ? macroLimits.maxMacros - 1 : 31;

    if (newMacro.id === -1) {
      // Find the smallest available ID using the ref (latest state)
      const existingIds = new Set(macrosRef.current.map(m => m.id));
      let nextId = 0;
      while (existingIds.has(nextId)) nextId++;

      if (nextId > maxAllowedId) {
        throw new Error(`Maximum number of macros reached. Max allowed is ${maxAllowedId + 1}.`);
      }

      macroToSave = { ...newMacro, id: nextId };
      isNew = true;

      // OPTIMISTIC RESERVATION: Update ref and state IMMEDIATELY to prevent collisions in sequential calls
      const newList = [...macrosRef.current, macroToSave];
      macrosRef.current = newList;
      setMacros(newList);
    } else {
      if (macroToSave.id > maxAllowedId) {
        throw new Error(`Macro ID ${macroToSave.id} exceeds maximum allowed ID of ${maxAllowedId}.`);
      }
    }

    // Send only the single macro via CFG_KEY_MACRO_SINGLE
    const jsonStr = JSON.stringify(macroToSave);
    const jsonBytes = new TextEncoder().encode(jsonStr);
    const buf = new Uint8Array(3 + jsonBytes.length);
    buf[0] = MODULE_CONFIG;
    buf[1] = CFG_CMD_SET;
    buf[2] = CFG_KEY_MACRO_SINGLE;
    buf.set(jsonBytes, 3);

    const resp = await hidService.sendCommand(buf);
    if (resp && resp.status === 0) {
      // Final merge: ensure the specific card is updated and deduplicated by ID
      const currentList = macrosRef.current;
      const newList = currentList.map(m => m.id === macroToSave.id ? macroToSave : m);
      const deduplicated = Array.from(new Map(newList.map(m => [m.id, m])).values());

      macrosRef.current = deduplicated;
      setMacros(deduplicated);

      macroCache.current[macroToSave.id] = macroToSave; // Update cache gracefully
      setLogs(prev => [...prev, { id: getNextLogId(), timestamp: new Date(), data: new Uint8Array(0), text: `Macro "${macroToSave.name}" saved to device (ID: ${macroToSave.id})` }]);
    } else {
      // ROLLBACK if it was a new reservation that failed
      if (isNew) {
        const newList = macrosRef.current.filter(m => m.id !== macroToSave.id);
        macrosRef.current = newList;
        setMacros(newList);
      }
      const errMsg = resp ? `Device error (0x${resp.status.toString(16).toUpperCase()})` : 'Device timeout';
      setLogs(prev => [...prev, { id: getNextLogId(), timestamp: new Date(), data: new Uint8Array(0), text: `Failed to save macro: ${errMsg}` }]);
      throw new Error(errMsg);
    }
  };

  const handleDeleteMacro = async (id: number) => {
    const isConfirmed = await confirm(
      'Delete Macro',
      'Are you sure you want to delete this macro? Any keys mapped to it will stop working.'
    );
    if (!isConfirmed) return;

    // Send delete command via CFG_KEY_MACRO_SINGLE
    const jsonStr = JSON.stringify({ delete: id });
    const jsonBytes = new TextEncoder().encode(jsonStr);
    const buf = new Uint8Array(3 + jsonBytes.length);
    buf[0] = MODULE_CONFIG;
    buf[1] = CFG_CMD_SET;
    buf[2] = CFG_KEY_MACRO_SINGLE;
    buf.set(jsonBytes, 3);

    const resp = await hidService.sendCommand(buf);
    if (resp && resp.status === 0) {
      const newList = macrosRef.current.filter(m => m.id !== id);
      macrosRef.current = newList;
      setMacros(newList);
      delete macroCache.current[id]; // Remove from cache
      setLogs(prev => [...prev, { id: getNextLogId(), timestamp: new Date(), data: new Uint8Array(0), text: `Macro deleted. ${macrosRef.current.length} remaining.` }]);
    } else {
      const errMsg = resp ? `Device error (0x${resp.status.toString(16).toUpperCase()})` : 'Device timeout';
      setLogs(prev => [...prev, { id: getNextLogId(), timestamp: new Date(), data: new Uint8Array(0), text: `Failed to delete macro: ${errMsg}` }]);
      throw new Error(errMsg);
    }
  };

  const fetchSingleCustomKey = useCallback(async (id: number): Promise<CustomKey | null> => {
    if (!isConnected) return null;
    try {
      const detail = await hidService.fetchCustomKeySingle(id);
      if (detail) {
        setCustomKeys(prev => {
          const newList = prev.map(k => k.id === id ? detail : k);
          return [...newList].sort((a, b) => a.id - b.id);
        });
        return detail;
      }
    } catch (e) {
      console.error(`[App] Failed to fetch custom key ${id}:`, e);
    }
    return null;
  }, [isConnected]);

  const fetchCustomKeys = useCallback(async () => {
    if (!isConnected) return;
    setIsFetching(true);
    try {
      const outline = await hidService.fetchCustomKeys();
      setCustomKeys(outline.sort((a, b) => a.id - b.id));
      
      setLogs(prev => [...prev, { id: getNextLogId(), timestamp: new Date(), data: new Uint8Array(0), text: `Found ${outline.length} custom keys. Fetching details...` }]);

      // Sequentially fetch details for each custom key
      for (const k of outline) {
        await fetchSingleCustomKey(k.id);
      }
      setLogs(prev => [...prev, { id: getNextLogId(), timestamp: new Date(), data: new Uint8Array(0), text: `All custom key details loaded.` }]);
    } catch (e) {
      console.error('[App] Failed to fetch custom keys:', e);
    }
    setIsFetching(false);
  }, [isConnected, fetchSingleCustomKey]);

  useEffect(() => {
    if (isConnected) {
      fetchStatus();
      fetchMacroLimits();
      fetchCustomKeys();
    }
  }, [isConnected, fetchStatus, fetchMacroLimits, fetchCustomKeys]);


  const handleSaveCustomKey = async (ckey: CustomKey): Promise<void> => {
    let ckeyToSave = ckey;
    if (ckey.id === -1) {
      const usedIds = new Set(customKeys.map(k => k.id));
      let nextId = 0;
      while (usedIds.has(nextId)) nextId++;
      if (nextId >= 32) throw new Error('Maximum number of custom keys (32) reached.');
      ckeyToSave = { ...ckey, id: nextId };
    }
    const ok = await hidService.saveCustomKey(ckeyToSave);
    if (!ok) throw new Error('Failed to save custom key to device');
    setCustomKeys(prev => {
      const filtered = prev.filter(k => k.id !== ckeyToSave.id);
      return [...filtered, ckeyToSave].sort((a, b) => a.id - b.id);
    });
  };

  const handleDeleteCustomKey = async (id: number): Promise<void> => {
    const isConfirmed = await confirm(
      'Delete Custom Key',
      'Are you sure? Any keys mapped to this custom key will stop working.'
    );
    if (!isConfirmed) return;
    const ok = await hidService.deleteCustomKey(id);
    if (!ok) throw new Error('Failed to delete custom key from device');
    setCustomKeys(prev => prev.filter(k => k.id !== id));
  };


  useEffect(() => {
    if (isConnected && controlsEnabled) {
      fetchConfigData(selectedModule, selectedKey);
    }
  }, [isConnected, controlsEnabled, selectedModule, selectedKey, fetchConfigData]);

  useEffect(() => {
    if (isConnected) {
      fetchMacros();
    }
  }, [isConnected, fetchMacros]);

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


  const handleSendCustomPayload = async () => {
    let payloadBytes = new Uint8Array(0);

    // Only encode payload data if command is SET and we have a valid form object
    if (selectedModule === MODULE_CONFIG && selectedCmd === CFG_CMD_SET && configData) {
      const encoder = new TextEncoder();
      const stringifiedPayload = JSON.stringify(configData);
      payloadBytes = encoder.encode(stringifiedPayload);
    }

    // Build payload: [MODULE, CMD, KEY, ...data]
    const buf = new Uint8Array(3 + payloadBytes.length);
    buf[0] = selectedModule;
    buf[1] = selectedCmd;
    buf[2] = selectedKey;
    if (payloadBytes.length > 0) {
      buf.set(payloadBytes, 3);
    }

    const cmdStr = selectedCmd === CFG_CMD_GET ? 'GET' : 'SET';
    const keyNames: Record<number, string> = {
      [CFG_KEY_TEST]: 'TEST', [CFG_KEY_HELLO]: 'HELLO',
      [CFG_KEY_PHYSICAL_LAYOUT]: 'PHYSICAL_LAYOUT',
      [CFG_KEY_LAYER_0]: 'LAYER_0', [CFG_KEY_LAYER_1]: 'LAYER_1',
      [CFG_KEY_LAYER_2]: 'LAYER_2', [CFG_KEY_LAYER_3]: 'LAYER_3',
    };
    const keyStr = keyNames[selectedKey] ?? `KEY(${selectedKey})`;
    const modStr = selectedModule === MODULE_CONFIG ? 'CONFIG' : 'SYSTEM';

    console.log(`Sending ${modStr} ${cmdStr} ${keyStr}, Total Len: ${buf.length}`);

    const resp = await hidService.sendCommand(buf);

    if (resp && selectedCmd === CFG_CMD_SET) {
      // After SET, refetch to show updated data
      await fetchConfigData(selectedModule, selectedKey);
    } else if (resp && selectedCmd === CFG_CMD_GET && resp.jsonText.trim().length > 0) {
      try {
        setConfigData(JSON.parse(resp.jsonText));
      } catch (e) {
        console.error('Config JSON Parse Error:', e);
        setConfigData({});
      }
      setIsFetching(false);
    }

    setLogs((prev) => [
      ...prev,
      {
        id: getNextLogId(),
        timestamp: new Date(),
        data: buf,
        text: `Sent [${modStr}] Cmd: ${cmdStr}, Key: ${keyStr}, Len: ${payloadBytes.length}`,
      },
    ]);
  };

  const renderConfigForm = () => {
    if (isFetching) {
      return <div style={{ color: 'var(--text-secondary)', padding: '1rem 0' }}>Fetching configuration...</div>;
    }

    if (!configData || Object.keys(configData).length === 0) {
      return <div style={{ color: 'var(--text-secondary)', padding: '1rem 0' }}>No configuration data retrieved.</div>;
    }

    return (
      <div style={{ display: 'flex', flexDirection: 'column', gap: '0.75rem', marginTop: '1rem', padding: '1rem', background: 'var(--bg-color)', borderRadius: '6px', border: '1px solid var(--border-color)' }}>
        <h4 style={{ marginBottom: '0.5rem', marginTop: 0 }}>Configuration Form</h4>
        {Object.entries(configData).map(([key, value]) => {
          const isBoolean = typeof value === 'boolean';
          const isNumber = typeof value === 'number';

          return (
            <div key={key} style={{ display: 'flex', alignItems: 'center', gap: '1rem' }}>
              <label style={{ width: '120px', fontSize: '0.9rem', fontWeight: 500 }}>
                {key}
              </label>
              {isBoolean ? (
                <input
                  type="checkbox"
                  checked={value as boolean}
                  onChange={(e) => {
                    setConfigData({ ...configData, [key]: e.target.checked });
                  }}
                  style={{ width: '20px', height: '20px' }}
                />
              ) : (
                <input
                  type={isNumber ? "number" : "text"}
                  value={value as string | number}
                  onChange={(e) => {
                    const newValue = isNumber ? Number(e.target.value) : e.target.value;
                    setConfigData({ ...configData, [key]: newValue });
                  }}
                  style={{
                    flex: 1,
                    padding: '0.5rem',
                    borderRadius: '4px',
                    border: '1px solid var(--border-color)',
                    background: 'var(--bg-color)',
                    color: 'var(--text-primary)',
                  }}
                />
              )}
            </div>
          );
        })}
      </div>
    );
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
              onLog={(text: string) => setLogs(prev => [...prev, { id: getNextLogId(), timestamp: new Date(), data: new Uint8Array(0), text }])}
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
            <div className="dashboard glass-panel">
              <div className="controls-panel">
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '1rem' }}>
                  <h3 style={{ margin: 0 }}>Controls</h3>
                  <label style={{ display: 'flex', alignItems: 'center', gap: '0.6rem', cursor: 'pointer', userSelect: 'none' }}>
                    <span style={{ fontSize: '0.8rem', fontWeight: 600, color: controlsEnabled ? 'var(--accent-color)' : 'var(--text-secondary)' }}>
                      {controlsEnabled ? 'ENABLED' : 'DISABLED'}
                    </span>
                    <div
                      onClick={() => setControlsEnabled(!controlsEnabled)}
                      style={{
                        width: '40px',
                        height: '20px',
                        background: controlsEnabled ? 'var(--accent-color)' : '#333',
                        borderRadius: '20px',
                        position: 'relative',
                        transition: 'all 0.2s',
                      }}
                    >
                      <div style={{
                        width: '14px',
                        height: '14px',
                        background: '#fff',
                        borderRadius: '50%',
                        position: 'absolute',
                        top: '3px',
                        left: controlsEnabled ? '23px' : '3px',
                        transition: 'all 0.2s',
                      }} />
                    </div>
                  </label>
                </div>

                {controlsEnabled ? (
                  <>
                    <p style={{ color: 'var(--text-secondary)', marginBottom: '1.5rem', fontSize: '0.9rem' }}>
                      Send a COMM HID report dynamically utilizing CRC-8 packet structure.
                    </p>

                    <div style={{ marginBottom: '1.5rem', display: 'flex', flexDirection: 'column', gap: '0.75rem' }}>
                      <div>
                        <label style={{ display: 'block', marginBottom: '0.25rem', fontSize: '0.9rem' }}>Target Module:</label>
                        <select
                          value={selectedModule}
                          onChange={(e) => setSelectedModule(Number(e.target.value))}
                          style={{ width: '100%', padding: '0.5rem', borderRadius: '4px', background: 'var(--bg-color)', color: 'var(--text-primary)', border: '1px solid var(--border-color)' }}
                        >
                          <option value={MODULE_CONFIG}>Config Module</option>
                          <option value={MODULE_SYSTEM}>System Module</option>
                        </select>
                      </div>

                      {selectedModule === MODULE_CONFIG && (
                        <>
                          <div style={{ display: 'flex', gap: '0.5rem' }}>
                            <div style={{ flex: 1 }}>
                              <label style={{ display: 'block', marginBottom: '0.25rem', fontSize: '0.9rem' }}>Key ID:</label>
                              <select
                                value={selectedKey}
                                onChange={(e) => setSelectedKey(Number(e.target.value))}
                                style={{ width: '100%', padding: '0.5rem', borderRadius: '4px', background: 'var(--bg-color)', color: 'var(--text-primary)', border: '1px solid var(--border-color)' }}
                              >
                                <option value={CFG_KEY_TEST}>TEST</option>
                                <option value={CFG_KEY_HELLO}>HELLO</option>
                                <option value={CFG_KEY_PHYSICAL_LAYOUT}>Physical Layout</option>
                                <option value={CFG_KEY_LAYER_0}>Layer 0 (Base)</option>
                                <option value={CFG_KEY_LAYER_1}>Layer 1 (FN1)</option>
                                <option value={CFG_KEY_LAYER_2}>Layer 2 (FN2)</option>
                                <option value={CFG_KEY_LAYER_3}>Layer 3 (FN3)</option>
                              </select>
                            </div>
                          </div>

                          {/* Render the dynamic configuration form */}
                          {renderConfigForm()}
                        </>
                      )}

                      <button className="btn" onClick={handleSendCustomPayload} disabled={isFetching || !configData} style={{ width: '100%', marginTop: '0.5rem' }}>
                        <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                          <line x1="22" y1="2" x2="11" y2="13"></line>
                          <polygon points="22 2 15 22 11 13 2 9 22 2"></polygon>
                        </svg>
                        Save Payload
                      </button>
                    </div>

                    <div style={{ marginTop: '2rem', borderTop: '1px solid var(--border-color)', paddingTop: '1.5rem' }}>
                      <button
                        className="btn btn-danger"
                        onClick={() => setLogs([])}
                        style={{ width: '100%', padding: '0.5rem' }}
                      >
                        Clear Logs
                      </button>
                    </div>
                  </>
                ) : (
                  <div style={{ padding: '2rem 1rem', textAlign: 'center', background: 'rgba(0,0,0,0.2)', borderRadius: '8px', border: '1px dashed var(--border-color)', marginTop: '1rem' }}>
                    <p style={{ color: 'var(--text-secondary)', fontSize: '0.9rem', lineHeight: '1.5' }}>
                      Manual controls are locked.<br />Enable to send configuration or system commands.
                    </p>
                  </div>
                )}
              </div>

              <div>
                <h3>Device Logs</h3>
                <div className="log-container">
                  {logs.length === 0 ? (
                    <div style={{ color: 'var(--text-secondary)', textAlign: 'center', marginTop: '2rem' }}>
                      No logs received yet. Click the trigger button to request data.
                    </div>
                  ) : (
                    logs.map((log) => (
                      <div key={log.id} className="log-entry">
                        <span className="timestamp">{log.timestamp.toLocaleTimeString()}</span>
                        <span style={{ color: log.text.includes('Sent [') ? 'var(--accent-color)' : 'inherit' }}>
                          {log.text}
                        </span>
                      </div>
                    ))
                  )}
                  <div ref={logsEndRef} />
                </div>
              </div>
            </div>
          )}
        </>
      )}
    </div>
  );
}

export default App;
