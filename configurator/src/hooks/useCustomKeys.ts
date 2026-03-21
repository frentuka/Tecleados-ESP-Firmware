import { useState, useCallback } from 'react';
import { hidService } from '../HIDService';
import type { CustomKey } from '../types/customKeys';

type ConfirmFn = (title: string, description: string) => Promise<boolean>;

/**
 * Manages all custom key state and device operations.
 *
 * @param isConnected - Current device connection state.
 * @param addLog      - Callback to append a text entry to the global log.
 * @param confirm     - Async confirm dialog from useConfirm.
 */
export function useCustomKeys(
    isConnected: boolean,
    addLog: (text: string) => void,
    confirm: ConfirmFn,
) {
    const [customKeys, setCustomKeys] = useState<CustomKey[]>([]);

    const fetchSingleCustomKey = useCallback(async (id: number): Promise<CustomKey | null> => {
        if (!isConnected) return null;
        try {
            const detail = await hidService.fetchCustomKeySingle(id);
            if (detail) {
                setCustomKeys(prev => {
                    const newList = prev.map(k => k.id === id ? detail : k);
                    return [...newList].sort((a, b) => a.id - b.id);
                });
                return detail;
            }
        } catch (e) {
            console.error(`[useCustomKeys] Failed to fetch custom key ${id}:`, e);
        }
        return null;
    }, [isConnected]);

    const fetchCustomKeys = useCallback(async () => {
        if (!isConnected) return;
        try {
            const outline = await hidService.fetchCustomKeys();
            setCustomKeys(outline.sort((a, b) => a.id - b.id));

            addLog(`Found ${outline.length} custom keys. Fetching details...`);

            // Sequentially fetch details for each custom key
            for (const k of outline) {
                await fetchSingleCustomKey(k.id);
            }
            addLog(`All custom key details loaded.`);
        } catch (e) {
            console.error('[useCustomKeys] Failed to fetch custom keys:', e);
        }
    }, [isConnected, fetchSingleCustomKey, addLog]);

    const saveCustomKey = async (ckey: CustomKey): Promise<void> => {
        let ckeyToSave = ckey;
        if (ckey.id === -1) {
            const usedIds = new Set(customKeys.map(k => k.id));
            let nextId = 0;
            while (usedIds.has(nextId)) nextId++;
            if (nextId >= 32) throw new Error('Maximum number of custom keys (32) reached.');
            ckeyToSave = { ...ckey, id: nextId };
        }
        const ok = await hidService.saveCustomKey(ckeyToSave);
        if (!ok) throw new Error('Failed to save custom key to device');
        setCustomKeys(prev => {
            const filtered = prev.filter(k => k.id !== ckeyToSave.id);
            return [...filtered, ckeyToSave].sort((a, b) => a.id - b.id);
        });
    };

    const deleteCustomKey = async (id: number): Promise<void> => {
        const isConfirmed = await confirm(
            'Delete Custom Key',
            'Are you sure? Any keys mapped to this custom key will stop working.'
        );
        if (!isConfirmed) return;
        const ok = await hidService.deleteCustomKey(id);
        if (!ok) throw new Error('Failed to delete custom key from device');
        setCustomKeys(prev => prev.filter(k => k.id !== id));
    };

    return {
        customKeys,
        fetchCustomKeys,
        saveCustomKey,
        deleteCustomKey,
    };
}
