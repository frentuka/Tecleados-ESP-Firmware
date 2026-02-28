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
  const [selectedCmd, setSelectedCmd] = useState<number>(CFG_CMD_GET);
  const [selectedKey, setSelectedKey] = useState<number>(CFG_KEY_TEST);
  const [customPayload, setCustomPayload] = useState('{"val": 123}');

  const logsEndRef = useRef<HTMLDivElement>(null);

  // Auto-scroll to bottom of logs
  useEffect(() => {
    logsEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [logs]);

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

    // Decode payload text based on new logic format
    // For incoming responses, if module is CONFIG, [Mod][Cmd][KeyId][Len]
    // The ESP actually replies with [Mod][Cmd][Key][Len][Status...][Json...] -> oh wait, ESP replies with [Mod][Cmd][KeyId][Payload_len][Status (4bytes)][...data] 
    // We can just dump everything after byte 4.
    let payloadText = new TextDecoder().decode(payloadBytes);
    payloadText = payloadText.replace(/\0/g, ''); // Remove null terminators

    let text = `${getFlagsString(flags)} Len: ${safeLen}, Rem: ${remaining}`;
    if (safeLen > 0) {
      if (payloadText.trim() === '' || Array.from(payloadBytes).some(b => b < 32 && b !== 9 && b !== 10 && b !== 13)) {
        text += ` => Hex: ${Array.from(payloadBytes).map(b => b.toString(16).padStart(2, '0')).join(' ')}`;
      } else {
        text += ` => Payload: "${payloadText}"`;
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

    // Only encode payload data if command is SET
    if (selectedModule === MODULE_CONFIG && selectedCmd === CFG_CMD_SET) {
      const encoder = new TextEncoder();
      payloadBytes = encoder.encode(customPayload);
    }

    // Format: [Module][Command][KeyId][PayloadLen][...Data]
    const packetLen = 4 + payloadBytes.length;
    const buf = new Uint8Array(packetLen - 1); // Exclude Module ID from the initial buffer

    if (selectedModule === MODULE_CONFIG) {
      buf[0] = selectedCmd;
      buf[1] = selectedKey;
      buf[2] = payloadBytes.length;
      if (payloadBytes.length > 0) {
        buf.set(payloadBytes, 3);
      }
    } else {
      // System module generic format
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
                      <label style={{ display: 'block', marginBottom: '0.25rem', fontSize: '0.9rem' }}>Command:</label>
                      <select
                        value={selectedCmd}
                        onChange={(e) => setSelectedCmd(Number(e.target.value))}
                        style={{ width: '100%', padding: '0.5rem', borderRadius: '4px', background: 'var(--bg-color)', color: 'var(--text-primary)', border: '1px solid var(--border-color)' }}
                      >
                        <option value={CFG_CMD_GET}>GET</option>
                        <option value={CFG_CMD_SET}>SET</option>
                      </select>
                    </div>
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

                  {selectedCmd === CFG_CMD_SET && (
                    <div>
                      <label style={{ display: 'block', marginBottom: '0.25rem', fontSize: '0.9rem' }}>Payload Data (JSON):</label>
                      <input
                        type="text"
                        value={customPayload}
                        onChange={(e) => setCustomPayload(e.target.value)}
                        style={{
                          width: '100%',
                          padding: '0.75rem',
                          borderRadius: '6px',
                          border: '1px solid var(--border-color)',
                          background: 'var(--bg-color)',
                          color: 'var(--text-primary)',
                        }}
                      />
                    </div>
                  )}
                </>
              )}

              <button className="btn" onClick={handleSendCustomPayload} style={{ width: '100%', marginTop: '0.5rem' }}>
                <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                  <line x1="22" y1="2" x2="11" y2="13"></line>
                  <polygon points="22 2 15 22 11 13 2 9 22 2"></polygon>
                </svg>
                Send Payload
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
