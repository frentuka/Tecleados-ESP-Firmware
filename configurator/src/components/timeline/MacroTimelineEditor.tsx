import React, { useState, useEffect, useRef, useMemo, useImperativeHandle, forwardRef } from 'react';
import type { MacroElement, Macro } from '../../types/macros';
import type { TimelineBlock, TimelineTrack } from '../../types/timeline';
import { elementsToTimeline, timelineToElements } from '../../utils/macroTimelineAdapter';
import TimelineTrackComponent from './TimelineTrack';
import TimelineRuler from './TimelineRuler';
import { BROWSER_CODE_TO_HID, getKeyName } from '../../KeyDefinitions';

export interface MacroTimelineRef {
    addKeyBlock: (key: number) => void;
    addSleepBlock: (duration: number) => void;
    clearAll: () => void;
}

interface MacroTimelineEditorProps {
    initialElements: MacroElement[];
    macros: Macro[];
    onChange: (elements: MacroElement[]) => void;
    isRecording: boolean;
    recordDelay: boolean;
    maxEvents?: number;
}

const MacroTimelineEditor = forwardRef<MacroTimelineRef, MacroTimelineEditorProps>(({ 
    initialElements, 
    macros, 
    onChange, 
    isRecording,
    recordDelay,
    maxEvents
}, ref) => {
    const [blocks, setBlocks] = useState<TimelineBlock[]>(() => elementsToTimeline(initialElements));
    const [zoom, setZoom] = useState(1); // 1 = 1px per ms
    const [selectedBlockId, setSelectedBlockId] = useState<string | undefined>();
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
        addSleepBlock: () => {
            // Sleep blocks are not visible in the absolute timeline (they just create gaps).
            // But dragging existing blocks effectively creates sleep.
            // If the user presses "Add Delay", we don't have a specific track to put it on.
            // Timeline paradigm doesn't use explicit sleep blocks.
            // We can just shift the `maxTime` cursor for the NEXT added key.
            // For now, we do nothing visually, because gaps are implicit.
        },
        clearAll: () => {
            setBlocks([]);
        }
    }));

    // Sync back to parent whenever blocks change
    useEffect(() => {
        const elements = timelineToElements(blocks);
        onChange(elements);
    }, [blocks]); // Remove onChange from dependency to prevent loop if parent regenerates function without useCallback

    // Handle incoming block updates
    const handleBlockChange = (id: string, newStartTime: number, newDuration: number) => {
        setBlocks(prev => prev.map(b => 
            b.id === id ? { ...b, startTime: newStartTime, duration: newDuration } : b
        ));
    };

    const handleRemoveTrack = (trackKey: number) => {
        setBlocks(prev => prev.filter(b => b.key !== trackKey));
        setSelectedBlockId(undefined);
    };

    // Calculate total duration for ruler
    const totalDuration = useMemo(() => {
        let max = 1000; // Minimum 1 second width
        for (const b of blocks) {
            if (b.startTime + b.duration > max) {
                max = b.startTime + b.duration;
            }
        }
        return max + 500; // Add 500ms padding
    }, [blocks]);

    // Group blocks into tracks
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

    // Handle ctrl+wheel for zoom
    useEffect(() => {
        const el = scrollContainerRef.current;
        if (!el) return;

        const handleNativeWheel = (e: WheelEvent) => {
            if (e.ctrlKey) {
                e.preventDefault(); // Prevent browser zoom
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

                // Update width of actively recorded blocks
                recordingStateRef.current.activeKeys.forEach((val) => {
                    const blockEl = document.getElementById(val.id);
                    if (blockEl) {
                        const durationMs = current - val.startTime;
                        blockEl.style.width = `${Math.max(durationMs * zoom, 8)}px`;
                    }
                });

                // Auto-expand container if playhead goes past current width
                if (containerInnerRef.current) {
                    const currentMinWidth = parseFloat(containerInnerRef.current.style.minWidth || '0');
                    if (currentPx + 500 > currentMinWidth) {
                        containerInnerRef.current.style.minWidth = `${currentPx + 500}px`;
                    }
                }

                // Auto-scroll timeline to keep playhead in view
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
        activeKeys: new Map<string, { id: string, startTime: number }>(),
        nextBlockId: Date.now()
    });

    useEffect(() => {
        if (isRecording !== recordingStateRef.current.isRecording) {
            recordingStateRef.current.isRecording = isRecording;
            if (isRecording) {
                recordingStateRef.current.startTimeAbsolute = Date.now();
                
                // Set the current timeline "cursor" to the end of the existing blocks
                let maxTime = 0;
                for (const b of blocks) {
                    if (b.startTime + b.duration > maxTime) maxTime = b.startTime + b.duration;
                }
                recordingStateRef.current.lastEventTime = maxTime;
                recordingStateRef.current.activeKeys.clear();
            } else {
                // When stopping, finish any pending presses as tap/hold blocks
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
                if (recordingStateRef.current.activeKeys.has(e.code)) return; // Auto-repeat ignore

                const now = Date.now();
                const deltaMs = now - recordingStateRef.current.startTimeAbsolute;
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
                        duration: 0, // Starts at 0, grows until keyup
                        type: 'hold' // Defaults to hold until release
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

        if (isRecording) {
            window.addEventListener('keydown', handleKeyDown, { capture: true });
            window.addEventListener('keyup', handleKeyUp, { capture: true });
        }
        return () => {
            window.removeEventListener('keydown', handleKeyDown, { capture: true });
            window.removeEventListener('keyup', handleKeyUp, { capture: true });
        };
    }, [isRecording, recordDelay]);

    const handleContainerClick = (e: React.MouseEvent) => {
        if (e.target === e.currentTarget) {
            setSelectedBlockId(undefined);
        }
    };

    return (
        <div className="macro-timeline-editor" style={{ display: 'flex', flexDirection: 'column', height: '100%', minHeight: '300px', backgroundColor: '#111', border: '1px solid #333', borderRadius: '4px' }}>
            {/* Toolbar */}
            <div className="timeline-toolbar" style={{ display: 'flex', padding: '8px', gap: '16px', borderBottom: '1px solid #333', backgroundColor: '#1a1a1a', alignItems: 'center' }}>
                <span style={{ color: '#aaa', fontSize: '12px' }}><i>Ctrl+Scroll to Zoom</i></span>
                <button 
                    className="btn btn-sm" 
                    onClick={() => {
                        if (selectedBlockId) {
                            setBlocks(prev => prev.filter(b => b.id !== selectedBlockId));
                            setSelectedBlockId(undefined);
                        }
                    }}
                    disabled={!selectedBlockId}
                >
                    Delete Selected
                </button>
                <div style={{ marginLeft: 'auto', color: '#888', fontSize: '12px' }}>
                    {blocks.length} events {maxEvents ? `/ ${maxEvents}` : ''}
                </div>
            </div>

            {/* Canvas Area */}
            <div 
                style={{ display: 'flex', flexGrow: 1, overflow: 'hidden' }}
                onClick={handleContainerClick}
            >
                {/* Track Headers Container - fixed width */}
                <div style={{ width: '150px', flexShrink: 0, backgroundColor: '#1a1a1a', zIndex: 2, borderRight: '1px solid #333', display: 'flex', flexDirection: 'column' }}>
                    <div style={{ height: '24px', flexShrink: 0, borderBottom: '1px solid #444' }}></div> {/* Empty corner over ruler */}
                    {tracks.length === 0 && <div style={{ padding: '8px', color: '#666', fontSize: '12px', fontStyle: 'italic' }}>No actions</div>}
                    {tracks.map(track => (
                        <div key={track.id} style={{ height: '32px', flexShrink: 0, borderBottom: '1px solid #333', display: 'flex', alignItems: 'center', padding: '0 8px', justifyContent: 'space-between', color: '#ddd', fontSize: '12px' }}>
                            <span style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                                {getKeyName(track.key, macros)}
                            </span>
                            <button 
                                onClick={() => handleRemoveTrack(track.key)}
                                style={{ background: 'none', border: 'none', color: '#888', cursor: 'pointer', padding: '0 4px' }}
                                title="Remove Track"
                            >
                                &times;
                            </button>
                        </div>
                    ))}
                </div>

                {/* Scrollable Timeline */}
                <div 
                    ref={scrollContainerRef}
                    style={{ flexGrow: 1, overflowX: 'auto', overflowY: 'auto', position: 'relative' }}
                >
                    {/* Inner wrapper to enforce width */}
                    <div ref={containerInnerRef} style={{ minWidth: `${totalDuration * zoom}px`, position: 'relative', minHeight: '100%' }}>
                        <TimelineRuler pxPerMs={zoom} totalMs={totalDuration} />
                        
                        <div style={{ position: 'absolute', top: '24px', left: 0, right: 0, bottom: 0 }}>
                            {tracks.map(track => (
                                <TimelineTrackComponent 
                                    key={track.id}
                                    track={track}
                                    pxPerMs={zoom}
                                    onChangeBlock={handleBlockChange}
                                    selectedBlockId={selectedBlockId}
                                    onSelectBlock={setSelectedBlockId}
                                />
                            ))}
                        </div>

                        {/* Playhead */}
                        <div 
                            ref={playheadRef}
                            style={{
                                display: 'none',
                                position: 'absolute',
                                top: 0,
                                bottom: 0,
                                width: '2px',
                                backgroundColor: 'red',
                                zIndex: 10,
                                pointerEvents: 'none',
                                boxShadow: '0 0 4px rgba(255,0,0,0.5)'
                            }}
                        />
                    </div>
                </div>
            </div>
        </div>
    );
});

export default MacroTimelineEditor;
