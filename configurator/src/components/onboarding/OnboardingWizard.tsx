import { useState, useEffect, useRef, useCallback } from 'react';
import { createPortal } from 'react-dom';
import { useOnboardingStore } from '../../stores/onboardingStore';
import SpotlightOverlay from './SpotlightOverlay';
import '../../assets/css/onboarding.css';

interface OnboardingWizardProps {
    isConnected: boolean;
    onConnect: () => Promise<void>;
}

// ── Step indicator dots ──
function StepDots({ current, total }: { current: number; total: number }) {
    return (
        <div className="onboarding-step-dots">
            {Array.from({ length: total }, (_, i) => (
                <div
                    key={i}
                    className={`onboarding-dot ${i === current ? 'active' : i < current ? 'completed' : ''}`}
                />
            ))}
        </div>
    );
}

// ── Icons ──
const KeyboardIcon = () => (
    <svg width="90" height="90" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1" strokeLinecap="round" strokeLinejoin="round">
        <rect x="2" y="4" width="20" height="16" rx="2" ry="2" />
        <line x1="6" y1="8" x2="6.01" y2="8" strokeWidth="2" />
        <line x1="10" y1="8" x2="10.01" y2="8" strokeWidth="2" />
        <line x1="14" y1="8" x2="14.01" y2="8" strokeWidth="2" />
        <line x1="18" y1="8" x2="18.01" y2="8" strokeWidth="2" />
        <line x1="6" y1="12" x2="6.01" y2="12" strokeWidth="2" />
        <line x1="10" y1="12" x2="10.01" y2="12" strokeWidth="2" />
        <line x1="14" y1="12" x2="14.01" y2="12" strokeWidth="2" />
        <line x1="18" y1="12" x2="18.01" y2="12" strokeWidth="2" />
        <line x1="7" y1="16" x2="17" y2="16" strokeWidth="2" />
    </svg>
);

const LayersIcon = () => (
    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <polygon points="12 2 2 7 12 12 22 7 12 2" />
        <polyline points="2 17 12 22 22 17" />
        <polyline points="2 12 12 17 22 12" />
    </svg>
);

const ZapIcon = () => (
    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2" />
    </svg>
);

const TerminalIcon = () => (
    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <polyline points="4 17 10 11 4 5" />
        <line x1="12" y1="19" x2="20" y2="19" />
    </svg>
);

const KeyIcon = () => (
    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <rect x="2" y="4" width="20" height="16" rx="2" ry="2" />
        <line x1="6" y1="8" x2="6.01" y2="8" strokeWidth="2.5" />
        <line x1="10" y1="8" x2="10.01" y2="8" strokeWidth="2.5" />
        <line x1="14" y1="8" x2="14.01" y2="8" strokeWidth="2.5" />
        <line x1="8" y1="12" x2="16" y2="12" strokeWidth="2" />
    </svg>
);

const LinkIcon = () => (
    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <circle cx="8" cy="8" r="4" />
        <circle cx="16" cy="16" r="4" />
        <line x1="11" y1="11" x2="13" y2="13" />
    </svg>
);

const PlugIcon = () => (
    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <path d="M15 3h4a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2h-4" />
        <polyline points="10 17 15 12 10 7" />
        <line x1="15" y1="12" x2="3" y2="12" />
    </svg>
);



const ArrowRightIcon = () => (
    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
        <line x1="5" y1="12" x2="19" y2="12" />
        <polyline points="12 5 19 12 12 19" />
    </svg>
);


// ── Step configurations ──
const STEPS = [
    {
        target: null, // Full-screen welcome card
        tooltipPosition: 'bottom' as const,
    },
    {
        target: '.layer-tabs',
        tooltipPosition: 'bottom' as const,
    },
    {
        target: '.sidebar-rail',
        tooltipPosition: 'left' as const,
    },
];

export default function OnboardingWizard({ isConnected, onConnect }: OnboardingWizardProps) {
    const { step, nextStep, complete } = useOnboardingStore();
    const [displayStep, setDisplayStep] = useState(step);
    const [isTransitioning, setIsTransitioning] = useState(false);
    const [isConnecting, setIsConnecting] = useState(false);
    const [overlayState, setOverlayState] = useState<'entering' | 'visible' | 'exiting'>('entering');
    const overlayRef = useRef<HTMLDivElement>(null);

    // Fade in on mount
    useEffect(() => {
        const timer = setTimeout(() => setOverlayState('visible'), 50);
        return () => clearTimeout(timer);
    }, []);

    // Handle step transitions smoothly
    useEffect(() => {
        if (step !== displayStep) {
            setIsTransitioning(true);
            const t1 = setTimeout(() => {
                setDisplayStep(step);
                const t2 = setTimeout(() => {
                    setIsTransitioning(false);
                }, 50);
                return () => clearTimeout(t2);
            }, 300); // Wait for tooltip to fade out
            return () => clearTimeout(t1);
        }
    }, [step, displayStep]);

    const handleConnectClick = useCallback(async () => {
        setIsConnecting(true);
        try {
            await onConnect();
        } finally {
            setIsConnecting(false);
        }
    }, [onConnect]);

    const handleSkip = useCallback(() => {
        setOverlayState('exiting');
        setTimeout(complete, 500);
    }, [complete]);

    const handleFinish = useCallback(() => {
        setOverlayState('exiting');
        setTimeout(complete, 500);
    }, [complete]);

    const handleNext = useCallback(() => {
        if (step < 2) {
            nextStep();
        } else {
            handleFinish();
        }
    }, [step, nextStep, handleFinish]);

    // Initial focus when visible
    useEffect(() => {
        if (overlayState === 'visible') {
            overlayRef.current?.focus();
        }
    }, [overlayState]);

    // Keyboard navigation and focus trap
    useEffect(() => {
        const handleKeyDown = (e: KeyboardEvent) => {
            if (e.key === 'Escape') {
                handleSkip();
            } else if (e.key === 'Enter') {
                if (displayStep === 0) {
                    if (isConnected) nextStep();
                    else handleConnectClick();
                } else if (displayStep === 1) {
                    nextStep();
                } else if (displayStep === 2) {
                    handleFinish();
                }
            } else if (e.key === 'Tab') {
                if (!overlayRef.current) return;
                const focusable = overlayRef.current.querySelectorAll<HTMLElement>(
                    'button, [href], input, select, textarea, [tabindex]:not([tabindex="-1"])'
                );
                if (focusable.length === 0) return;

                const firstElement = focusable[0];
                const lastElement = focusable[focusable.length - 1];

                if (e.shiftKey && document.activeElement === firstElement) {
                    lastElement.focus();
                    e.preventDefault();
                } else if (!e.shiftKey && document.activeElement === lastElement) {
                    firstElement.focus();
                    e.preventDefault();
                }
            }
        };

        window.addEventListener('keydown', handleKeyDown);
        return () => window.removeEventListener('keydown', handleKeyDown);
    }, [displayStep, isConnected, isConnecting, handleSkip, handleConnectClick, nextStep, handleFinish]);

    const currentStep = STEPS[displayStep];

    const content = createPortal(
        <div
            ref={overlayRef}
            className={`onboarding-overlay ${overlayState}`}
            role="dialog"
            aria-modal="true"
            tabIndex={-1}
        >
            <SpotlightOverlay
                target={currentStep.target}
                tooltipPosition={currentStep.tooltipPosition}
                visible={true}
                isTransitioning={isTransitioning}
            >
                {displayStep === 0 && (
                    <div className="onboarding-welcome-card">
                        <div className="onboarding-welcome-icon">
                            <KeyboardIcon />
                            <div className="pulse-ring" />
                        </div>
                        <div className="onboarding-welcome-title">Welcome to Tecleados</div>
                        <div className="onboarding-welcome-subtitle">
                            Your custom keyboard is one click away.<br />
                            Plug in your keyboard and let's begin.
                        </div>
                        <div className="onboarding-welcome-footer">
                            <button
                                className={`onboarding-btn-connect ${isConnected ? 'success' : ''} ${isConnecting ? 'connecting' : ''}`}
                                onClick={isConnected ? nextStep : handleConnectClick}
                                disabled={isConnecting}
                            >
                                {isConnected ? <ArrowRightIcon /> : <PlugIcon />}
                                {isConnecting ? 'Connecting...' : isConnected ? 'Start Tutorial' : 'Connect Keyboard'}
                            </button>
                            <button className="onboarding-skip" onClick={handleSkip}>
                                Skip Tutorial →
                            </button>
                            <StepDots current={0} total={3} />
                        </div>
                    </div>
                )}

                {displayStep === 1 && (
                    <>
                        <div className="onboarding-tooltip-title">
                            <LayersIcon />
                            The Layer System
                        </div>
                        <div className="onboarding-tooltip-body">
                            Your keyboard's primary layout is the <strong>Base</strong> layer, which handles standard typing.
                            <div style={{ color: 'var(--text-secondary)', fontStyle: 'italic', fontSize: '0.85em', marginTop: '4px' }}>
                                Note that this layer can't be removed nor renamed
                            </div>
                            <br />
                            By holding a function key like <strong>FN1</strong> or <strong>FN2</strong>, you temporarily activate a different layer, giving your keys entirely new functions.
                            <br /><br />
                            The <strong>FN3</strong> layer, by default, activates when pressing both <strong>FN1</strong> and <strong>FN2</strong> keys simultaneously.
                            <div style={{ color: 'var(--text-secondary)', fontStyle: 'italic', fontSize: '0.85em', marginTop: '4px' }}>
                                This behaviour has been achieved using a <strong>Combo</strong>
                            </div>
                            <br />
                            If a key on an active layer is set to <strong>"Transparent"</strong>, it simply uses the function from the <strong>Base</strong> layer.
                        </div>
                        <div className="onboarding-tooltip-footer">
                            <div className="onboarding-tooltip-actions">
                                <button className="onboarding-btn-primary" onClick={handleNext}>
                                    Next
                                    <ArrowRightIcon />
                                </button>
                                <button className="onboarding-skip" onClick={handleSkip}>
                                    Skip →
                                </button>
                            </div>
                            <StepDots current={1} total={3} />
                        </div>
                    </>
                )}

                {displayStep === 2 && (
                    <>
                        <div className="onboarding-tooltip-title">
                            <ZapIcon />
                            Power Features
                        </div>
                        <div className="onboarding-tooltip-body">
                            The sidebar gives you access to three advanced tools:
                        </div>
                        <div className="onboarding-feature-list">
                            <div className="onboarding-feature-item">
                                <TerminalIcon />
                                <span><strong>Macros</strong> — automate key sequences and complex workflows</span>
                            </div>
                            <div className="onboarding-feature-item">
                                <KeyIcon />
                                <span><strong>Custom Keys</strong> — tap, hold, or double-tap for different behaviors</span>
                            </div>
                            <div className="onboarding-feature-item">
                                <LinkIcon />
                                <span><strong>Combos</strong> — trigger actions on simultaneous key presses</span>
                            </div>
                        </div>
                        <div className="onboarding-tooltip-footer" style={{ marginTop: '1.25rem' }}>
                            <div className="onboarding-tooltip-actions">
                                <button className="onboarding-btn-primary" onClick={handleFinish}>
                                    Start Configuring
                                    <ArrowRightIcon />
                                </button>
                                <button className="onboarding-skip" onClick={handleSkip}>
                                    Skip →
                                </button>
                            </div>
                            <StepDots current={2} total={3} />
                        </div>
                    </>
                )}
            </SpotlightOverlay>
        </div>,
        document.body,
    );

    return content;
}
