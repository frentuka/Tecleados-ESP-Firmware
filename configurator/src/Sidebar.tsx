import React, { useRef, useEffect } from 'react';

// ── Sidebar Tab Type ──
export type SidebarTab = 'macros' | 'ckeys' | 'combos' | null;



// ── Icon Components ──
const MacrosIcon = () => (
    <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <polyline points="4 17 10 11 4 5" />
        <line x1="12" y1="19" x2="20" y2="19" />
    </svg>
);

const CustomKeysIcon = () => (
    <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <rect x="2" y="4" width="20" height="16" rx="2" ry="2" />
        <line x1="6" y1="8" x2="6.01" y2="8" strokeWidth="2.5" />
        <line x1="10" y1="8" x2="10.01" y2="8" strokeWidth="2.5" />
        <line x1="14" y1="8" x2="14.01" y2="8" strokeWidth="2.5" />
        <line x1="18" y1="8" x2="18.01" y2="8" strokeWidth="2.5" />
        <line x1="6" y1="12" x2="6.01" y2="12" strokeWidth="2.5" />
        <line x1="10" y1="12" x2="10.01" y2="12" strokeWidth="2.5" />
        <line x1="14" y1="12" x2="14.01" y2="12" strokeWidth="2.5" />
        <line x1="18" y1="12" x2="18.01" y2="12" strokeWidth="2.5" />
        <line x1="8" y1="16" x2="16" y2="16" strokeWidth="2" />
    </svg>
);

const CombosIcon = () => (
    <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <circle cx="8" cy="8" r="4" />
        <circle cx="16" cy="16" r="4" />
        <line x1="11" y1="11" x2="13" y2="13" />
    </svg>
);

const SettingsIcon = () => (
    <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <circle cx="12" cy="12" r="3" />
        <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z" />
    </svg>
);

const ConsoleIcon = () => (
    <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <rect x="2" y="3" width="20" height="14" rx="2" />
        <line x1="8" y1="21" x2="16" y2="21" />
        <line x1="12" y1="17" x2="12" y2="21" />
        <polyline points="6 8 9 11 6 14" />
        <line x1="12" y1="14" x2="17" y2="14" />
    </svg>
);

// ── Props ──
interface SidebarProps {
    activeTab: SidebarTab;
    onTabChange: (tab: SidebarTab) => void;
    onSettingsClick: () => void;
    onConsoleClick: () => void;
    isDeveloperMode: boolean;
    children: React.ReactNode;
}

// ── Sidebar Icon Button ──
function SidebarIconButton({ 
    icon, 
    label, 
    isActive, 
    onClick,
    className = '',
}: { 
    icon: React.ReactNode; 
    label: string; 
    isActive: boolean; 
    onClick: () => void;
    className?: string;
}) {
    return (
        <button
            className={`sidebar-icon-btn ${isActive ? 'active' : ''} ${className}`}
            onClick={onClick}
            title={label}
            aria-label={label}
        >
            <span className="sidebar-icon-svg">{icon}</span>
            <span className="sidebar-icon-label">{label}</span>
        </button>
    );
}

// ── Main Sidebar Component ──
export default function Sidebar({
    activeTab,
    onTabChange,
    onSettingsClick,
    onConsoleClick,
    isDeveloperMode,
    children,
}: SidebarProps) {
    const isOpen = activeTab !== null;
    const sidebarRef = useRef<HTMLDivElement>(null);

    useEffect(() => {
        const handleClickOutside = (event: MouseEvent) => {
            if (!isOpen || !sidebarRef.current) return;

            // If the user clicks exactly on the container itself, it means they clicked 
            // the ::before overlay (since the container otherwise has pointer-events: none).
            if (event.target === sidebarRef.current) {
                onTabChange(null);
                return;
            }

            if (!sidebarRef.current.contains(event.target as Node)) {
                // Check if the click was inside a modal or notification.
                // We don't want to close the sidebar if they are interacting with an overlay.
                const target = event.target as Element;
                if (target.closest('.modal-overlay') || target.closest('.notification-toast') || target.closest('.dropdown-menu')) {
                    return;
                }
                onTabChange(null);
            }
        };

        document.addEventListener('mousedown', handleClickOutside);
        return () => {
            document.removeEventListener('mousedown', handleClickOutside);
        };
    }, [isOpen, onTabChange]);

    const handleTabClick = (tab: SidebarTab) => {
        if (activeTab === tab) {
            onTabChange(null);
        } else {
            onTabChange(tab);
        }
    };

    return (
        <div className={`sidebar-container ${isOpen ? 'open' : ''}`} ref={sidebarRef}>
            {/* ── Expandable Panel ── */}
            <div className="sidebar-panel">
                <div className="sidebar-panel-content">
                    {children}
                </div>
            </div>

            {/* ── Icon Rail ── */}
            <div className="sidebar-rail">
                <div className="sidebar-rail-top">
                    <SidebarIconButton
                        icon={<MacrosIcon />}
                        label="Macros"
                        isActive={activeTab === 'macros'}
                        onClick={() => handleTabClick('macros')}
                    />
                    <SidebarIconButton
                        icon={<CustomKeysIcon />}
                        label="CKEYS"
                        isActive={activeTab === 'ckeys'}
                        onClick={() => handleTabClick('ckeys')}
                    />
                    <SidebarIconButton
                        icon={<CombosIcon />}
                        label="Combos"
                        isActive={activeTab === 'combos'}
                        onClick={() => handleTabClick('combos')}
                    />
                </div>

                <div className="sidebar-rail-bottom">
                    {isDeveloperMode && (
                        <SidebarIconButton
                            icon={<ConsoleIcon />}
                            label="Console"
                            isActive={false}
                            onClick={onConsoleClick}
                            className="sidebar-icon-dev"
                        />
                    )}
                    <SidebarIconButton
                        icon={<SettingsIcon />}
                        label="Settings"
                        isActive={false}
                        onClick={onSettingsClick}
                        className="sidebar-icon-settings"
                    />
                </div>
            </div>
        </div>
    );
}
