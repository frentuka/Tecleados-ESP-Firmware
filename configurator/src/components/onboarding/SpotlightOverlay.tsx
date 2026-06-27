import { useState, useEffect, useCallback, useRef } from 'react';

interface SpotlightOverlayProps {
    /** CSS selector for the element to spotlight, or null for no spotlight (full dim) */
    target: string | null;
    /** Padding around the target element in px */
    padding?: number;
    /** Content to render (the tooltip card) */
    children: React.ReactNode;
    /** Where to position the tooltip relative to the cutout */
    tooltipPosition?: 'top' | 'bottom' | 'left' | 'right';
    /** Whether the overlay is visible */
    visible?: boolean;
}

interface Rect {
    top: number;
    left: number;
    width: number;
    height: number;
}

export default function SpotlightOverlay({
    target,
    padding = 12,
    children,
    tooltipPosition = 'bottom',
    visible = true,
}: SpotlightOverlayProps) {
    const [targetRect, setTargetRect] = useState<Rect | null>(null);
    const [tooltipStyle, setTooltipStyle] = useState<React.CSSProperties>({});
    const [tooltipClass, setTooltipClass] = useState('entering');
    const tooltipRef = useRef<HTMLDivElement>(null);

    // Measure target element
    const measureTarget = useCallback(() => {
        if (!target) {
            setTargetRect(null);
            return;
        }
        const el = document.querySelector(target);
        if (!el) {
            setTargetRect(null);
            return;
        }
        const r = el.getBoundingClientRect();
        setTargetRect({
            top: r.top - padding,
            left: r.left - padding,
            width: r.width + padding * 2,
            height: r.height + padding * 2,
        });
    }, [target, padding]);

    useEffect(() => {
        measureTarget();
        window.addEventListener('resize', measureTarget);
        window.addEventListener('scroll', measureTarget);

        // Also re-measure after a short delay for layout settling
        const timer = setTimeout(measureTarget, 100);

        return () => {
            window.removeEventListener('resize', measureTarget);
            window.removeEventListener('scroll', measureTarget);
            clearTimeout(timer);
        };
    }, [measureTarget]);

    // Position tooltip relative to the spotlight cutout
    useEffect(() => {
        if (!targetRect || !tooltipRef.current) {
            // No target = centered (welcome card handles its own layout)
            setTooltipStyle({});
            return;
        }

        const tooltipEl = tooltipRef.current;
        const tooltipW = tooltipEl.offsetWidth || 360;
        const tooltipH = tooltipEl.offsetHeight || 300;
        const vw = window.innerWidth;
        const vh = window.innerHeight;
        const gap = 16;

        let top = 0;
        let left = 0;
        let finalPosition = tooltipPosition;

        // Try preferred position first, then flip if it overflows
        const tryPosition = (pos: string) => {
            switch (pos) {
                case 'bottom':
                    top = targetRect.top + targetRect.height + gap;
                    left = targetRect.left + targetRect.width / 2 - tooltipW / 2;
                    break;
                case 'top':
                    top = targetRect.top - tooltipH - gap;
                    left = targetRect.left + targetRect.width / 2 - tooltipW / 2;
                    break;
                case 'left':
                    top = targetRect.top + targetRect.height / 2 - tooltipH / 2;
                    left = targetRect.left - tooltipW - gap;
                    break;
                case 'right':
                    top = targetRect.top + targetRect.height / 2 - tooltipH / 2;
                    left = targetRect.left + targetRect.width + gap;
                    break;
            }
        };

        tryPosition(tooltipPosition);

        // Flip if overflow
        if (tooltipPosition === 'bottom' && top + tooltipH > vh - 20) {
            finalPosition = 'top';
            tryPosition('top');
        } else if (tooltipPosition === 'top' && top < 20) {
            finalPosition = 'bottom';
            tryPosition('bottom');
        } else if (tooltipPosition === 'left' && left < 20) {
            finalPosition = 'right';
            tryPosition('right');
        } else if (tooltipPosition === 'right' && left + tooltipW > vw - 20) {
            finalPosition = 'left';
            tryPosition('left');
        }

        // Clamp to viewport edges
        left = Math.max(16, Math.min(left, vw - tooltipW - 16));
        top = Math.max(16, Math.min(top, vh - tooltipH - 16));

        setTooltipStyle({ top, left });
        void finalPosition; // used for future directional arrow
    }, [targetRect, tooltipPosition]);

    // Animate tooltip in
    useEffect(() => {
        setTooltipClass('entering');
        const timer = setTimeout(() => setTooltipClass('visible'), 80);
        return () => clearTimeout(timer);
    }, [target]);

    if (!visible) return null;

    // Build the SVG mask: full-screen rectangle with a cutout hole
    const svgMask = targetRect ? (
        <svg
            style={{ position: 'absolute', inset: 0, width: '100%', height: '100%' }}
            xmlns="http://www.w3.org/2000/svg"
        >
            <defs>
                <mask id="spotlight-mask">
                    {/* White = visible (dimmed), black = hole (transparent) */}
                    <rect x="0" y="0" width="100%" height="100%" fill="white" />
                    <rect
                        x={targetRect.left}
                        y={targetRect.top}
                        width={targetRect.width}
                        height={targetRect.height}
                        rx="16"
                        ry="16"
                        fill="black"
                    />
                </mask>
            </defs>
            <rect
                x="0" y="0"
                width="100%" height="100%"
                fill="rgba(0, 0, 0, 0.78)"
                mask="url(#spotlight-mask)"
            />
        </svg>
    ) : (
        <div className="onboarding-dim" />
    );

    return (
        <>
            {/* Dimming layer with cutout */}
            <div style={{ position: 'fixed', inset: 0, zIndex: 9000, pointerEvents: 'auto' }}>
                {svgMask}
            </div>

            {/* Glow ring around the cutout */}
            {targetRect && (
                <div
                    className="onboarding-spotlight-ring"
                    style={{
                        top: targetRect.top,
                        left: targetRect.left,
                        width: targetRect.width,
                        height: targetRect.height,
                    }}
                />
            )}

            {/* Tooltip card */}
            {target ? (
                <div
                    ref={tooltipRef}
                    className={`onboarding-tooltip ${tooltipClass}`}
                    style={tooltipStyle}
                >
                    {children}
                </div>
            ) : (
                // No target = full-screen content (welcome card)
                <div className="onboarding-welcome-wrapper">
                    {children}
                </div>
            )}
        </>
    );
}
