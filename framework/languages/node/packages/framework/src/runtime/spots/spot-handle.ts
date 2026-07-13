import type { RoutingId, SpotHandle, ZLinkSpotKind } from '../../contracts';

export interface ResolvedSpotHandle {
  readonly meshName: string;
  readonly nodeRid: RoutingId;
  readonly spotRid: RoutingId;
  readonly spotKind?: ZLinkSpotKind;
}

type SpotHandleResolver = (signal?: AbortSignal) => Promise<ResolvedSpotHandle | undefined>;

const handleResolvers = new WeakMap<SpotHandle, SpotHandleResolver>();

export function createSpotHandle(spotRid: string, resolve: SpotHandleResolver): SpotHandle {
  const handle = Object.freeze({ spotRid }) as SpotHandle;
  handleResolvers.set(handle, resolve);
  return handle;
}

export async function resolveSpotHandle(
  handle: SpotHandle,
  signal?: AbortSignal
): Promise<ResolvedSpotHandle | undefined> {
  const resolve = handleResolvers.get(handle);
  if (resolve === undefined) {
    throw new TypeError('SpotHandle was not created by this framework runtime.');
  }
  return await resolve(signal);
}
