// SPDX-License-Identifier: MPL-2.0

export type RequestProgressFn = (handle: unknown) => void;

interface RequestProgressState {
  refCount: number;
  pump: RequestProgressFn;
  interval: NodeJS.Timeout;
}

const requestProgressByHandle = new Map<unknown, RequestProgressState>();

export function startRequestProgress(handle: unknown, pump: RequestProgressFn): () => void {
  const existing = requestProgressByHandle.get(handle);
  if (existing) {
    existing.refCount += 1;
    return () => releaseRequestProgress(handle);
  }

  const state: RequestProgressState = {
    refCount: 1,
    pump,
    interval: setInterval(() => {
      const current = requestProgressByHandle.get(handle);
      if (!current) {
        return;
      }
      try {
        current.pump(handle);
      } catch {
        // Progress calls are best-effort. Completion still arrives through the native callback.
      }
    }, 1)
  };
  state.interval.unref();
  requestProgressByHandle.set(handle, state);
  return () => releaseRequestProgress(handle);
}

function releaseRequestProgress(handle: unknown): void {
  const state = requestProgressByHandle.get(handle);
  if (!state) {
    return;
  }
  state.refCount -= 1;
  if (state.refCount > 0) {
    return;
  }
  clearInterval(state.interval);
  requestProgressByHandle.delete(handle);
}
