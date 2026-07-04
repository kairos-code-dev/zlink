import type { ActorRef, RoutingId } from '../Common';
import type { ZLinkSpotKind } from '../Spots';
import type { ZLinkLocationAutoConnectType, ZLinkLocationRole, ZLinkRouteKind } from './Values';

export interface ZLinkPeerLocation {
  readonly autoConnectType: ZLinkLocationAutoConnectType;
  readonly meshName: string;
  readonly nodeRid?: RoutingId;
  readonly role: ZLinkLocationRole;
  readonly endpoint: string;
  readonly weight: number;
  readonly value: bigint;
  readonly metadata?: Readonly<Record<string, string>>;
  readonly capabilities?: readonly string[];
  readonly ownerId: string;
  readonly generation: bigint;
  readonly updatedAt: Date;
}

export interface ZLinkSpotLocation {
  readonly meshName: string;
  readonly spotRid: RoutingId;
  readonly spotType?: string;
  readonly nodeRid: RoutingId;
  readonly spotKind: ZLinkSpotKind;
  readonly routeEndpoint?: string;
  readonly ownerId: string;
  readonly generation: bigint;
  readonly updatedAt: Date;
}

export interface ZLinkActorLocation {
  readonly actorId: string;
  readonly actorType?: string;
  readonly actorRef?: ActorRef;
  readonly nodeRid: RoutingId;
  readonly locationKind: ZLinkSpotKind;
  readonly spotMeshName: string;
  readonly spotRid?: RoutingId;
  readonly ownerId: string;
  readonly generation: bigint;
  readonly updatedAt: Date;
}

export interface ZLinkRouteLocation {
  readonly routeKind: ZLinkRouteKind;
  readonly routeKey: string;
  readonly ownerNodeRid: RoutingId;
  readonly ownerId: string;
  readonly generation: bigint;
  /**
   * Framework internal route payload. This is not an application key-value
   * storage surface.
   */
  readonly value: Uint8Array;
  readonly updatedAt: Date;
}

export interface ZLinkOwnerLease {
  readonly ownerId: string;
  readonly nodeRid: RoutingId;
  readonly leaseExpiresAt: Date;
  readonly updatedAt: Date;
}

export interface ZLinkOwnerLeaseSnapshot {
  readonly leases: readonly ZLinkOwnerLease[];
  readonly storeNow: Date;
}

export interface ZLinkSpotAddress {
  readonly meshName: string;
  readonly nodeRid: RoutingId;
  readonly spotRid: RoutingId;
}
