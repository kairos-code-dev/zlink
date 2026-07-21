import type { RoutingId } from '../../contracts/Common';
import type { ZLinkSpotKind } from '../../contracts/Spots';

export interface ZLinkSpotRouteResolver {
  resolve(spotRid: RoutingId, signal?: AbortSignal): Promise<ZLinkSpotRouteTarget>;
}

export interface ZLinkSpotRouteTarget {
  readonly routerChannelId: string;
  readonly targetNodeRid: RoutingId;
  readonly spotRid: RoutingId;
  readonly spotKind: ZLinkSpotKind;
  /** Required for user Spot operations and absent for an Entry Spot route. */
  readonly targetSpotGeneration?: bigint;
}
