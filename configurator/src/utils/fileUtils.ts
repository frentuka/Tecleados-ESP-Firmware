/**
 * fileUtils.ts — Browser file I/O helpers.
 */

/**
 * Save a string as a JSON file.
 *
 * Tries the File System Access API first (shows a native Save As dialog).
 * Falls back to a hidden <a> element download when the API is unavailable
 * or the browser doesn't support it.
 *
 * Silently ignores user cancellation (AbortError).
 * Re-throws any other error so the caller can handle it.
 */
export async function saveJsonFile(content: string, suggestedName: string): Promise<void> {
    if ('showSaveFilePicker' in window) {
        try {
            const handle = await (window as any).showSaveFilePicker({
                suggestedName,
                types: [{
                    description: 'JSON Files',
                    accept: { 'application/json': ['.json'] },
                }],
            });
            const writable = await handle.createWritable();
            await writable.write(content);
            await writable.close();
            return;
        } catch (err: any) {
            if (err.name === 'AbortError') return; // User cancelled
            // Fall through to legacy download on any other error
            console.error('showSaveFilePicker error:', err);
        }
    }

    // Legacy fallback: create a temporary <a> element and trigger a click
    const blob = new Blob([content], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = suggestedName;
    a.click();
    URL.revokeObjectURL(url);
}
