import type { RoutingId } from '../Common';

declare const spotHandleBrand: unique symbol;

/** A stable public messaging target whose mutable route is owned by the framework. */
export interface SpotHandle {
  readonly meshName: string;
  readonly spotRid: RoutingId;
  readonly [spotHandleBrand]: never;
}

export interface ZLinkSpotHandleResolver {
  resolveSpotHandle(
    meshName: string,
    spotRid: RoutingId,
    signal?: AbortSignal
  ): Promise<SpotHandle | undefined>;
}

export interface ZLinkActorSpotHandleResolver {
  resolveActorSpotHandle(
    meshName: string,
    actorId: string,
    signal?: AbortSignal
  ): Promise<SpotHandle | undefined>;
}
