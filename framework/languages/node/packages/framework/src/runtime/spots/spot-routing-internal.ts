import type { RoutingId } from '../../contracts/Common';
import type { ZLinkSpotKind } from '../../contracts/Spots';

export interface ZLinkSpotRouteResolver {
  resolve(spotId: RoutingId, signal?: AbortSignal): Promise<ZLinkSpotRouteTarget>;
}

export interface ZLinkSpotRouteTarget {
  readonly routerChannelId: string;
  readonly targetNodeRid: RoutingId;
  readonly spotId: RoutingId;
  readonly spotKind: ZLinkSpotKind;
  /** Stable registered type for authority-backed User and Instance Spots. */
  readonly stableType?: string;
  /** Required for user Spot operations and absent for an Entry Spot route. */
  readonly targetSpotGeneration?: bigint;
}
