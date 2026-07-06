export type BlockType = 'tap' | 'hold' | 'press' | 'release';

export interface TimelineBlock {
    id: string;
    trackId: string;
    key: number;
    startTime: number;
    duration: number; // For tap, this is pressTime. For hold, this is the total hold duration.
    type: BlockType;
}

export interface TimelineTrack {
    id: string;
    key: number; // The primary key this track is associated with
    blocks: TimelineBlock[];
}
