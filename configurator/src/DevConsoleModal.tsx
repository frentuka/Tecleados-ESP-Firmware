import { createPortal } from 'react-dom';

interface DevConsoleModalProps {
    isOpen: boolean;
    onClose: () => void;
    logEntries: string[];
    onClearLog: () => void;
}

export default function DevConsoleModal({
    isOpen,
    onClose,
    logEntries,
    onClearLog,
}: DevConsoleModalProps) {
    if (!isOpen) return null;

    return createPortal(
        <div className="devconsole-modal-overlay" onClick={onClose}>
            <div className="devconsole-modal-content" onClick={e => e.stopPropagation()}>
                <div className="devconsole-modal-header">
                    <div className="devconsole-modal-title">
                        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" style={{ opacity: 0.6 }}>
                            <polyline points="4 17 10 11 4 5" />
                            <line x1="12" y1="19" x2="20" y2="19" />
                        </svg>
                        <span>Developer Console</span>
                        <span className="devconsole-entry-count">{logEntries.length} entries</span>
                    </div>
                    <div className="devconsole-modal-actions">
                        <button
                            className="btn btn-sm btn-secondary"
                            onClick={onClearLog}
                            title="Clear log"
                        >
                            Clear
                        </button>
                        <button className="btn-close" onClick={onClose}>×</button>
                    </div>
                </div>
                <div className="devconsole-modal-body">
                    {logEntries.length === 0 ? (
                        <div className="devconsole-empty">No log entries yet.</div>
                    ) : (
                        <div className="devconsole-log-list">
                            {logEntries.map((entry, i) => (
                                <div key={i} className="devconsole-log-entry">
                                    <span className="devconsole-log-index">{i + 1}</span>
                                    <span className="devconsole-log-text">{entry}</span>
                                </div>
                            ))}
                        </div>
                    )}
                </div>
            </div>
        </div>,
        document.body
    );
}
