import type { TimelineTrack as TrackType } from '../../types/timeline';
import TimelineBlockComponent from './TimelineBlock';


interface TimelineTrackProps {
    track: TrackType;
    pxPerMs: number;
    onChangeBlock: (blockId: string, newStartTime: number, newDuration: number) => void;
    onMoveBlocks: (blockIds: string[], deltaMs: number) => void;
    selectedBlockIds: Set<string>;
    onSelectBlock: (id: string, multi: boolean) => void;
}

export default function TimelineTrackComponent({ 
    track, 
    pxPerMs, 
    onChangeBlock,
    onMoveBlocks,
    selectedBlockIds,
    onSelectBlock
}: TimelineTrackProps) {
    return (
        <div className="timeline-track-row">
            {track.blocks.map(block => (
                <TimelineBlockComponent 
                    key={block.id} 
                    block={block} 
                    pxPerMs={pxPerMs} 
                    onChange={onChangeBlock}
                    onMove={onMoveBlocks}
                    onSelect={onSelectBlock}
                    isSelected={selectedBlockIds.has(block.id)}
                    selectedIds={selectedBlockIds}
                />
            ))}
        </div>
    );
}
