/**
 * Wraps a promise with a timeout. Rejects with a specific TimeoutError if the
 * promise doesn't resolve within `ms` milliseconds.
 */
export class TimeoutError extends Error {
    readonly isTimeout = true;
    constructor(ms: number) {
        super(`Operation timed out after ${ms / 1000}s. Please retry.`);
        this.name = 'TimeoutError';
    }
}

export function withTimeout<T>(promise: Promise<T>, ms: number): Promise<T> {
    let timerId: ReturnType<typeof setTimeout>;
    const timeoutPromise = new Promise<never>((_, reject) => {
        timerId = setTimeout(() => reject(new TimeoutError(ms)), ms);
    });
    return Promise.race([promise, timeoutPromise]).finally(() => clearTimeout(timerId));
}
