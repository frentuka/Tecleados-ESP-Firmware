import { create } from 'zustand';

interface UiState {
    render3DModel: boolean;
    setRender3DModel: (val: boolean) => void;
}

export const useUiStore = create<UiState>((set) => {
    const saved = localStorage.getItem('render3DModel');
    const initial = saved !== null ? saved === 'true' : true;
    return {
        render3DModel: initial,
        setRender3DModel: (val) => {
            localStorage.setItem('render3DModel', val.toString());
            set({ render3DModel: val });
        },
    };
});
