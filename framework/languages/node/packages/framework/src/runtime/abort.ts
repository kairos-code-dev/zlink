export function createAbortError(): Error {
  return new Error('The operation was aborted.');
}

export function throwIfAborted(signal: AbortSignal | undefined): void {
  if (signal?.aborted === true) {
    throw createAbortError();
  }
}
