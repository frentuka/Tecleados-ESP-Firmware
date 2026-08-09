import { useState, useRef, useCallback, useEffect } from 'react';
import { hidService } from '../HIDService';
import type { Macro } from '../types/macros';

type ConfirmFn = (title: string, description: string) => Promise<boolean>;

/**
 * Manages all macro state and device operations.
 *
 * @param isConnected - Current device connection state.
 * @param addLog      - Callback to append a text entry to the global log.
 * @param confirm     - Async confirm dialog from useConfirm.
 */
export function useMacros(
    isConnected: boolean,
    addLog: (text: string) => void,
    confirm: ConfirmFn,
) {
    const [macros, setMacros] = useState<Macro[]>([]);
    const [macroLimits, setMacroLimits] = useState<{ maxEvents: number; maxMacros: number } | null>(null);
    // macrosRef mirrors macros state synchronously so async callbacks always see the latest list.
    const macrosRef = useRef<Macro[]>([]);
    const macroCache = useRef<Record<number, Macro>>({});

    // Keep macrosRef in sync whenever the state updates from outside (e.g. onReload).
    // Note: useMacros callers should spread the returned macros into their state or
    // rely on the setters here; this ref exists solely for microtask-safe reads.
    const syncMacros = (list: Macro[]) => {
        macrosRef.current = list;
        setMacros(list);
    };

    // Reset state when disconnected
    useEffect(() => {
        if (!isConnected) {
            syncMacros([]);
            setMacroLimits(null);
            macroCache.current = {};
        }
    }, [isConnected]);

    const fetchMacroLimits = useCallback(async () => {
        if (!isConnected) return;
        try {
            const limits = await hidService.fetchMacroLimits();
            if (limits) {
                setMacroLimits(limits);
                console.log(`Macro limits: maxEvents=${limits.maxEvents}, maxMacros=${limits.maxMacros}`);
            }
        } catch (e) {
            console.error('fetchMacroLimits Error:', e);
        }
    }, [isConnected]);

    const fetchSingleMacro = useCallback(async (id: number): Promise<Macro | null> => {
        if (!isConnected) return null;
        if (macroCache.current[id]) {
            const cached = macroCache.current[id];
            const newList = macrosRef.current.map(m => m.id === id ? cached : m);
            syncMacros(newList);
            return cached;
        }

        try {
            addLog(`Fetching details for macro ID ${id}...`);
            const parsed = await hidService.fetchSingleMacro(id);
            if (parsed) {
                macroCache.current[id] = parsed; // Cache it
                // Update state to hydrate UI elements
                const newList = macrosRef.current.map(m => m.id === id ? parsed : m);
                syncMacros(newList); // Synchronous update for microtask safety

                addLog(`Details for macro "${parsed.name}" loaded.`);
                return parsed;
            }
        } catch (e) {
            console.error('Single Macro Fetch Error:', e);
        }
        return null;
    }, [isConnected, addLog]);

    const fetchMacros = useCallback(async () => {
        if (!isConnected) return;

        try {
            const ids = await hidService.fetchMacroOutline();
            addLog(`Found ${ids.length} macros on device`);
            
            // Build temporary skeleton macros for the UI while they fetch
            const skeletons = ids.map(id => ({ id, name: `Loading... (ID ${id})`, execMode: 0, stackMax: 0, repeatCount: 0, elements: [] } as Macro));
            syncMacros(skeletons);
            macroCache.current = {};

            addLog(`Initialized ${ids.length} macros. Fetching details...`);
            for (const id of ids) {
                let retries = 3;
                let success = false;
                while (retries > 0 && !success) {
                    const result = await fetchSingleMacro(id);
                    if (result) {
                        success = true;
                    } else {
                        retries--;
                        if (retries > 0) {
                            console.warn(`[useMacros] Macro ID ${id} fetch failed, retrying... (${retries} left)`);
                            await new Promise(r => setTimeout(r, 1000));
                        } else {
                            console.error(`[useMacros] Macro ID ${id} failed to fetch after multiple attempts.`);
                        }
                    }
                }
            }
        } catch (e) {
            console.error('Macros Fetch Error:', e);
        }
    }, [isConnected, fetchSingleMacro, addLog]);

    const saveMacro = async (newMacro: Macro): Promise<void> => {
        let macroToSave = newMacro;
        let isNew = false;
        const maxAllowedId = macroLimits ? macroLimits.maxMacros - 1 : 31;

        if (newMacro.id === -1) {
            const existingIds = new Set(macrosRef.current.map(m => m.id));
            let nextId = 0;
            while (existingIds.has(nextId)) nextId++;
            if (nextId > maxAllowedId) {
                throw new Error(`Maximum number of macros reached. Max allowed is ${maxAllowedId + 1}.`);
            }
            macroToSave = { ...newMacro, id: nextId };
            isNew = true;
            syncMacros([...macrosRef.current, macroToSave]);
        } else {
            if (macroToSave.id > maxAllowedId) {
                throw new Error(`Macro ID ${macroToSave.id} exceeds maximum allowed ID of ${maxAllowedId}.`);
            }
        }

        try {
            const success = await hidService.saveMacro(macroToSave);
            if (success) {
                const newList = macrosRef.current.map(m => m.id === macroToSave.id ? macroToSave : m);
                const deduplicated = Array.from(new Map(newList.map(m => [m.id, m])).values());
                syncMacros(deduplicated);
                macroCache.current[macroToSave.id] = macroToSave;
                addLog(`Macro "${macroToSave.name}" saved to device (ID: ${macroToSave.id})`);
            } else {
                if (isNew) {
                    syncMacros(macrosRef.current.filter(m => m.id !== macroToSave.id));
                }
                addLog(`Failed to save macro.`);
                throw new Error('Device error or timeout');
            }
        } catch (e) {
            if (isNew) {
                syncMacros(macrosRef.current.filter(m => m.id !== macroToSave.id));
            }
            throw e;
        }
    };

    const deleteMacro = async (id: number): Promise<void> => {
        const isConfirmed = await confirm(
            'Delete Macro',
            'Are you sure you want to delete this macro? Any keys mapped to it will stop working.'
        );
        if (!isConfirmed) return;

        const success = await hidService.deleteMacro(id);
        if (success) {
            syncMacros(macrosRef.current.filter(m => m.id !== id));
            delete macroCache.current[id];
            addLog(`Macro deleted. ${macrosRef.current.length} remaining.`);
        } else {
            const errMsg = 'Failed to delete macro (device error or timeout)';
            addLog(errMsg);
            throw new Error(errMsg);
        }
    };

    return {
        macros,
        macroLimits,
        fetchMacroLimits,
        fetchSingleMacro,
        fetchMacros,
        saveMacro,
        deleteMacro,
    };
}
