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
  readonly draining: boolean;
  readonly value: bigint;
  readonly metadata?: Readonly<Record<string, string>>;
  readonly capabilities?: readonly string[];
  readonly ownerId: string;
  readonly generation: bigint;
  readonly updatedAt: Date;
}

/** RouteMesh discovery descriptor fixed by the 10.0 location-store contract. */
export interface ZLinkMeshNodeDescriptor {
  readonly meshName: string;
  readonly rid: RoutingId;
  readonly lifecycleGeneration: bigint;
  readonly descriptorRevision: bigint;
  readonly endpoint: string;
  readonly channelWeights: Readonly<Record<string, number>>;
  readonly draining: boolean;
  readonly securityIdentity: string;
  readonly ownerId: string;
  readonly updatedAt: Date;
}

export interface ZLinkSpotLocation {
  readonly meshName: string;
  readonly spotRid: RoutingId;
  /** Core lifecycle generation of the addressed Spot. */
  readonly spotGeneration: bigint;
  readonly spotType: string;
  /** MeshNode that currently owns this Spot. */
  readonly ownerNodeRid: RoutingId;
  /** Core lifecycle generation of the owning MeshNode. */
  readonly ownerNodeGeneration: bigint;
  readonly spotKind: ZLinkSpotKind;
  readonly ownerId: string;
  readonly updatedAt: Date;
}

export interface ZLinkActorLocation {
  /** RouteMesh that owns this Actor location. */
  readonly meshName: string;
  readonly actorId: string;
  readonly actorType: string;
  readonly actorRef: ActorRef;
  /** MeshNode that currently owns this Actor. */
  readonly ownerNodeRid: RoutingId;
  readonly ownerNodeGeneration: bigint;
  /** Kind of Spot that currently owns this Actor. */
  readonly spotKind: ZLinkSpotKind;
  readonly spotRid: RoutingId;
  readonly spotGeneration: bigint;
  readonly membershipEpoch: bigint;
  readonly ownerId: string;
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
