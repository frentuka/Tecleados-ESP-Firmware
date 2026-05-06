import { useCallback, useEffect, useRef } from 'react';
import type { LogMessage } from '../types/device';
import '../assets/css/dev-controls.css';

interface DevControlsPanelProps {
    isConnected: boolean;
    logs: LogMessage[];
    onClearLogs: () => void;
}

export default function DevControlsPanel({ logs, onClearLogs }: DevControlsPanelProps) {
    const logContainerRef = useRef<HTMLDivElement>(null);
    const logContentRef = useRef<HTMLDivElement>(null);
    const isAtBottomRef = useRef(true);

    // Track scroll position to know if we should auto-scroll
    const handleScroll = useCallback(() => {
        if (logContainerRef.current) {
            const container = logContainerRef.current;
            // High threshold (400px) to stay "locked" even during massive spam
            const threshold = 400;
            const distanceToBottom = container.scrollHeight - container.scrollTop - container.clientHeight;
            isAtBottomRef.current = distanceToBottom < threshold;
        }
    }, []);

    // ResizeObserver to handle auto-scroll whenever the content height changes.
    useEffect(() => {
        const container = logContainerRef.current;
        const content = logContentRef.current;
        if (!container || !content) return;

        const observer = new ResizeObserver(() => {
            if (isAtBottomRef.current) {
                // Immediate scroll
                container.scrollTop = container.scrollHeight;

                // Deferred scroll to catch any late-arriving layout shifts from the browser
                requestAnimationFrame(() => {
                    if (container) container.scrollTop = container.scrollHeight;
                });
            }
        });

        observer.observe(content);
        return () => observer.disconnect();
    }, []);

    return (
        <div className="devctrl-page">
            <div className="devctrl-inner">

                {/* ── Device Logs ── */}
                <div className="devctrl-logs-section">

                    <div className="devctrl-logs-header">
                        <div className="devctrl-panel-title-row">
                            <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ opacity: 0.6 }}>
                                <line x1="8" y1="6" x2="21" y2="6" />
                                <line x1="8" y1="12" x2="21" y2="12" />
                                <line x1="8" y1="18" x2="21" y2="18" />
                                <line x1="3" y1="6" x2="3.01" y2="6" />
                                <line x1="3" y1="12" x2="3.01" y2="12" />
                                <line x1="3" y1="18" x2="3.01" y2="18" />
                            </svg>
                            <span className="devctrl-panel-title">Device Logs</span>
                        </div>
                        <div className="devctrl-logs-header-actions">
                            <span className="devctrl-log-count">{logs.length} entries</span>
                            <button className="btn btn-danger btn-sm" onClick={onClearLogs} style={{ marginLeft: '1rem' }}>
                                Clear Logs
                            </button>
                        </div>
                    </div>

                    <div className="devctrl-log-container" ref={logContainerRef} onScroll={handleScroll}>
                        <div ref={logContentRef}>
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
                        </div>
                    </div>
                </div>

            </div>
        </div>
    );
}
