interface TimelineRulerProps {
    pxPerMs: number;
    totalMs: number;
}

export default function TimelineRuler({ pxPerMs, totalMs }: TimelineRulerProps) {
    // Generate tick marks. Determine a good step interval based on zoom.
    let stepMs = 100;
    if (pxPerMs > 2) stepMs = 50;
    if (pxPerMs < 0.5) stepMs = 500;
    if (pxPerMs < 0.1) stepMs = 1000;

    const ticks = [];
    const numTicks = Math.ceil(totalMs / stepMs) + 1; // Add a bit of buffer
    
    for (let i = 0; i < numTicks; i++) {
        ticks.push(i * stepMs);
    }

    return (
        <div className="timeline-ruler" style={{ 
            height: '24px', 
            position: 'relative', 
            borderBottom: '1px solid #444', 
            backgroundColor: '#222',
            color: '#888',
            fontSize: '10px',
            overflow: 'hidden'
        }}>
            {ticks.map(tick => (
                <div key={tick} style={{
                    position: 'absolute',
                    left: `${tick * pxPerMs}px`,
                    top: 0,
                    bottom: 0,
                    borderLeft: '1px solid #444',
                    paddingLeft: '4px',
                    paddingTop: '2px'
                }}>
                    {tick}ms
                </div>
            ))}
        </div>
    );
}
