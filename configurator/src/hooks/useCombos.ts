import { useState, useCallback, useEffect } from 'react';
import { hidService } from '../HIDService';
import type { Combo, ComboLimits } from '../types/combos';
import { withTimeout } from '../utils/withTimeout';

type ConfirmFn = (title: string, description: string) => Promise<boolean>;

export function useCombos(
    isConnected: boolean,
    addLog: (text: string) => void,
    confirm: ConfirmFn,
) {
    const [combos, setCombos] = useState<Combo[]>([]);
    const [comboLimits, setComboLimits] = useState<ComboLimits | null>(null);

    useEffect(() => {
        if (!isConnected) {
            setCombos([]);
            setComboLimits(null);
        }
    }, [isConnected]);

    const fetchComboLimits = useCallback(async () => {
        if (!isConnected) return;
        try {
            const limits = await hidService.fetchComboLimits();
            if (limits) setComboLimits(limits);
        } catch (e) {
            console.error('[useCombos] Failed to fetch combo limits:', e);
        }
    }, [isConnected]);

    const fetchSingleCombo = useCallback(async (id: number): Promise<Combo | null> => {
        if (!isConnected) return null;
        try {
            const detail = await hidService.fetchSingleCombo(id);
            if (detail) {
                setCombos(prev => {
                    const newList = prev.map(k => k.id === id ? detail : k);
                    return [...newList].sort((a, b) => a.id - b.id);
                });
                return detail;
            }
        } catch (e) {
            console.error(`[useCombos] Failed to fetch combo ${id}:`, e);
        }
        return null;
    }, [isConnected]);

    const fetchCombos = useCallback(async () => {
        if (!isConnected) return;
        try {
            const ids = await hidService.fetchCombos();
            
            const skeletons = ids.map(id => ({ 
                id, name: `Loading... (ID ${id})`, action: 0, delayMs: 0, 
                activeLayers: [], strictOrder: false, cancelKeys: false, 
                delayedPress: false, releaseOnFirstKey: false, keys: [] 
            } as Combo));
            setCombos(skeletons);

            addLog(`Found ${ids.length} combos. Fetching details...`);

            for (const id of ids) {
                await fetchSingleCombo(id);
            }
            addLog(`All combo details loaded.`);
        } catch (e) {
            console.error('[useCombos] Failed to fetch combos:', e);
        }
    }, [isConnected, fetchSingleCombo, addLog]);

    const saveCombo = async (combo: Combo): Promise<void> => {
        let comboToSave = combo;
        if (combo.id === -1) {
            const usedIds = new Set(combos.map(k => k.id));
            let nextId = 0;
            while (usedIds.has(nextId)) nextId++;
            const maxCombos = comboLimits?.maxCombos ?? 32;
            if (nextId >= maxCombos) throw new Error('Maximum number of combos reached.');
            comboToSave = { ...combo, id: nextId };
        }
        const ok = await withTimeout(hidService.saveCombo(comboToSave), 7000);
        if (!ok) throw new Error('Failed to save combo to device');
        setCombos(prev => {
            const filtered = prev.filter(k => k.id !== comboToSave.id);
            return [...filtered, comboToSave].sort((a, b) => a.id - b.id);
        });
    };

    const deleteCombo = async (id: number): Promise<void> => {
        const isConfirmed = await confirm(
            'Delete Combo',
            'Are you sure you want to delete this combo?'
        );
        if (!isConfirmed) return;
        const ok = await withTimeout(hidService.deleteCombo(id), 7000);
        if (!ok) throw new Error('Failed to delete combo from device');
        setCombos(prev => prev.filter(k => k.id !== id));
    };

    return {
        combos,
        comboLimits,
        fetchCombos,
        fetchComboLimits,
        saveCombo,
        deleteCombo,
    };
}
