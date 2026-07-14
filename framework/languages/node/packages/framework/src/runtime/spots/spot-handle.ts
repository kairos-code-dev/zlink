import type { RoutingId, SpotHandle, ZLinkSpotKind } from '../../contracts';

export interface ResolvedSpotHandle {
  readonly meshName: string;
  readonly nodeRid: RoutingId;
  readonly spotRid: RoutingId;
  readonly spotKind?: ZLinkSpotKind;
}

type SpotHandleResolver = (signal?: AbortSignal) => Promise<ResolvedSpotHandle | undefined>;

interface SpotHandleState {
  current: ResolvedSpotHandle | undefined;
  readonly refresh: SpotHandleResolver;
  refreshing?: Promise<ResolvedSpotHandle | undefined>;
}

const handleStates = new WeakMap<SpotHandle, SpotHandleState>();

export function createSpotHandle(
  spotRid: string,
  initial: ResolvedSpotHandle,
  refresh: SpotHandleResolver
): SpotHandle;
export function createSpotHandle(spotRid: string, refresh: SpotHandleResolver): SpotHandle;
export function createSpotHandle(
  spotRid: string,
  initialOrRefresh: ResolvedSpotHandle | SpotHandleResolver,
  refresh?: SpotHandleResolver
): SpotHandle {
  const handle = Object.freeze({ spotRid }) as SpotHandle;
  handleStates.set(handle, typeof initialOrRefresh === 'function'
    ? { current: undefined, refresh: initialOrRefresh }
    : { current: initialOrRefresh, refresh: refresh! });
  return handle;
}

export async function resolveSpotHandle(
  handle: SpotHandle,
  signal?: AbortSignal
): Promise<ResolvedSpotHandle | undefined> {
  const state = requireHandleState(handle);
  return state.current ?? await refreshSpotHandle(handle, signal);
}

export async function refreshSpotHandle(
  handle: SpotHandle,
  signal?: AbortSignal
): Promise<ResolvedSpotHandle | undefined> {
  const state = requireHandleState(handle);
  state.refreshing ??= state.refresh(signal).then((resolved) => {
    state.current = resolved;
    return resolved;
  }).finally(() => {
    state.refreshing = undefined;
  });
  return await state.refreshing;
}

function requireHandleState(handle: SpotHandle): SpotHandleState {
  const state = handleStates.get(handle);
  if (state === undefined) {
    throw new TypeError('SpotHandle was not created by this framework runtime.');
  }
  return state;
}
