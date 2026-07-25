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
  /** Authority fence used to install the resolved route in the stateful runtime. */
  readonly targetNodeGeneration?: bigint;
  /** Authority fence used to reject a superseded Spot owner. */
  readonly authorityOwnerGeneration?: bigint;
  /** Store version paired with the authority fence. */
  readonly authorityStoreVersion?: string;
}
