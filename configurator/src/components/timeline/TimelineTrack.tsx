import type { TimelineTrack as TrackType } from '../../types/timeline';
import TimelineBlockComponent from './TimelineBlock';


interface TimelineTrackProps {
    track: TrackType;
    pxPerMs: number;
    onChangeBlock: (blockId: string, newStartTime: number, newDuration: number) => void;
    selectedBlockId?: string;
    onSelectBlock?: (id: string) => void;
}

export default function TimelineTrackComponent({ 
    track, 
    pxPerMs, 
    onChangeBlock, 
    selectedBlockId,
    onSelectBlock
}: TimelineTrackProps) {
    return (
        <div className="timeline-track-row" style={{ position: 'relative', height: '32px', borderBottom: '1px solid #333', backgroundColor: '#0f0f0f' }}>
            {track.blocks.map(block => (
                <TimelineBlockComponent 
                    key={block.id} 
                    block={block} 
                    pxPerMs={pxPerMs} 
                    onChange={onChangeBlock}
                    onSelect={onSelectBlock}
                    isSelected={block.id === selectedBlockId}
                />
            ))}
        </div>
    );
}
