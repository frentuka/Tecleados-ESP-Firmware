import { useState } from 'react';
import { createPortal } from 'react-dom';
import type { Macro } from '../types/macros';
import type { ModeCategory } from './MacroIcons';
import { getModeCategory } from './MacroIcons';

interface MacroModeModalProps {
    macro: Macro;
    onSave: (macro: Macro) => void;
    onClose: () => void;
}

export default function MacroModeModal({ macro, onSave, onClose }: MacroModeModalProps) {
    const categoryButtons: { id: ModeCategory; icon: string; label: string; tagline: string }[] = [
        { id: 'once', icon: '1×', label: 'Once', tagline: 'Runs the macro a single time per press' },
        { id: 'repeat', icon: '⟳', label: 'Repeat', tagline: 'Loops the macro continuously' },
        { id: 'burst', icon: 'N×', label: 'Burst', tagline: 'Fires the macro multiple times per press' },
    ];

    const initCat = getModeCategory(macro.execMode ?? 0);
    const initOnce = (macro.execMode !== undefined && macro.execMode <= 2) ? macro.execMode : 0;
    const initRepeatTrigger: 'hold' | 'toggle' = (macro.execMode === 5 || macro.execMode === 6) ? 'toggle' : 'hold';
    const initRepeatCancel = (macro.execMode === 4 || macro.execMode === 6);

    const [category, setCategory] = useState<ModeCategory>(initCat);
    const [onceMode, setOnceMode] = useState<number>(initOnce); // 0=stack-once, 1=no-stack, 2=stack-N
    const [stackMax, setStackMax] = useState(macro.stackMax ?? 2);
    const [repeatTrigger, setRepeatTrigger] = useState<'hold' | 'toggle'>(initRepeatTrigger);
    const [repeatCancel, setRepeatCancel] = useState(initRepeatCancel);
    const [repeatCount, setRepeatCount] = useState(macro.repeatCount ?? 2);
    const [mouseDownOnOverlay, setMouseDownOnOverlay] = useState(false);

    const computeExecMode = (): number => {
        if (category === 'once') return onceMode;
        if (category === 'repeat') {
            if (repeatTrigger === 'hold') return repeatCancel ? 4 : 3;
            return repeatCancel ? 6 : 5;
        }
        return 7; // burst
    };

    const handleSave = () => {
        const mode = computeExecMode();
        const updated: Macro = {
            ...macro,
            execMode: mode,
            stackMax: mode === 2 ? Math.max(1, stackMax) : undefined,
            repeatCount: mode === 7 ? Math.max(1, repeatCount) : undefined,
        };
        onSave(updated);
        onClose();
    };

    return createPortal(
        <div
            className="modal-overlay"
            onMouseDown={e => { if (e.target === e.currentTarget) setMouseDownOnOverlay(true); else setMouseDownOnOverlay(false); }}
            onMouseUp={e => { if (mouseDownOnOverlay && e.target === e.currentTarget) onClose(); setMouseDownOnOverlay(false); }}
        >
            <div className="modal-content macro-mode-modal" onClick={e => e.stopPropagation()}>
                <div className="modal-header">
                    <h3>Execution Mode — {macro.name || `Macro #${macro.id}`}</h3>
                </div>
                <div className="modal-body" style={{ padding: '1.25rem 1.5rem' }}>
                    <p className="macro-mode-intro">Choose how this macro behaves when its key is pressed.</p>

                    <div className="macro-mode-categories">
                        {categoryButtons.map(cat => (
                            <div key={cat.id} className="macro-mode-cat-block">
                                <button
                                    className={`macro-mode-cat-btn ${category === cat.id ? 'active' : ''}`}
                                    onClick={() => setCategory(cat.id)}
                                >
                                    <span className="macro-mode-cat-icon">{cat.icon}</span>
                                    <div className="macro-mode-cat-text">
                                        <span className="macro-mode-cat-label">{cat.label}</span>
                                        <span className="macro-mode-cat-tagline">{cat.tagline}</span>
                                    </div>
                                </button>

                                {category === 'once' && cat.id === 'once' && (
                                    <div className="macro-mode-suboptions">
                                        <div className="macro-mode-sub-title">If pressed again while still running:</div>
                                        <label className={`macro-mode-radio ${onceMode === 0 ? 'active' : ''}`} onClick={() => setOnceMode(0)}>
                                            <input type="radio" name="once" checked={onceMode === 0} readOnly />
                                            <div>
                                                <div className="macro-mode-radio-label">Queue once</div>
                                                <div className="macro-mode-radio-desc">Runs one more time after the current execution finishes. Additional presses are ignored.</div>
                                            </div>
                                        </label>
                                        <label className={`macro-mode-radio ${onceMode === 1 ? 'active' : ''}`} onClick={() => setOnceMode(1)}>
                                            <input type="radio" name="once" checked={onceMode === 1} readOnly />
                                            <div>
                                                <div className="macro-mode-radio-label">Ignore</div>
                                                <div className="macro-mode-radio-desc">Extra presses are completely ignored until the macro finishes running.</div>
                                            </div>
                                        </label>
                                        <label className={`macro-mode-radio ${onceMode === 2 ? 'active' : ''}`} onClick={() => setOnceMode(2)}>
                                            <input type="radio" name="once" checked={onceMode === 2} readOnly />
                                            <div>
                                                <div className="macro-mode-radio-label">Queue up to N times</div>
                                                <div className="macro-mode-radio-desc">Each press while running adds another queued execution, up to a maximum of N.</div>
                                            </div>
                                        </label>
                                        {onceMode === 2 && (
                                            <div className="macro-mode-input-row" onClick={e => e.stopPropagation()}>
                                                <span>Max queued runs:</span>
                                                <input
                                                    type="number" min="1" max="255"
                                                    value={stackMax === 0 ? '' : stackMax}
                                                    onChange={e => setStackMax(e.target.value === '' ? 0 : parseInt(e.target.value) || 1)}
                                                    className="macro-mode-input"
                                                />
                                            </div>
                                        )}
                                    </div>
                                )}

                                {category === 'repeat' && cat.id === 'repeat' && (
                                    <div className="macro-mode-suboptions">
                                        <div className="macro-mode-sub-title">How to start and stop:</div>
                                        <label className={`macro-mode-radio ${repeatTrigger === 'hold' ? 'active' : ''}`} onClick={() => setRepeatTrigger('hold')}>
                                            <input type="radio" name="trigger" checked={repeatTrigger === 'hold'} readOnly />
                                            <div>
                                                <div className="macro-mode-radio-label">Hold to repeat</div>
                                                <div className="macro-mode-radio-desc">The macro loops as long as the key is physically held down. Releasing the key stops it.</div>
                                            </div>
                                        </label>
                                        <label className={`macro-mode-radio ${repeatTrigger === 'toggle' ? 'active' : ''}`} onClick={() => setRepeatTrigger('toggle')}>
                                            <input type="radio" name="trigger" checked={repeatTrigger === 'toggle'} readOnly />
                                            <div>
                                                <div className="macro-mode-radio-label">Toggle</div>
                                                <div className="macro-mode-radio-desc">Press once to start looping, press again to stop. No need to hold the key.</div>
                                            </div>
                                        </label>

                                        <div className="macro-mode-sub-title" style={{ marginTop: '0.75rem' }}>When stopped:</div>
                                        <label className={`macro-mode-radio ${!repeatCancel ? 'active' : ''}`} onClick={() => setRepeatCancel(false)}>
                                            <input type="radio" name="cancel" checked={!repeatCancel} readOnly />
                                            <div>
                                                <div className="macro-mode-radio-label">Finish current run</div>
                                                <div className="macro-mode-radio-desc">Completes the macro iteration that's currently executing before stopping. All keystrokes finish cleanly.</div>
                                            </div>
                                        </label>
                                        <label className={`macro-mode-radio ${repeatCancel ? 'active' : ''}`} onClick={() => setRepeatCancel(true)}>
                                            <input type="radio" name="cancel" checked={repeatCancel} readOnly />
                                            <div>
                                                <div className="macro-mode-radio-label">Cancel immediately</div>
                                                <div className="macro-mode-radio-desc">Stops the macro mid-execution. Any keys currently held by the macro will be released instantly.</div>
                                            </div>
                                        </label>
                                    </div>
                                )}

                                {category === 'burst' && cat.id === 'burst' && (
                                    <div className="macro-mode-suboptions">
                                        <div className="macro-mode-sub-title">A single key press will fire the macro this many times in a row:</div>
                                        <div className="macro-mode-input-row" onClick={e => e.stopPropagation()}>
                                            <span>Repeat count:</span>
                                            <input
                                                type="number" min="1" max="255"
                                                value={repeatCount === 0 ? '' : repeatCount}
                                                onChange={e => setRepeatCount(e.target.value === '' ? 0 : parseInt(e.target.value) || 1)}
                                                className="macro-mode-input"
                                            />
                                            <span className="macro-mode-input-suffix">times</span>
                                        </div>
                                    </div>
                                )}
                            </div>
                        ))}
                    </div>
                </div>
                <div className="modal-footer">
                    <button className="btn" onClick={onClose}>Cancel</button>
                    <button className="btn btn-success" onClick={handleSave}>Apply</button>
                </div>
            </div>
        </div>,
        document.body
    );
}
