import React from 'react';

// ── Execution Mode Helpers ──────────────────────────────────────────────
export type ModeCategory = 'once' | 'repeat' | 'burst';

export function getModeCategory(execMode: number): ModeCategory {
    if (execMode >= 3 && execMode <= 6) return 'repeat';
    if (execMode === 7) return 'burst';
    return 'once';
}

// ── Execution Mode Icons (2-symbol system) ──────────────────────────────
export const IconOne = () => <span className="m-sym">1</span>;
export const IconN = () => <span className="m-sym">N</span>;
export const IconXSmall = () => <span className="m-sym m-smaller">×</span>;
export const IconPlus = () => <span className="m-sym m-smaller">+</span>;
export const IconExclamation = () => <span className="m-sym m-smaller">!</span>;
export const IconMultiply = () => <span className="m-sym m-smaller">×</span>;

export const IconHold = () => (
    <svg className="m-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
        <path d="M12 4v14M7 13l5 5 5-5M5 21h14" />
    </svg>
);

export const IconToggle = () => (
    <svg className="m-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round">
        <path d="M18.36 6.64a9 9 0 1 1-12.73 0M12 2v10" />
    </svg>
);

export const IconLoop = () => (
    <svg className="m-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
        <path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8" />
        <polyline points="21 3 21 8 16 8" />
    </svg>
);

export const IconLoopCancel = () => (
    <svg className="m-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
        <path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8" />
        <polyline points="21 3 21 8 16 8" />
        <line x1="10" y1="10" x2="14" y2="14" strokeWidth="3" />
        <line x1="14" y1="10" x2="10" y2="14" strokeWidth="3" />
    </svg>
);

export function getModeBadge(execMode: number): React.ReactNode {
    // 0=stack-once, 1=no-stack, 2=stack-N
    // 3=hold-finish, 4=hold-cancel, 5=toggle-finish, 6=toggle-cancel, 7=burst
    switch (execMode) {
        case 0: return <><IconOne /><IconXSmall /></>;
        case 1: return <><IconOne /><IconExclamation /></>;
        case 2: return <><IconN /><IconPlus /></>;
        case 3: return <><IconLoop /><IconHold /></>;
        case 4: return <><IconLoopCancel /><IconHold /></>;
        case 5: return <><IconLoop /><IconToggle /></>;
        case 6: return <><IconLoopCancel /><IconToggle /></>;
        case 7: return <><IconN /><IconMultiply /></>;
        default: return <><IconOne /><IconXSmall /></>;
    }
}
