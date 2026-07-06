import { create } from 'zustand';

const STORAGE_KEY = 'tef_onboarding_completed';

interface OnboardingState {
    /** Whether the user has finished or skipped the wizard */
    hasCompleted: boolean;
    /** Current wizard step: 0 = Welcome/Connect, 1 = Layers, 2 = Sidebar Features */
    step: number;
    /** Advance to the next step (clamped to 0–2) */
    nextStep: () => void;
    /** Jump to a specific step */
    goToStep: (step: number) => void;
    /** Mark onboarding as done, persist to localStorage */
    complete: () => void;
    /** Reset onboarding (for "Replay Tour" in Settings) */
    reset: () => void;
}

export const useOnboardingStore = create<OnboardingState>((set) => ({
    hasCompleted: localStorage.getItem(STORAGE_KEY) === 'true',
    step: 0,
    nextStep: () => set((s) => ({ step: Math.min(s.step + 1, 2) })),
    goToStep: (step) => set({ step }),
    complete: () => {
        localStorage.setItem(STORAGE_KEY, 'true');
        set({ hasCompleted: true });
    },
    reset: () => {
        localStorage.removeItem(STORAGE_KEY);
        set({ hasCompleted: false, step: 0 });
    },
}));
