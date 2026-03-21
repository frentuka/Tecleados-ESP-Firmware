/**
 * Custom Key types — previously defined in HIDService.ts.
 */

export interface CustomKeyPR {
    pressAction: number;
    releaseAction: number;
    pressDuration: number;
    releaseDuration: number;
    waitForFinish: boolean;
}

export interface CustomKeyMA {
    tapAction: number;
    doubleTapAction: number;
    holdAction: number;
    doubleTapThreshold: number;
    holdThreshold: number;
    tapDuration: number;
    doubleTapDuration: number;
    holdDuration: number;
}

export interface CustomKey {
    id: number;
    name: string;
    mode: number; // 0 = PressRelease, 1 = MultiAction
    pr?: CustomKeyPR;
    ma?: CustomKeyMA;
}
