import type { PhysKey } from '../types/device';

export const PREDEFINED_LAYOUTS: Record<string, PhysKey[][]> = {
    'stag-65p': [
        // Row 0: ESC  1  2  3  4  5  6  7  8  9  0  -  =  BKSP(2u)  DEL
        [{ row: 0, col: 0, w: 1, h: 1, x: 0, y: 0 }, { row: 0, col: 1, w: 1, h: 1, x: 1, y: 0 }, { row: 0, col: 2, w: 1, h: 1, x: 2, y: 0 }, { row: 0, col: 3, w: 1, h: 1, x: 3, y: 0 }, { row: 0, col: 4, w: 1, h: 1, x: 4, y: 0 }, { row: 0, col: 5, w: 1, h: 1, x: 5, y: 0 }, { row: 0, col: 6, w: 1, h: 1, x: 6, y: 0 }, { row: 0, col: 7, w: 1, h: 1, x: 7, y: 0 }, { row: 0, col: 8, w: 1, h: 1, x: 8, y: 0 }, { row: 0, col: 9, w: 1, h: 1, x: 9, y: 0 }, { row: 0, col: 10, w: 1, h: 1, x: 10, y: 0 }, { row: 0, col: 11, w: 1, h: 1, x: 11, y: 0 }, { row: 0, col: 12, w: 1, h: 1, x: 12, y: 0 }, { row: 0, col: 13, w: 2, h: 1, x: 13, y: 0 }, { row: 0, col: 14, w: 1, h: 1, x: 15, y: 0 }],
        // Row 1: TAB(1.5u)  Q W E R T Y U I O P [ ] \(1.5u) HOME
        [{ row: 1, col: 0, w: 1.5, h: 1, x: 0, y: 1 }, { row: 1, col: 1, w: 1, h: 1, x: 1.5, y: 1 }, { row: 1, col: 2, w: 1, h: 1, x: 2.5, y: 1 }, { row: 1, col: 3, w: 1, h: 1, x: 3.5, y: 1 }, { row: 1, col: 4, w: 1, h: 1, x: 4.5, y: 1 }, { row: 1, col: 5, w: 1, h: 1, x: 5.5, y: 1 }, { row: 1, col: 6, w: 1, h: 1, x: 6.5, y: 1 }, { row: 1, col: 7, w: 1, h: 1, x: 7.5, y: 1 }, { row: 1, col: 8, w: 1, h: 1, x: 8.5, y: 1 }, { row: 1, col: 9, w: 1, h: 1, x: 9.5, y: 1 }, { row: 1, col: 10, w: 1, h: 1, x: 10.5, y: 1 }, { row: 1, col: 11, w: 1, h: 1, x: 11.5, y: 1 }, { row: 1, col: 12, w: 1, h: 1, x: 12.5, y: 1 }, { row: 1, col: 13, w: 1.5, h: 1, x: 13.5, y: 1 }, { row: 1, col: 14, w: 1, h: 1, x: 15, y: 1 }],
        // Row 2: CAPS(1.75u) A S D F G H J K L ; ' ENTER(2.25u) PGUP
        [{ row: 2, col: 0, w: 1.75, h: 1, x: 0, y: 2 }, { row: 2, col: 1, w: 1, h: 1, x: 1.75, y: 2 }, { row: 2, col: 2, w: 1, h: 1, x: 2.75, y: 2 }, { row: 2, col: 3, w: 1, h: 1, x: 3.75, y: 2 }, { row: 2, col: 4, w: 1, h: 1, x: 4.75, y: 2 }, { row: 2, col: 5, w: 1, h: 1, x: 5.75, y: 2 }, { row: 2, col: 6, w: 1, h: 1, x: 6.75, y: 2 }, { row: 2, col: 7, w: 1, h: 1, x: 7.75, y: 2 }, { row: 2, col: 8, w: 1, h: 1, x: 8.75, y: 2 }, { row: 2, col: 9, w: 1, h: 1, x: 9.75, y: 2 }, { row: 2, col: 10, w: 1, h: 1, x: 10.75, y: 2 }, { row: 2, col: 11, w: 1, h: 1, x: 11.75, y: 2 }, { row: 2, col: 12, w: 2.25, h: 1, x: 12.75, y: 2 }, { row: 2, col: 14, w: 1, h: 1, x: 15, y: 2 }],
        // Row 3: LSHIFT(2.25u) Z X C V B N M , . / RSHIFT(1.75u) ↑ PGDN
        [{ row: 3, col: 0, w: 2.25, h: 1, x: 0, y: 3 }, { row: 3, col: 2, w: 1, h: 1, x: 2.25, y: 3 }, { row: 3, col: 3, w: 1, h: 1, x: 3.25, y: 3 }, { row: 3, col: 4, w: 1, h: 1, x: 4.25, y: 3 }, { row: 3, col: 5, w: 1, h: 1, x: 5.25, y: 3 }, { row: 3, col: 6, w: 1, h: 1, x: 6.25, y: 3 }, { row: 3, col: 7, w: 1, h: 1, x: 7.25, y: 3 }, { row: 3, col: 8, w: 1, h: 1, x: 8.25, y: 3 }, { row: 3, col: 9, w: 1, h: 1, x: 9.25, y: 3 }, { row: 3, col: 10, w: 1, h: 1, x: 10.25, y: 3 }, { row: 3, col: 11, w: 1, h: 1, x: 11.25, y: 3 }, { row: 3, col: 12, w: 1.75, h: 1, x: 12.25, y: 3 }, { row: 3, col: 13, w: 1, h: 1, x: 14, y: 3 }, { row: 3, col: 14, w: 1, h: 1, x: 15, y: 3 }],
        // Row 4: LCTRL(1.25u) LGUI(1.25u) LALT(1.25u) SPACE(6.25u) RALT FN1 FN2 ← ↓ →
        [{ row: 4, col: 0, w: 1.25, h: 1, x: 0, y: 4 }, { row: 4, col: 1, w: 1.25, h: 1, x: 1.25, y: 4 }, { row: 4, col: 2, w: 1.25, h: 1, x: 2.5, y: 4 }, { row: 4, col: 5, w: 6.25, h: 1, x: 3.75, y: 4 }, { row: 4, col: 9, w: 1, h: 1, x: 10, y: 4 }, { row: 4, col: 10, w: 1, h: 1, x: 11, y: 4 }, { row: 4, col: 11, w: 1, h: 1, x: 12, y: 4 }, { row: 4, col: 12, w: 1, h: 1, x: 13, y: 4 }, { row: 4, col: 13, w: 1, h: 1, x: 14, y: 4 }, { row: 4, col: 14, w: 1, h: 1, x: 15, y: 4 }],
        // Row 5: (Optional/Thumb keys)
        []
    ],
    'split-lily58': [],
    'ortho-65p': [],
    'stag-75p': [],
    'stag-tkl': [],
    'stag-100p': []
};
