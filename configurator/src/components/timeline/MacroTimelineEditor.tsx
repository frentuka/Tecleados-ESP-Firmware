import React, { useState, useEffect, useRef, useMemo, useImperativeHandle, forwardRef } from 'react';
import type { MacroElement, Macro } from '../../types/macros';
import type { TimelineBlock, TimelineTrack } from '../../types/timeline';
import { elementsToTimeline, timelineToElements } from '../../utils/macroTimelineAdapter';
import TimelineTrackComponent from './TimelineTrack';
import TimelineRuler from './TimelineRuler';
import { BROWSER_CODE_TO_HID, getKeyName } from '../../KeyDefinitions';
import './timeline.css';

export interface MacroTimelineRef {
    addKeyBlock: (key: number) => void;
    addSleepBlock: (duration: number) => void;
    clearAll: () => void;
}

interface MacroTimelineEditorProps {
    elements: MacroElement[];
    macros: Macro[];
    onChange: (elements: MacroElement[]) => void;
    isRecording: boolean;
    recordDelay: boolean;
    maxEvents?: number;
    isActiveView: boolean;
}

const MacroTimelineEditor = forwardRef<MacroTimelineRef, MacroTimelineEditorProps>(({ 
    elements, 
    macros, 
    onChange, 
    isRecording,
    recordDelay,
    maxEvents,
    isActiveView
}, ref) => {
    const lastElementsRef = useRef<MacroElement[]>(elements);
    const pendingElementsRef = useRef<MacroElement[] | null>(null);

    const [blocks, setBlocks] = useState<TimelineBlock[]>(() => elementsToTimeline(elements));
    
    // 1. Sync blocks from elements if elements changed EXTERNALLY
    useEffect(() => {
        // If the incoming elements are the exact array we just emitted, ignore them!
        if (elements === lastElementsRef.current) return;
        
        if (isActiveView) {
            setBlocks(elementsToTimeline(elements));
            pendingElementsRef.current = null;
        } else {
            pendingElementsRef.current = elements;
        }
    }, [elements, isActiveView]);

    // 2. Catch up when becoming active
    useEffect(() => {
        if (isActiveView && pendingElementsRef.current) {
            setBlocks(elementsToTimeline(pendingElementsRef.current));
            pendingElementsRef.current = null;
        }
    }, [isActiveView]);
    const [zoom, setZoom] = useState(1); // 1 = 1px per ms
    const [selectedBlockIds, setSelectedBlockIds] = useState<Set<string>>(new Set());
    
    // Marquee state
    const [isSelecting, setIsSelecting] = useState(false);
    const [selectionBox, setSelectionBox] = useState<{ startX: number, startY: number, endX: number, endY: number } | null>(null);

    const scrollContainerRef = useRef<HTMLDivElement>(null);
    const containerInnerRef = useRef<HTMLDivElement>(null);
    const playheadRef = useRef<HTMLDivElement>(null);
    const nextBlockIdRef = useRef(Date.now());

    useImperativeHandle(ref, () => ({
        addKeyBlock: (key: number) => {
            let maxTime = 0;
            for (const b of blocks) {
                if (b.startTime + b.duration > maxTime) maxTime = b.startTime + b.duration;
            }
            const blockId = `blk-${nextBlockIdRef.current++}`;
            setBlocks(prev => [...prev, {
                id: blockId,
                trackId: `trk-${key}`,
                key: key,
                startTime: maxTime,
                duration: 20,
                type: 'tap'
            }]);
        },
        addSleepBlock: (duration: number) => {
            // Shift selected blocks right. If none selected, shift ALL blocks right by duration.
            // If we are appending delay at the end, shifting all blocks is not what we want.
            // Wait, "Add Delay" from the toolbar usually means we want a gap.
            // If the user selects a block and clicks "Add Delay", shift everything starting from that block.
            setBlocks(prev => {
                let shiftThreshold = 0;
                if (selectedBlockIds.size > 0) {
                    const selectedBlocks = prev.filter(b => selectedBlockIds.has(b.id));
                    shiftThreshold = Math.min(...selectedBlocks.map(b => b.startTime));
                } else {
                    // Shift everything that is after the current absolute max time? No, then delay does nothing.
                    // Just shift ALL blocks right by 'duration' to insert a delay at the start.
                    shiftThreshold = -1;
                }
                
                return prev.map(b => ({
                    ...b,
                    startTime: b.startTime >= shiftThreshold ? b.startTime + duration : b.startTime
                }));
            });
        },
        clearAll: () => {
            setBlocks([]);
            setSelectedBlockIds(new Set());
        }
    }));

    // 3. Emit internal changes
    useEffect(() => {
        if (!isActiveView) return;
        const newElements = timelineToElements(blocks);
        lastElementsRef.current = newElements;
        onChange(newElements);
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [blocks]);

    const handleBlockChange = (id: string, newStartTime: number, newDuration: number) => {
        setBlocks(prev => prev.map(b => 
            b.id === id ? { ...b, startTime: newStartTime, duration: newDuration } : b
        ));
    };

    const handleMoveBlocks = (blockIds: string[], deltaMs: number) => {
        setBlocks(prev => {
            const newBlocks = [...prev];
            blockIds.forEach(id => {
                const idx = newBlocks.findIndex(b => b.id === id);
                if (idx !== -1) {
                    const block = newBlocks[idx];
                    newBlocks[idx] = { ...block, startTime: Math.max(0, block.startTime + deltaMs) };
                }
            });
            return newBlocks;
        });
    };

    const handleSelectBlock = (id: string, multi: boolean) => {
        setSelectedBlockIds(prev => {
            if (multi) {
                const next = new Set(prev);
                if (next.has(id)) next.delete(id);
                else next.add(id);
                return next;
            } else {
                return new Set([id]);
            }
        });
    };

    const handleRemoveTrack = (trackKey: number) => {
        setBlocks(prev => {
            const filtered = prev.filter(b => b.key !== trackKey);
            return filtered;
        });
        setSelectedBlockIds(prev => {
            const next = new Set(prev);
            blocks.filter(b => b.key === trackKey).forEach(b => next.delete(b.id));
            return next;
        });
    };

    const totalDuration = useMemo(() => {
        let max = 1000;
        for (const b of blocks) {
            if (b.startTime + b.duration > max) {
                max = b.startTime + b.duration;
            }
        }
        return max + 500;
    }, [blocks]);

    const tracks = useMemo(() => {
        const map = new Map<number, TimelineTrack>();
        for (const block of blocks) {
            if (!map.has(block.key)) {
                map.set(block.key, { id: `trk-${block.key}`, key: block.key, blocks: [] });
            }
            map.get(block.key)!.blocks.push(block);
        }
        return Array.from(map.values()).sort((a, b) => a.key - b.key);
    }, [blocks]);

    // Marquee selection logic
    const handleContainerPointerDown = (e: React.PointerEvent) => {
        if (e.target !== containerInnerRef.current && e.target !== scrollContainerRef.current) return;
        if (e.button !== 0) return; // Only left click

        e.preventDefault();
        if (!e.shiftKey && !e.ctrlKey && !e.metaKey) {
            setSelectedBlockIds(new Set());
        }

        const rect = containerInnerRef.current!.getBoundingClientRect();
        const startX = e.clientX - rect.left;
        const startY = e.clientY - rect.top;

        setIsSelecting(true);
        setSelectionBox({ startX, startY, endX: startX, endY: startY });

        const onPointerMove = (moveEvent: PointerEvent) => {
            const moveRect = containerInnerRef.current!.getBoundingClientRect();
            setSelectionBox(prev => prev ? { ...prev, endX: moveEvent.clientX - moveRect.left, endY: moveEvent.clientY - moveRect.top } : null);
        };

        const onPointerUp = (upEvent: PointerEvent) => {
            setIsSelecting(false);
            setSelectionBox(prev => {
                if (prev) {
                    const minX = Math.min(prev.startX, prev.endX);
                    const maxX = Math.max(prev.startX, prev.endX);
                    const minY = Math.min(prev.startY, prev.endY);
                    const maxY = Math.max(prev.startY, prev.endY);

                    // Check which blocks intersect with the marquee box
                    const newSelection = new Set(selectedBlockIds);
                    tracks.forEach((track, tIdx) => {
                        const trackTop = 28 + tIdx * 36; // 28 is ruler height, 36 is track height
                        const trackBottom = trackTop + 36;
                        
                        if (trackBottom > minY && trackTop < maxY) {
                            track.blocks.forEach(block => {
                                const blockLeft = block.startTime * zoom;
                                const blockRight = blockLeft + Math.max(block.duration * zoom, 8);
                                
                                if (blockRight > minX && blockLeft < maxX) {
                                    newSelection.add(block.id);
                                }
                            });
                        }
                    });
                    
                    if (!upEvent.shiftKey && !upEvent.ctrlKey && !upEvent.metaKey && newSelection.size === selectedBlockIds.size) {
                         // No new selection and no modifier, clear
                    } else {
                         setSelectedBlockIds(newSelection);
                    }
                }
                return null;
            });

            window.removeEventListener('pointermove', onPointerMove);
            window.removeEventListener('pointerup', onPointerUp);
        };

        window.addEventListener('pointermove', onPointerMove);
        window.addEventListener('pointerup', onPointerUp);
    };


    // Handle ctrl+wheel for zoom
    useEffect(() => {
        const el = scrollContainerRef.current;
        if (!el) return;

        const handleNativeWheel = (e: WheelEvent) => {
            if (e.ctrlKey) {
                e.preventDefault();
                const rect = el.getBoundingClientRect();
                const mouseX = e.clientX - rect.left;
                
                setZoom(prev => {
                    const zoomFactor = e.deltaY > 0 ? 0.9 : 1.1;
                    const newZoom = Math.max(0.05, Math.min(prev * zoomFactor, 10));
                    
                    const mouseTime = (el.scrollLeft + mouseX) / prev;
                    
                    requestAnimationFrame(() => {
                        if (scrollContainerRef.current) {
                            scrollContainerRef.current.scrollLeft = mouseTime * newZoom - mouseX;
                        }
                    });
                    
                    return newZoom;
                });
            }
        };

        el.addEventListener('wheel', handleNativeWheel, { passive: false });
        return () => el.removeEventListener('wheel', handleNativeWheel);
    }, []);

    // Playhead animation during recording
    useEffect(() => {
        let animationFrameId: number;
        
        const updatePlayhead = () => {
            if (recordingStateRef.current.isRecording) {
                const now = Date.now();
                const deltaMs = now - recordingStateRef.current.startTimeAbsolute;
                const current = recordDelay ? recordingStateRef.current.lastEventTime + deltaMs : recordingStateRef.current.lastEventTime;
                
                const currentPx = current * zoom;
                
                if (playheadRef.current) {
                    playheadRef.current.style.left = `${currentPx}px`;
                }

                recordingStateRef.current.activeKeys.forEach((val) => {
                    const blockEl = document.getElementById(val.id);
                    if (blockEl) {
                        const durationMs = current - val.startTime;
                        blockEl.style.width = `${Math.max(durationMs * zoom, 8)}px`;
                    }
                });

                if (containerInnerRef.current) {
                    const currentMinWidth = parseFloat(containerInnerRef.current.style.minWidth || '0');
                    if (currentPx + 500 > currentMinWidth) {
                        containerInnerRef.current.style.minWidth = `${currentPx + 500}px`;
                    }
                }

                const el = scrollContainerRef.current;
                if (el) {
                    const rect = el.getBoundingClientRect();
                    const relativeX = currentPx - el.scrollLeft;
                    if (relativeX > rect.width * 0.8) {
                        el.scrollLeft = currentPx - rect.width * 0.8;
                    } else if (relativeX < rect.width * 0.1 && el.scrollLeft > 0) {
                        el.scrollLeft = Math.max(0, currentPx - rect.width * 0.1);
                    }
                }
                
                animationFrameId = requestAnimationFrame(updatePlayhead);
            }
        };

        if (isRecording) {
            if (playheadRef.current) playheadRef.current.style.display = 'block';
            animationFrameId = requestAnimationFrame(updatePlayhead);
        } else {
            if (playheadRef.current) playheadRef.current.style.display = 'none';
        }

        return () => {
            if (animationFrameId) cancelAnimationFrame(animationFrameId);
        };
    }, [isRecording, zoom, recordDelay]);

    // Keyboard Recording Logic
    const recordingStateRef = useRef({
        isRecording,
        lastEventTime: 0,
        startTimeAbsolute: 0,
        hasRecordedFirstEvent: false,
        activeKeys: new Map<string, { id: string, startTime: number }>(),
        nextBlockId: Date.now()
    });

    useEffect(() => {
        if (isRecording !== recordingStateRef.current.isRecording) {
            recordingStateRef.current.isRecording = isRecording;
            if (isRecording) {
                recordingStateRef.current.startTimeAbsolute = Date.now();
                recordingStateRef.current.hasRecordedFirstEvent = false;
                
                let maxTime = 0;
                for (const b of blocks) {
                    if (b.startTime + b.duration > maxTime) maxTime = b.startTime + b.duration;
                }
                recordingStateRef.current.lastEventTime = maxTime;
                recordingStateRef.current.activeKeys.clear();
            } else {
                const now = Date.now();
                const deltaMs = now - recordingStateRef.current.startTimeAbsolute;
                const currentTime = recordDelay ? recordingStateRef.current.lastEventTime + deltaMs : recordingStateRef.current.lastEventTime;
                
                if (recordingStateRef.current.activeKeys.size > 0) {
                    setBlocks(prev => {
                        let newBlocks = [...prev];
                        recordingStateRef.current.activeKeys.forEach((val) => {
                            newBlocks = newBlocks.map(b => 
                                b.id === val.id 
                                    ? { ...b, duration: currentTime - b.startTime, type: 'hold' } 
                                    : b
                            );
                        });
                        return newBlocks;
                    });
                    recordingStateRef.current.activeKeys.clear();
                }
            }
        }
    }, [isRecording, blocks, recordDelay]);

    useEffect(() => {
        const handleKeyDown = (e: KeyboardEvent) => {
            if (!recordingStateRef.current.isRecording) return;
            const hidCode = BROWSER_CODE_TO_HID[e.code];
            if (hidCode !== undefined) {
                e.preventDefault();
                e.stopPropagation();
                if (recordingStateRef.current.activeKeys.has(e.code)) return;

                const now = Date.now();
                let deltaMs = now - recordingStateRef.current.startTimeAbsolute;
                
                // If the macro is empty and this is the very first keystroke, don't record the leading delay
                if (!recordingStateRef.current.hasRecordedFirstEvent && recordingStateRef.current.lastEventTime === 0) {
                    recordingStateRef.current.startTimeAbsolute = now;
                    deltaMs = 0;
                }
                recordingStateRef.current.hasRecordedFirstEvent = true;

                const startTime = recordDelay ? recordingStateRef.current.lastEventTime + deltaMs : recordingStateRef.current.lastEventTime;
                
                const blockId = `blk-${nextBlockIdRef.current++}`;
                recordingStateRef.current.activeKeys.set(e.code, { id: blockId, startTime });
                
                setBlocks(prev => [
                    ...prev,
                    {
                        id: blockId,
                        trackId: `trk-${hidCode}`,
                        key: hidCode,
                        startTime,
                        duration: 0,
                        type: 'hold'
                    }
                ]);
            }
        };

        const handleKeyUp = (e: KeyboardEvent) => {
            if (!recordingStateRef.current.isRecording) return;
            const hidCode = BROWSER_CODE_TO_HID[e.code];
            if (hidCode !== undefined) {
                e.preventDefault();
                e.stopPropagation();
                
                const pressInfo = recordingStateRef.current.activeKeys.get(e.code);
                if (pressInfo) {
                    recordingStateRef.current.activeKeys.delete(e.code);
                    const now = Date.now();
                    const deltaMs = now - recordingStateRef.current.startTimeAbsolute;
                    const endTime = recordDelay ? recordingStateRef.current.lastEventTime + deltaMs : recordingStateRef.current.lastEventTime;
                    const duration = endTime - pressInfo.startTime;
                    
                    setBlocks(prev => prev.map(b => 
                        b.id === pressInfo.id 
                            ? { ...b, duration: Math.max(10, duration), type: duration < 50 ? 'tap' : 'hold' } 
                            : b
                    ));
                    
                    if (!recordDelay) {
                         recordingStateRef.current.lastEventTime = pressInfo.startTime + Math.max(10, duration);
                    }
                }
            }
        };

        if (isRecording && isActiveView) {
            window.addEventListener('keydown', handleKeyDown, { capture: true });
            window.addEventListener('keyup', handleKeyUp, { capture: true });
        }
        return () => {
            window.removeEventListener('keydown', handleKeyDown, { capture: true });
            window.removeEventListener('keyup', handleKeyUp, { capture: true });
        };
    }, [isRecording, recordDelay, isActiveView]);

    return (
        <div className="macro-timeline-editor">
            {/* Toolbar */}
            <div className="timeline-toolbar" style={{ display: 'flex', alignItems: 'center', width: '100%' }}>
                <div style={{ color: '#888', fontSize: '12px' }}>
                    {blocks.length} events {maxEvents ? `/ ${maxEvents}` : ''}
                </div>
                {selectedBlockIds.size > 0 && (
                    <button 
                        className="btn-icon btn-danger" 
                        style={{ marginLeft: 'auto', padding: '4px', height: 'auto', width: 'auto' }}
                        onClick={() => {
                            setBlocks(prev => prev.filter(b => !selectedBlockIds.has(b.id)));
                            setSelectedBlockIds(new Set());
                        }}
                        title="Delete Selected"
                    >
                        <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                            <polyline points="3 6 5 6 21 6"></polyline>
                            <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"></path>
                        </svg>
                    </button>
                )}
            </div>

            {/* Canvas Area */}
            <div style={{ display: 'flex', flexGrow: 1, overflow: 'hidden' }}>
                {/* Track Headers */}
                <div style={{ width: '150px', flexShrink: 0, backgroundColor: 'var(--bg-panel, #2a2a35)', zIndex: 12, borderRight: '1px solid var(--border-color, #333)', display: 'flex', flexDirection: 'column' }}>
                    <div style={{ height: '28px', flexShrink: 0, borderBottom: '1px solid var(--border-color, #333)' }}></div>
                    {tracks.length === 0 && <div style={{ padding: '12px', color: '#64748b', fontSize: '12px', fontStyle: 'italic', textAlign: 'center' }}>No actions recorded</div>}
                    {tracks.map(track => (
                        <div key={track.id} className="timeline-track-header">
                            <span style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                                {getKeyName(track.key, macros)}
                            </span>
                            <button 
                                onClick={() => handleRemoveTrack(track.key)}
                                style={{ background: 'none', border: 'none', color: '#64748b', cursor: 'pointer', padding: '4px' }}
                                title="Remove Track"
                            >
                                <svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><line x1="18" y1="6" x2="6" y2="18"></line><line x1="6" y1="6" x2="18" y2="18"></line></svg>
                            </button>
                        </div>
                    ))}
                </div>

                {/* Scrollable Timeline */}
                <div 
                    ref={scrollContainerRef}
                    style={{ flexGrow: 1, overflowX: 'auto', overflowY: 'auto', position: 'relative' }}
                    onPointerDown={handleContainerPointerDown}
                >
                    <div ref={containerInnerRef} style={{ minWidth: `${totalDuration * zoom}px`, position: 'relative', minHeight: '100%', cursor: isSelecting ? 'crosshair' : 'default' }}>
                        <TimelineRuler pxPerMs={zoom} totalMs={totalDuration} />
                        
                        <div style={{ position: 'absolute', top: '28px', left: 0, right: 0, bottom: 0 }}>
                            {tracks.map(track => (
                                <TimelineTrackComponent 
                                    key={track.id}
                                    track={track}
                                    pxPerMs={zoom}
                                    onChangeBlock={handleBlockChange}
                                    onMoveBlocks={handleMoveBlocks}
                                    selectedBlockIds={selectedBlockIds}
                                    onSelectBlock={handleSelectBlock}
                                />
                            ))}
                        </div>

                        {/* Marquee Selection Box */}
                        {isSelecting && selectionBox && (
                            <div className="timeline-marquee" style={{
                                left: Math.min(selectionBox.startX, selectionBox.endX),
                                top: Math.min(selectionBox.startY, selectionBox.endY),
                                width: Math.abs(selectionBox.endX - selectionBox.startX),
                                height: Math.abs(selectionBox.endY - selectionBox.startY)
                            }} />
                        )}

                        {/* Playhead */}
                        <div ref={playheadRef} className="timeline-playhead">
                            <div className="timeline-playhead-top" />
                        </div>
                    </div>
                </div>
            </div>
        </div>
    );
});

export default React.memo(MacroTimelineEditor);
