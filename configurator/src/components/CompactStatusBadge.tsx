import React from 'react';

interface CompactStatusBadgeProps {
  isConnected: boolean;
  onClick?: () => void;
}

const CompactStatusBadge: React.FC<CompactStatusBadgeProps> = ({
  isConnected,
  onClick
}) => {
  return (
    <div 
      className={`status-widget compact ${isConnected ? 'connected' : 'disconnected'}`}
      onClick={onClick}
      style={{ cursor: 'pointer', padding: '0 10px' }}
    >
      <div className="status-badge" title={isConnected ? "Connected" : "Disconnected"}>
        <span className="status-dot"></span>
        <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round" style={{ opacity: 0.8 }}>
          {isConnected ? (
            <polyline points="16 18 22 12 16 6"></polyline>
          ) : (
            <line x1="18" y1="6" x2="6" y2="18"></line>
          )}
          {isConnected ? (
            <polyline points="8 6 2 12 8 18"></polyline>
          ) : (
            <line x1="6" y1="6" x2="18" y2="18"></line>
          )}
        </svg>
      </div>
    </div>
  );
};

export default CompactStatusBadge;
