import type { MacroElement } from '../types/macros';
import type { TimelineBlock } from '../types/timeline';

export function elementsToTimeline(elements: MacroElement[]): TimelineBlock[] {
    let currentTime = 0;
    const blocks: TimelineBlock[] = [];
    const pendingPresses: Record<number, { startTime: number, id: string }> = {};
    let nextId = 1;

    for (const el of elements) {
        if (el.type === 'sleep') {
            currentTime += el.duration;
        } else if (el.type === 'key') {
            const action = el.action || 'tap';
            
            if (action === 'tap') {
                blocks.push({
                    id: `blk-${nextId++}`,
                    trackId: `trk-${el.key}`,
                    key: el.key,
                    startTime: currentTime,
                    duration: el.pressTime ?? 10,
                    type: 'tap'
                });
            } else if (action === 'press') {
                // Register a pending press
                pendingPresses[el.key] = { startTime: currentTime, id: `blk-${nextId++}` };
            } else if (action === 'release') {
                if (pendingPresses[el.key]) {
                    const press = pendingPresses[el.key];
                    blocks.push({
                        id: press.id,
                        trackId: `trk-${el.key}`,
                        key: el.key,
                        startTime: press.startTime,
                        duration: Math.max(0, currentTime - press.startTime),
                        type: 'hold'
                    });
                    delete pendingPresses[el.key];
                } else {
                    // Orphaned release
                    blocks.push({
                        id: `blk-${nextId++}`,
                        trackId: `trk-${el.key}`,
                        key: el.key,
                        startTime: currentTime,
                        duration: 0,
                        type: 'release'
                    });
                }
            }
            
            if (el.inlineSleep) {
                currentTime += el.inlineSleep;
            }
        }
    }

    // Any pending presses left over become 'press' blocks
    for (const keyStr in pendingPresses) {
        const key = parseInt(keyStr, 10);
        const press = pendingPresses[key];
        blocks.push({
            id: press.id,
            trackId: `trk-${key}`,
            key: key,
            startTime: press.startTime,
            duration: 0,
            type: 'press'
        });
    }

    return blocks;
}

interface Edge {
    time: number;
    key: number;
    action: 'tap' | 'press' | 'release';
    pressTime?: number;
}

export function timelineToElements(blocks: TimelineBlock[]): MacroElement[] {
    const edges: Edge[] = [];

    for (const block of blocks) {
        if (block.type === 'tap') {
            edges.push({ time: block.startTime, key: block.key, action: 'tap', pressTime: block.duration });
        } else if (block.type === 'hold') {
            edges.push({ time: block.startTime, key: block.key, action: 'press' });
            edges.push({ time: block.startTime + block.duration, key: block.key, action: 'release' });
        } else if (block.type === 'press') {
            edges.push({ time: block.startTime, key: block.key, action: 'press' });
        } else if (block.type === 'release') {
            edges.push({ time: block.startTime, key: block.key, action: 'release' });
        }
    }

    // Sort edges by time
    // If times are equal, order doesn't strictly matter, but putting releases before presses might be cleaner
    edges.sort((a, b) => a.time - b.time);

    const elements: MacroElement[] = [];
    let lastTime = 0;

    for (const edge of edges) {
        const delay = edge.time - lastTime;
        if (delay > 0) {
            // Try to fold delay into the previous element's inlineSleep to save space
            if (elements.length > 0) {
                const prev = elements[elements.length - 1];
                if (prev.type === 'key' && prev.inlineSleep === undefined) {
                    prev.inlineSleep = delay;
                } else {
                    elements.push({ type: 'sleep', duration: delay });
                }
            } else {
                elements.push({ type: 'sleep', duration: delay });
            }
            lastTime = edge.time;
        }

        const el: MacroElement = { type: 'key', key: edge.key, action: edge.action };
        if (edge.action === 'tap') {
            if (edge.pressTime !== undefined && edge.pressTime !== 10) { // Default is 10
                el.pressTime = edge.pressTime;
            }
        }
        elements.push(el);
    }

    return elements;
}
