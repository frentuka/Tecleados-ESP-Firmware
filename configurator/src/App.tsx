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
  CFG_KEY_HELLO
} from './HIDService';
import './index.css';

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
  const [logs, setLogs] = useState<LogMessage[]>([]);

  const [selectedModule, setSelectedModule] = useState<number>(MODULE_CONFIG);
  // Default to SET because the dynamic UI implicitly wants to SET what the GET retrieved.
  // We'll auto request a GET under the hood when selectedModule/selectedKey changes.
  const selectedCmd: number = CFG_CMD_SET;
  const [selectedKey, setSelectedKey] = useState<number>(CFG_KEY_TEST);

  // Dynamic configuration data state
  const [configData, setConfigData] = useState<Record<string, any> | null>(null);
  const [isFetching, setIsFetching] = useState<boolean>(false);

  const logsEndRef = useRef<HTMLDivElement>(null);
  const incomingPayloadRef = useRef<{ cmd: number, keyId: number, status: number, jsonText: string } | null>(null);

  // Auto-scroll to bottom of logs
  useEffect(() => {
    logsEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [logs]);

  // Handle requesting config data when module, key, or connection status changes
  const fetchConfigData = useCallback(async (module: number, key: number) => {
    if (!isConnected || module !== MODULE_CONFIG) {
      setConfigData(null);
      return;
    }

    setIsFetching(true);
    setConfigData(null); // Clear current form while loading

    // Construct a GET packet for the selected module and key
    const buf = new Uint8Array(3);
    buf[0] = CFG_CMD_GET;
    buf[1] = key;
    buf[2] = 0; // Length 0

    const finalPayload = new Uint8Array(1 + buf.length);
    finalPayload[0] = module;
    finalPayload.set(buf, 1);

    await hidService.sendCustomCommReport(finalPayload);
    // The WebHID callback will parse the jsonText and update configData.
  }, [isConnected]);

  useEffect(() => {
    if (isConnected) {
      fetchConfigData(selectedModule, selectedKey);
    }
  }, [isConnected, selectedModule, selectedKey, fetchConfigData]);

  const handleLogReceived = useCallback((data: Uint8Array) => {
    // Parse the 48-byte usb_packet_msg_t
    if (data.length < 48) return;

    const flags = data[0];
    const remaining = data[1] | (data[2] << 8);
    const payloadLen = data[3];

    // Only send ACK/OK if the packet contains actual payload data (not an ACK/OK itself)
    const isResponsePacket = (flags & PAYLOAD_FLAG_ACK) || (flags & PAYLOAD_FLAG_NAK) || (flags & PAYLOAD_FLAG_OK) || (flags & PAYLOAD_FLAG_ERR) || (flags & PAYLOAD_FLAG_ABORT);

    if (!isResponsePacket) {
      // Automatically send ACK for incoming packets
      if (remaining > 0) {
        hidService.sendResponse(PAYLOAD_FLAG_ACK);
      } else {
        hidService.sendResponse(PAYLOAD_FLAG_ACK | PAYLOAD_FLAG_OK);
      }
    }

    // Bounds check to avoid invalid lengths
    const safeLen = Math.min(payloadLen, 43);
    const payloadBytes = data.slice(4, 4 + safeLen);

    let text = `${getFlagsString(flags)} Len: ${safeLen}, Rem: ${remaining}`;
    let isParsed = false;

    if (safeLen > 0) {
      // Configuration response: Module 1, length is at least 7 bytes (cmd(1) + key(1) + payload_len(1) + status(4) + potential json)
      if ((flags & PAYLOAD_FLAG_FIRST) && payloadBytes[0] === MODULE_CONFIG && safeLen >= 7) {
        // First packet of a config response
        const cmd = payloadBytes[1];
        const keyId = payloadBytes[2];
        // ESP32 esp_err_t is 32-bit signed integer (little endian)
        const status = (payloadBytes[4] | (payloadBytes[5] << 8) | (payloadBytes[6] << 16) | (payloadBytes[7] << 24));

        const jsonBytes = payloadBytes.slice(8);
        const jsonText = new TextDecoder().decode(jsonBytes).replace(/\0/g, '');

        incomingPayloadRef.current = { cmd, keyId, status, jsonText };

        if (flags & PAYLOAD_FLAG_LAST) {
          const cmdStr = cmd === CFG_CMD_GET ? "GET" : (cmd === CFG_CMD_SET ? "SET" : `CMD(${cmd})`);
          const completeJsonTxt = incomingPayloadRef.current.jsonText;
          text += ` => [CONFIG ${cmdStr} Key:${keyId} Status:${status}] JSON: ${completeJsonTxt}`;

          if (cmd === CFG_CMD_GET && completeJsonTxt.trim().length > 0) {
            try {
              const parsedObj = JSON.parse(completeJsonTxt);
              setConfigData(parsedObj);
            } catch (e) {
              console.error("Config JSON Parse Error (First==Last):", e);
              setConfigData({});
            }
            setIsFetching(false);
          }

          incomingPayloadRef.current = null;
          isParsed = true;
        } else {
          text += ` => [CONFIG RX START] ${jsonText}`;
          isParsed = true;
        }
      } else if (incomingPayloadRef.current && !(flags & PAYLOAD_FLAG_FIRST)) {
        // Continuation packet
        const jsonText = new TextDecoder().decode(payloadBytes).replace(/\0/g, '');
        incomingPayloadRef.current.jsonText += jsonText;

        if (flags & PAYLOAD_FLAG_LAST) {
          const { cmd, keyId, status, jsonText: fullJson } = incomingPayloadRef.current;
          const cmdStr = cmd === CFG_CMD_GET ? "GET" : (cmd === CFG_CMD_SET ? "SET" : `CMD(${cmd})`);
          text += ` => [CONFIG ${cmdStr} Key:${keyId} Status:${status}] FULL JSON: ${fullJson}`;

          // Parse the fully re-assembled JSON strictly when it's a GET response
          if (cmd === CFG_CMD_GET && fullJson.trim().length > 0) {
            try {
              const parsedObj = JSON.parse(fullJson);
              setConfigData(parsedObj);
            } catch (e) {
              console.error("Config JSON Parse Error:", e);
              // fallback to empty if malformed to allow users to override
              setConfigData({});
            }
            setIsFetching(false);
          }

          incomingPayloadRef.current = null;
          isParsed = true;
        } else {
          text += ` => [CONFIG RX MID] ${jsonText}`;
          isParsed = true;
        }
      }

      if (!isParsed) {
        let payloadText = new TextDecoder().decode(payloadBytes).replace(/\0/g, '');
        if (payloadText.trim() === '' || Array.from(payloadBytes).some(b => b < 32 && b !== 9 && b !== 10 && b !== 13)) {
          text += ` => Hex: ${Array.from(payloadBytes).map(b => b.toString(16).padStart(2, '0')).join(' ')}`;
        } else {
          text += ` => Payload: "${payloadText}"`;
        }
      }
    }

    setLogs((prev) => [
      ...prev,
      {
        id: Date.now() + Math.random(),
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
    const success = await hidService.requestDevice();
    setIsConnected(success);
  };

  const handleDisconnect = async () => {
    await hidService.disconnect();
    setIsConnected(false);
  };


  const handleSendCustomPayload = async () => {
    let payloadBytes = new Uint8Array(0);

    // Only encode payload data if command is SET and we have a valid form object
    if (selectedModule === MODULE_CONFIG && selectedCmd === CFG_CMD_SET && configData) {
      const encoder = new TextEncoder();
      const stringifiedPayload = JSON.stringify(configData);
      payloadBytes = encoder.encode(stringifiedPayload);
    }

    let buf: Uint8Array;
    if (selectedModule === MODULE_CONFIG) {
      const packetLen = 4 + payloadBytes.length;
      buf = new Uint8Array(packetLen - 1); // Exclude Module ID from the initial buffer
      buf[0] = selectedCmd;
      buf[1] = selectedKey;
      buf[2] = payloadBytes.length;
      if (payloadBytes.length > 0) {
        buf.set(payloadBytes, 3);
      }
    } else {
      // System module generic format
      buf = new Uint8Array(3);
      buf[0] = 0;
      buf[1] = 0;
      buf[2] = 0;
    }

    // Combine module ID and payload so `sendCustomCommReport` can build the final packet
    const finalPayload = new Uint8Array(1 + buf.length);
    finalPayload[0] = selectedModule;
    finalPayload.set(buf, 1);

    const cmdStr = selectedCmd === CFG_CMD_GET ? 'GET' : 'SET';
    const keyStr = selectedKey === CFG_KEY_TEST ? 'TEST' : 'HELLO';
    const modStr = selectedModule === MODULE_CONFIG ? 'CONFIG' : 'SYSTEM';

    // Add debug console log to see what the website is sending
    console.log(`Sending packet to Mod ${modStr}, Cmd ${cmdStr}, Key ${keyStr}, Total Len: ${finalPayload.length}`);
    console.log("Payload bytes:", Array.from(finalPayload).map(b => b.toString(16).padStart(2, '0')).join(' '));

    await hidService.sendCustomCommReport(finalPayload);

    setLogs((prev) => [
      ...prev,
      {
        id: Date.now() + Math.random(),
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
      <h1 className="title">DF-ONE Configurator</h1>

      <div className="glass-panel">
        <h2>Connection Status</h2>
        <div style={{ display: 'flex', alignItems: 'center', marginBottom: '1rem' }}>
          <span className={`status - indicator ${isConnected ? 'status-connected' : 'status-disconnected'} `}></span>
          <span>{isConnected ? 'Device Connected (Tecleados DF-ONE)' : 'No Device Connected'}</span>
        </div>

        {!isConnected ? (
          <button className="btn btn-success" onClick={handleConnect}>
            <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <rect x="3" y="11" width="18" height="11" rx="2" ry="2"></rect>
              <path d="M7 11V7a5 5 0 0 1 10 0v4"></path>
            </svg>
            Connect Device
          </button>
        ) : (
          <button className="btn btn-danger" onClick={handleDisconnect}>
            Disconnect
          </button>
        )}
      </div>

      {isConnected && (
        <div className="dashboard glass-panel">
          <div className="controls-panel">
            <h3>Controls</h3>
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
    </div>
  );
}

export default App;
