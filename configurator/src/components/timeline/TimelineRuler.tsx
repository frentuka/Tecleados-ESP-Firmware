import React from 'react';

interface TimelineRulerProps {
    pxPerMs: number;
    totalMs: number;
}

export default function TimelineRuler({ pxPerMs, totalMs }: TimelineRulerProps) {
    // Target distance between main labels is roughly 100px.
    // So stepMs * pxPerMs ≈ 100 => stepMs ≈ 100 / pxPerMs
    let targetStepMs = 100 / pxPerMs;
    
    // Find the closest "nice" round number (1, 2, 5, 10, etc)
    let magnitude = Math.pow(10, Math.floor(Math.log10(targetStepMs)));
    let firstDigit = targetStepMs / magnitude;
    
    let stepMs;
    if (firstDigit <= 1.5) stepMs = magnitude;
    else if (firstDigit <= 3.5) stepMs = 2 * magnitude;
    else if (firstDigit <= 7.5) stepMs = 5 * magnitude;
    else stepMs = 10 * magnitude;

    let subSteps = 10;
    if (stepMs / 5 * pxPerMs >= 10) subSteps = 5;
    if (stepMs / 10 * pxPerMs >= 10) subSteps = 10;

    // HARD LIMIT: Prevent browser from crashing if totalMs is absurdly huge (e.g. millions)
    // If the timeline width exceeds tens of thousands of pixels, we just space out the ticks
    // more sparsely so we never render more than 1000 DOM nodes.
    const MAX_TICKS = 1000;
    if (totalMs / stepMs > MAX_TICKS) {
        stepMs = Math.ceil(totalMs / MAX_TICKS);
        subSteps = 1; // disable subticks for absurdly huge widths
    }

    const numTicks = Math.ceil(totalMs / stepMs) + 1;
    const ticks = [];
    const subTicks = [];

    for (let i = 0; i < numTicks; i++) {
        const tickTime = i * stepMs;
        ticks.push(tickTime);
        
        if (i < numTicks - 1 && subSteps > 1) {
            const subStepMs = stepMs / subSteps;
            for (let j = 1; j < subSteps; j++) {
                subTicks.push(tickTime + j * subStepMs);
            }
        }
    }

    const formatTime = (ms: number) => {
        if (ms >= 3600000) {
            return `${(ms / 3600000).toFixed(2)}h`;
        }
        if (ms >= 60000) {
            return `${(ms / 60000).toFixed(2)}m`;
        }
        if (ms >= 1000) {
            return `${(ms / 1000).toFixed(1)}s`;
        }
        return `${Math.round(ms)}ms`;
    };

    return (
        <div className="timeline-ruler">
            {subTicks.map(tick => (
                <div key={`sub-${tick}`} className="timeline-ruler-subtick" style={{ left: `${tick * pxPerMs}px` }} />
            ))}
            {ticks.map(tick => (
                <div key={tick} className="timeline-ruler-tick" style={{ left: `${tick * pxPerMs}px` }}>
                    {formatTime(tick)}
                </div>
            ))}
        </div>
    );
}
