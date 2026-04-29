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

    // Auto-scroll logs to bottom
    useEffect(() => {
        if (logsEndRef.current) {
            logsEndRef.current.scrollIntoView({ behavior: 'smooth' });
        }
    }, [logs]);

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
            return (
                <div className="devctrl-status-text">
                    <div className="macro-card-spinner" style={{ width: 16, height: 16, borderWidth: 2 }} />
                    Fetching configuration…
                </div>
            );
        }

        if (!configData || Object.keys(configData).length === 0) {
            return <div className="devctrl-status-text">No configuration data retrieved.</div>;
        }

        return (
            <div className="devctrl-config-form">
                <div className="devctrl-config-form-title">Configuration Form</div>
                {Object.entries(configData).map(([key, value]) => {
                    const isBoolean = typeof value === 'boolean';
                    const isNumber = typeof value === 'number';

                    return (
                        <div key={key} className="devctrl-config-row">
                            <label className="devctrl-config-key">{key}</label>
                            {isBoolean ? (
                                <input
                                    type="checkbox"
                                    checked={value as boolean}
                                    onChange={(e) => setConfigData({ ...configData, [key]: e.target.checked })}
                                    className="devctrl-checkbox"
                                />
                            ) : (
                                <input
                                    type={isNumber ? 'number' : 'text'}
                                    value={value as string | number}
                                    onChange={(e) => {
                                        const newValue = isNumber ? Number(e.target.value) : e.target.value;
                                        setConfigData({ ...configData, [key]: newValue });
                                    }}
                                    className="devctrl-text-input"
                                />
                            )}
                        </div>
                    );
                })}
            </div>
        );
    };

    return (
        <div className="devctrl-page">
          <div className="devctrl-inner">

            {/* ── Controls Panel ── */}
            <div className="devctrl-panel-section">
                <div className="devctrl-panel-header">
                    <div className="devctrl-panel-title-row">
                        <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ opacity: 0.6 }}>
                            <rect x="2" y="3" width="20" height="14" rx="2" ry="2"/>
                            <line x1="8" y1="21" x2="16" y2="21"/>
                            <line x1="12" y1="17" x2="12" y2="21"/>
                        </svg>
                        <span className="devctrl-panel-title">Controls</span>
                    </div>
                    <label className="devctrl-toggle-label">
                        <span className={`devctrl-toggle-text ${controlsEnabled ? 'enabled' : ''}`}>
                            {controlsEnabled ? 'ENABLED' : 'DISABLED'}
                        </span>
                        <div
                            className={`devctrl-toggle ${controlsEnabled ? 'active' : ''}`}
                            onClick={() => setControlsEnabled(!controlsEnabled)}
                        >
                            <div className="devctrl-toggle-thumb" />
                        </div>
                    </label>
                </div>

                {controlsEnabled ? (
                    <div className="devctrl-controls-body">
                        <p className="devctrl-hint-text">
                            Send a COMM HID report dynamically utilizing CRC-8 packet structure.
                        </p>

                        <div className="devctrl-form-fields">
                            <div className="devctrl-field">
                                <label className="devctrl-field-label">Target Module</label>
                                <select
                                    value={selectedModule}
                                    onChange={(e) => setSelectedModule(Number(e.target.value))}
                                    className="devctrl-select"
                                >
                                    <option value={MODULE_CONFIG}>Config Module</option>
                                    <option value={MODULE_SYSTEM}>System Module</option>
                                </select>
                            </div>

                            {selectedModule === MODULE_CONFIG && (
                                <>
                                    <div className="devctrl-field">
                                        <label className="devctrl-field-label">Key ID</label>
                                        <select
                                            value={selectedKey}
                                            onChange={(e) => setSelectedKey(Number(e.target.value))}
                                            className="devctrl-select"
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

                                    {renderConfigForm()}
                                </>
                            )}

                            <button
                                className="btn btn-sm"
                                onClick={handleSendCustomPayload}
                                disabled={isFetchingConfig || !configData}
                                style={{ marginTop: '0.25rem' }}
                            >
                                <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                                    <line x1="22" y1="2" x2="11" y2="13"/>
                                    <polygon points="22 2 15 22 11 13 2 9 22 2"/>
                                </svg>
                                Save Payload
                            </button>
                        </div>

                        <div className="devctrl-danger-row">
                            <button className="btn btn-danger btn-sm" onClick={onClearLogs}>
                                Clear Logs
                            </button>
                        </div>
                    </div>
                ) : (
                    <div className="devctrl-locked-state">
                        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" style={{ opacity: 0.3 }}>
                            <rect x="3" y="11" width="18" height="11" rx="2" ry="2"/>
                            <path d="M7 11V7a5 5 0 0 1 10 0v4"/>
                        </svg>
                        <p className="devctrl-locked-text">
                            Manual controls are locked.<br/>Enable to send configuration or system commands.
                        </p>
                    </div>
                )}
            </div>

            {/* ── Device Logs ── */}
            <div className="devctrl-logs-section">

                <div className="devctrl-logs-header">
                    <div className="devctrl-panel-title-row">
                        <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ opacity: 0.6 }}>
                            <line x1="8" y1="6" x2="21" y2="6"/>
                            <line x1="8" y1="12" x2="21" y2="12"/>
                            <line x1="8" y1="18" x2="21" y2="18"/>
                            <line x1="3" y1="6" x2="3.01" y2="6"/>
                            <line x1="3" y1="12" x2="3.01" y2="12"/>
                            <line x1="3" y1="18" x2="3.01" y2="18"/>
                        </svg>
                        <span className="devctrl-panel-title">Device Logs</span>
                    </div>
                    <span className="devctrl-log-count">{logs.length} entries</span>
                </div>

                <div className="devctrl-log-container">
                    {logs.length === 0 ? (
                        <div className="devctrl-log-empty">
                            No logs received yet.
                        </div>
                    ) : (
                        logs.map((log) => (
                            <div key={log.id} className="devctrl-log-entry">
                                <span className="devctrl-log-timestamp">{log.timestamp.toLocaleTimeString()}</span>
                                <span className={log.text.includes('Sent [') ? 'devctrl-log-sent' : ''}>
                                    {log.text}
                                </span>
                            </div>
                        ))
                    )}
                    <div ref={logsEndRef} />
                </div>
            </div>

          </div>
        </div>
    );
}
