import React, { useRef, useState } from 'react';
import type { SidebarTab } from '../Sidebar';
import '../assets/css/mobile-sidebar.css';

interface MobileSidebarProps {
  activeTab: SidebarTab;
  onTabChange: (tab: SidebarTab) => void;
  onSettingsClick: () => void;
  onConsoleClick?: () => void;
  isDeveloperMode?: boolean;
  children?: React.ReactNode;
}

const MobileSidebar: React.FC<MobileSidebarProps> = ({
  activeTab,
  onTabChange,
  onSettingsClick,
  children
}) => {
  const sheetRef = useRef<HTMLDivElement>(null);
  const [touchStartY, setTouchStartY] = useState<number | null>(null);

  const handleTouchStart = (e: React.TouchEvent) => {
    setTouchStartY(e.touches[0].clientY);
  };

  const handleTouchMove = (e: React.TouchEvent) => {
    if (touchStartY === null) return;
    const currentY = e.touches[0].clientY;
    const diff = currentY - touchStartY;
    
    // If pulling down significantly, dismiss
    if (diff > 100) {
      onTabChange(null);
      setTouchStartY(null);
    }
  };

  const handleTouchEnd = () => {
    setTouchStartY(null);
  };

  return (
    <>
      <div 
        className={`mobile-sheet-overlay ${activeTab ? 'open' : ''}`}
        onClick={() => onTabChange(null)}
      />
      <div 
        ref={sheetRef}
        className={`mobile-sheet ${activeTab ? 'open' : ''}`}
      >
        <div 
          className="mobile-sheet-handle-area"
          onTouchStart={handleTouchStart}
          onTouchMove={handleTouchMove}
          onTouchEnd={handleTouchEnd}
          style={{ padding: '4px 0', cursor: 'grab' }}
        >
          <div className="mobile-sheet-handle" />
        </div>
        <div className="mobile-sheet-content">
          {children}
        </div>
      </div>

      <div className="mobile-tab-bar">
        <button 
          className={`mobile-tab-btn ${activeTab === 'macros' ? 'active' : ''}`}
          onClick={() => onTabChange(activeTab === 'macros' ? null : 'macros')}
        >
          <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.2" strokeLinecap="round" strokeLinejoin="round">
            <polyline points="16 18 22 12 16 6"></polyline>
            <polyline points="8 6 2 12 8 18"></polyline>
          </svg>
          MACROS
        </button>
        <button 
          className={`mobile-tab-btn ${activeTab === 'ckeys' ? 'active' : ''}`}
          onClick={() => onTabChange(activeTab === 'ckeys' ? null : 'ckeys')}
        >
          <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.2" strokeLinecap="round" strokeLinejoin="round">
            <path d="M12 20h9"></path>
            <path d="M16.5 3.5a2.12 2.12 0 0 1 3 3L7 19l-4 1 1-4Z"></path>
          </svg>
          CKEYS
        </button>
        <button 
          className={`mobile-tab-btn ${activeTab === 'combos' ? 'active' : ''}`}
          onClick={() => onTabChange(activeTab === 'combos' ? null : 'combos')}
        >
          <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.2" strokeLinecap="round" strokeLinejoin="round">
            <circle cx="12" cy="12" r="10"></circle>
            <circle cx="12" cy="12" r="3"></circle>
          </svg>
          COMBOS
        </button>
        <button 
          className="mobile-tab-btn"
          onClick={onSettingsClick}
        >
          <svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.2" strokeLinecap="round" strokeLinejoin="round">
            <circle cx="12" cy="12" r="3"></circle>
            <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"></path>
          </svg>
          SETTINGS
        </button>
      </div>
    </>
  );
};

export default MobileSidebar;
