import { useState, useCallback, useEffect, useRef } from 'react';
import {
    hidService,
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
} from '../HIDService';
import type { LogMessage } from '../types/device';

interface DevControlsPanelProps {
    isConnected: boolean;
    logs: LogMessage[];
    onClearLogs: () => void;
    onAddLog: (text: string) => void;
}

export default function DevControlsPanel({ isConnected, logs, onClearLogs, onAddLog }: DevControlsPanelProps) {
    const [controlsEnabled, setControlsEnabled] = useState(false);
    const [selectedModule, setSelectedModule] = useState<number>(MODULE_CONFIG);
    // Always SET — the dynamic UI reads current config via GET then lets the user edit and send SET.
    const selectedCmd: number = CFG_CMD_SET;
    const [selectedKey, setSelectedKey] = useState<number>(CFG_KEY_TEST);
    const [configData, setConfigData] = useState<Record<string, any> | null>(null);
    const [isFetchingConfig, setIsFetchingConfig] = useState(false);

    const logsEndRef = useRef<HTMLDivElement>(null);

    const fetchConfigData = useCallback(async (module: number, key: number) => {
        if (!isConnected || !controlsEnabled || module !== MODULE_CONFIG) {
            setConfigData(null);
            return;
        }

        setIsFetchingConfig(true);
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
        setIsFetchingConfig(false);
    }, [isConnected, controlsEnabled]);

    useEffect(() => {
        if (isConnected && controlsEnabled) {
            fetchConfigData(selectedModule, selectedKey);
        }
    }, [isConnected, controlsEnabled, selectedModule, selectedKey, fetchConfigData]);

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
            setIsFetchingConfig(false);
        }

        onAddLog(`Sent [${modStr}] Cmd: ${cmdStr}, Key: ${keyStr}, Len: ${payloadBytes.length}`);
    };

    const renderConfigForm = () => {
        if (isFetchingConfig) {
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

                            <button className="btn" onClick={handleSendCustomPayload} disabled={isFetchingConfig || !configData} style={{ width: '100%', marginTop: '0.5rem' }}>
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
                                onClick={onClearLogs}
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
    );
}
