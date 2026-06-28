import { useState, useEffect } from 'react';
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

const CheckIcon = () => (
    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
        <polyline points="20 6 9 17 4 12" />
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
    const [connectSuccess, setConnectSuccess] = useState(false);
    const [overlayState, setOverlayState] = useState<'entering' | 'visible' | 'exiting'>('entering');

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

    // When connected during step 0, show success then advance
    useEffect(() => {
        if (isConnected && step === 0) {
            setConnectSuccess(true);
            const timer = setTimeout(() => {
                nextStep();
                setConnectSuccess(false);
            }, 800);
            return () => clearTimeout(timer);
        }
    }, [isConnected, step, nextStep]);

    const handleSkip = () => {
        setOverlayState('exiting');
        setTimeout(complete, 500);
    };

    const handleFinish = () => {
        setOverlayState('exiting');
        setTimeout(complete, 500);
    };

    const handleNext = () => {
        if (step < 2) {
            nextStep();
        } else {
            handleFinish();
        }
    };

    const currentStep = STEPS[displayStep];

    const content = createPortal(
        <div className={`onboarding-overlay ${overlayState}`}>
            <SpotlightOverlay
                target={currentStep.target}
                tooltipPosition={currentStep.tooltipPosition}
                visible={overlayState !== 'exiting'}
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
                                className={`onboarding-btn-connect ${connectSuccess ? 'success' : ''}`}
                                onClick={onConnect}
                                disabled={connectSuccess}
                            >
                                {connectSuccess ? <CheckIcon /> : <PlugIcon />}
                                {connectSuccess ? 'Connected!' : 'Connect Keyboard'}
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
                            Your keyboard has <strong>4 independent layers</strong>.
                            The <strong>Base</strong> layer handles normal typing.
                            Hold <strong>FN1</strong> or <strong>FN2</strong> to access secondary functions.
                            <strong> FN3</strong> activates when both FN keys are held at once.
                            <br /><br />
                            Keys left <strong>"Transparent"</strong> will fall through to the Base layer below.
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
