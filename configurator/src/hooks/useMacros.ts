import { useState, useRef, useCallback } from 'react';
import {
    hidService,
    MODULE_CONFIG,
    CFG_CMD_GET,
    CFG_CMD_SET,
    CFG_KEY_MACROS,
    CFG_KEY_MACRO_LIMITS,
    CFG_KEY_MACRO_SINGLE,
} from '../HIDService';
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

    const fetchMacroLimits = useCallback(async () => {
        if (!isConnected) return;

        const buf = new Uint8Array(3);
        buf[0] = MODULE_CONFIG;
        buf[1] = CFG_CMD_GET;
        buf[2] = CFG_KEY_MACRO_LIMITS;

        const resp = await hidService.sendCommand(buf);
        if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
            try {
                const parsed = JSON.parse(resp.jsonText);
                if (parsed.maxEvents && parsed.maxMacros) {
                    setMacroLimits({ maxEvents: parsed.maxEvents, maxMacros: parsed.maxMacros });
                    console.log(`Macro limits: maxEvents=${parsed.maxEvents}, maxMacros=${parsed.maxMacros}`);
                }
            } catch (e) {
                console.error('Macro Limits JSON Parse Error:', e);
            }
        }
    }, [isConnected]);

    const fetchSingleMacro = useCallback(async (id: number): Promise<Macro | null> => {
        if (!isConnected) return null;
        if (macroCache.current[id]) {
            // Hydrate from cache if we somehow have it
            const cached = macroCache.current[id];
            const newList = macrosRef.current.map(m => m.id === id ? cached : m);
            syncMacros(newList);
            return cached;
        }

        const jsonStr = JSON.stringify({ id });
        const jsonBytes = new TextEncoder().encode(jsonStr);
        const buf = new Uint8Array(3 + jsonBytes.length);
        buf[0] = MODULE_CONFIG;
        buf[1] = CFG_CMD_GET;
        buf[2] = CFG_KEY_MACRO_SINGLE;
        buf.set(jsonBytes, 3);

        const resp = await hidService.sendCommand(buf);
        if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
            try {
                addLog(`Fetching details for macro ID ${id}...`);
                const parsed = JSON.parse(resp.jsonText) as Macro;
                macroCache.current[id] = parsed; // Cache it

                // Update state to hydrate UI elements
                const newList = macrosRef.current.map(m => m.id === id ? parsed : m);
                syncMacros(newList); // Synchronous update for microtask safety

                addLog(`Details for macro "${parsed.name}" loaded.`);
                return parsed;
            } catch (e) {
                console.error('Single Macro JSON Parse Error:', e);
            }
        }
        return null;
    }, [isConnected, addLog]);

    const fetchMacros = useCallback(async () => {
        if (!isConnected) return;

        const buf = new Uint8Array(3);
        buf[0] = MODULE_CONFIG;
        buf[1] = CFG_CMD_GET;
        buf[2] = CFG_KEY_MACROS;

        const resp = await hidService.sendCommand(buf);
        if (resp && resp.status === 0 && resp.jsonText.trim().length > 0) {
            try {
                const parsed = JSON.parse(resp.jsonText);
                let list: Macro[] = [];
                if (Array.isArray(parsed)) {
                    list = parsed;
                } else if (parsed.macros && Array.isArray(parsed.macros)) {
                    list = parsed.macros;
                }
                addLog(`Found ${list.length} macros on device`);

                // Sort alphabetically by name before setting and fetching details
                list.sort((a, b) => (a.name || '').localeCompare(b.name || ''));

                syncMacros(list);
                macroCache.current = {}; // Reset cache on full list fetch
                addLog(`Initialized ${list.length} macros. Fetching details...`);

                // Sequentially fetch elements for each macro to respect USB limitations
                for (const m of list) {
                    let retries = 3;
                    let success = false;
                    while (retries > 0 && !success) {
                        const result = await fetchSingleMacro(m.id);
                        if (result) {
                            success = true;
                        } else {
                            retries--;
                            if (retries > 0) {
                                console.warn(`[useMacros] Macro ID ${m.id} fetch failed, retrying... (${retries} left)`);
                                await new Promise(r => setTimeout(r, 1000));
                            } else {
                                console.error(`[useMacros] Macro ID ${m.id} failed to fetch after multiple attempts.`);
                            }
                        }
                    }
                }
            } catch (e) {
                console.error('Macros JSON Parse Error:', e);
            }
        }
    }, [isConnected, fetchSingleMacro, addLog]);

    const saveMacro = async (newMacro: Macro): Promise<void> => {
        let macroToSave = newMacro;
        let isNew = false;
        const maxAllowedId = macroLimits ? macroLimits.maxMacros - 1 : 31;

        if (newMacro.id === -1) {
            // Find the smallest available ID using the ref (latest state)
            const existingIds = new Set(macrosRef.current.map(m => m.id));
            let nextId = 0;
            while (existingIds.has(nextId)) nextId++;

            if (nextId > maxAllowedId) {
                throw new Error(`Maximum number of macros reached. Max allowed is ${maxAllowedId + 1}.`);
            }

            macroToSave = { ...newMacro, id: nextId };
            isNew = true;

            // OPTIMISTIC RESERVATION: Update ref and state IMMEDIATELY to prevent collisions in sequential calls
            syncMacros([...macrosRef.current, macroToSave]);
        } else {
            if (macroToSave.id > maxAllowedId) {
                throw new Error(`Macro ID ${macroToSave.id} exceeds maximum allowed ID of ${maxAllowedId}.`);
            }
        }

        // Send only the single macro via CFG_KEY_MACRO_SINGLE
        const jsonStr = JSON.stringify(macroToSave);
        const jsonBytes = new TextEncoder().encode(jsonStr);
        const buf = new Uint8Array(3 + jsonBytes.length);
        buf[0] = MODULE_CONFIG;
        buf[1] = CFG_CMD_SET;
        buf[2] = CFG_KEY_MACRO_SINGLE;
        buf.set(jsonBytes, 3);

        const resp = await hidService.sendCommand(buf);
        if (resp && resp.status === 0) {
            // Final merge: ensure the specific card is updated and deduplicated by ID
            const newList = macrosRef.current.map(m => m.id === macroToSave.id ? macroToSave : m);
            const deduplicated = Array.from(new Map(newList.map(m => [m.id, m])).values());
            syncMacros(deduplicated);
            macroCache.current[macroToSave.id] = macroToSave; // Update cache gracefully
            addLog(`Macro "${macroToSave.name}" saved to device (ID: ${macroToSave.id})`);
        } else {
            // ROLLBACK if it was a new reservation that failed
            if (isNew) {
                syncMacros(macrosRef.current.filter(m => m.id !== macroToSave.id));
            }
            const errMsg = resp ? `Device error (0x${resp.status.toString(16).toUpperCase()})` : 'Device timeout';
            addLog(`Failed to save macro: ${errMsg}`);
            throw new Error(errMsg);
        }
    };

    const deleteMacro = async (id: number): Promise<void> => {
        const isConfirmed = await confirm(
            'Delete Macro',
            'Are you sure you want to delete this macro? Any keys mapped to it will stop working.'
        );
        if (!isConfirmed) return;

        // Send delete command via CFG_KEY_MACRO_SINGLE
        const jsonStr = JSON.stringify({ delete: id });
        const jsonBytes = new TextEncoder().encode(jsonStr);
        const buf = new Uint8Array(3 + jsonBytes.length);
        buf[0] = MODULE_CONFIG;
        buf[1] = CFG_CMD_SET;
        buf[2] = CFG_KEY_MACRO_SINGLE;
        buf.set(jsonBytes, 3);

        const resp = await hidService.sendCommand(buf);
        if (resp && resp.status === 0) {
            syncMacros(macrosRef.current.filter(m => m.id !== id));
            delete macroCache.current[id]; // Remove from cache
            addLog(`Macro deleted. ${macrosRef.current.length} remaining.`);
        } else {
            const errMsg = resp ? `Device error (0x${resp.status.toString(16).toUpperCase()})` : 'Device timeout';
            addLog(`Failed to delete macro: ${errMsg}`);
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
