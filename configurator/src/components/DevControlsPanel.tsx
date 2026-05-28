import { useCallback, useEffect, useRef, useState } from 'react';
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
    const [isExpanded, setIsExpanded] = useState(false);

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
                <div className={`devctrl-logs-section ${isExpanded ? 'is-expanded' : 'is-contracted'}`}>

                    <div className="devctrl-logs-header" onClick={() => setIsExpanded(!isExpanded)} style={{ cursor: 'pointer' }}>
                        <div className="devctrl-panel-title-row">
                            <svg 
                                width="12" 
                                height="12" 
                                viewBox="0 0 24 24" 
                                fill="none" 
                                stroke="currentColor" 
                                strokeWidth="3" 
                                strokeLinecap="round" 
                                strokeLinejoin="round" 
                                style={{ 
                                    transition: 'transform 0.3s cubic-bezier(0.16, 1, 0.3, 1)',
                                    transform: isExpanded ? 'rotate(90deg)' : 'rotate(0deg)',
                                    opacity: 0.8
                                }}
                            >
                                <polyline points="9 18 15 12 9 6" />
                            </svg>
                            <span className="devctrl-panel-title">Device Logs</span>
                        </div>
                        <div className="devctrl-logs-header-actions" onClick={(e) => e.stopPropagation()}>
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
