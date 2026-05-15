// SPDX-License-Identifier: MPL-2.0

import { requireNative } from '../native/native';

export type RequestProgressFn = (handle: unknown) => void;

interface RequestProgressState {
  refCount: number;
  poller: unknown;
  interval: NodeJS.Timeout;
}

const requestProgressByHandle = new Map<unknown, RequestProgressState>();

export function startRequestProgress(handle: unknown, pump: RequestProgressFn): () => void {
  void pump;
  const existing = requestProgressByHandle.get(handle);
  if (existing) {
    existing.refCount += 1;
    return () => releaseRequestProgress(handle);
  }

  const state: RequestProgressState = {
    refCount: 1,
    poller: requireNative().pollerNew(),
    interval: setInterval(() => {
      const current = requestProgressByHandle.get(handle);
      if (!current) {
        return;
      }
      try {
        requireNative().pollerWait(current.poller, 0);
      } catch {
        // Progress calls are best-effort. Completion still arrives through the native callback.
      }
    }, 1)
  };
  requireNative().pollerAdd(state.poller, handle, null, 32);
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
  try {
    requireNative().pollerRemove(state.poller, handle);
  } catch {
  }
  try {
    requireNative().pollerDestroy(state.poller);
  } catch {
  }
  requestProgressByHandle.delete(handle);
}
