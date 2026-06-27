import Lottie from 'lottie-react';

interface EmptyStateProps {
    /** Lottie JSON animation data (imported as a module) */
    animation: object;
    /** Main heading text */
    title: string;
    /** Supporting description text */
    description: string;
    /** CTA button label */
    actionLabel: string;
    /** Callback when the CTA button is clicked */
    onAction: () => void;
    /** Whether the CTA button should be disabled (e.g. at limit) */
    disabled?: boolean;
}

export default function EmptyState({
    animation,
    title,
    description,
    actionLabel,
    onAction,
    disabled = false,
}: EmptyStateProps) {
    return (
        <div className="empty-state-rich">
            <div className="empty-state-animation">
                <Lottie animationData={animation} loop autoplay style={{ width: 120, height: 120 }} />
            </div>
            <div className="empty-state-title">{title}</div>
            <div className="empty-state-description">{description}</div>
            <button
                className="btn-new-action btn-new-success"
                onClick={onAction}
                disabled={disabled}
            >
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
                    <line x1="12" y1="5" x2="12" y2="19" />
                    <line x1="5" y1="12" x2="19" y2="12" />
                </svg>
                {actionLabel}
            </button>
        </div>
    );
}
