import type {
  ZLinkActorLocation,
  ZLinkActorLocationFilter,
  ZLinkActorLocationKey,
  ZLinkLocationChanged,
  ZLinkLocationChangeStampScope,
  ZLinkLocationOwnerToken,
  ZLinkLocationPage,
  ZLinkLocationWatchFilter,
  ZLinkLocationWriteIntent,
  ZLinkLocationWriteResult,
  ZLinkLocationWriteStatus,
  ZLinkMeshNodeDescriptor,
  ZLinkMeshNodeDescriptorKey,
  ZLinkOwnerLeaseClaimResult,
  ZLinkOwnerLeaseReadResult,
  ZLinkOwnerLeaseReleaseResult,
  ZLinkOwnerLeaseRenewResult,
  ZLinkPageRequest,
  ZLinkPeerLocation,
  ZLinkPeerLocationFilter,
  ZLinkPeerLocationKey,
  ZLinkRouteLocation,
  ZLinkRouteLocationFilter,
  ZLinkRouteLocationKey,
  ZLinkSpotLocation,
  ZLinkSpotLocationFilter,
  ZLinkSpotLocationKey
} from './Models';
import type { ZLinkActorTransferStore } from './ActorTransfer';
import type {
  ZLinkAuthorityStore,
  ZLinkObjectCreationStore,
  ZLinkRelocationCapacityStore
} from './Authority';

export interface ZLinkLocationStore extends
  ZLinkMeshNodeLocationStore,
  ZLinkSpotLocationStore,
  ZLinkActorLocationStore,
  ZLinkOwnerLeaseStore,
  ZLinkActorTransferStore,
  ZLinkAuthorityStore,
  ZLinkObjectCreationStore,
  ZLinkRelocationCapacityStore {
  removeAllByOwner(owner: ZLinkLocationOwnerToken, signal?: AbortSignal): Promise<bigint>;
}

export interface ZLinkMeshNodeLocationStore {
  updateMeshNode(
    descriptor: ZLinkMeshNodeDescriptor,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult>;
  removeMeshNode(
    key: ZLinkMeshNodeDescriptorKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus>;
  listMeshNodes(meshName: string, signal?: AbortSignal): Promise<readonly ZLinkMeshNodeDescriptor[]>;
}

export interface ZLinkPeerLocationStore {
  updatePeer(
    peer: ZLinkPeerLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult>;
  removePeer(
    key: ZLinkPeerLocationKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult>;
  listPeers(filter: ZLinkPeerLocationFilter, signal?: AbortSignal): Promise<readonly ZLinkPeerLocation[]>;
}

export interface ZLinkSpotLocationStore {
  updateSpot(
    location: ZLinkSpotLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult>;
  removeSpot(
    key: ZLinkSpotLocationKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus>;
  resolveSpot(key: ZLinkSpotLocationKey, signal?: AbortSignal): Promise<ZLinkSpotLocation | undefined>;
}

// Operational pagination is an internal runtime capability rather than an
// application-facing store requirement.
export interface ZLinkSpotLocationQueryStore {
  listSpots(
    filter: ZLinkSpotLocationFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkSpotLocation>>;
}

export interface ZLinkActorLocationStore {
  updateActor(
    location: ZLinkActorLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult>;
  removeActor(
    key: ZLinkActorLocationKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus>;
  resolveActor(key: ZLinkActorLocationKey, signal?: AbortSignal): Promise<ZLinkActorLocation | undefined>;
}

// Operational pagination is consumed by the framework runtime, but is not
// part of the application-facing location-store contract.
export interface ZLinkActorLocationQueryStore {
  listActors(
    filter: ZLinkActorLocationFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkActorLocation>>;
}

export interface ZLinkRouteLocationStore {
  updateRoute(
    route: ZLinkRouteLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult>;
  removeRoute(
    key: ZLinkRouteLocationKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult>;
  resolveRoute(key: ZLinkRouteLocationKey, signal?: AbortSignal): Promise<ZLinkRouteLocation | undefined>;
  listRoutes(
    filter: ZLinkRouteLocationFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkRouteLocation>>;
}

export interface ZLinkOwnerLeaseStore {
  claimOwnerLease(
    ownerId: string,
    leaseTtlMs: number,
    signal?: AbortSignal
  ): Promise<ZLinkOwnerLeaseClaimResult>;
  readOwnerLease(
    ownerId: string,
    signal?: AbortSignal
  ): Promise<ZLinkOwnerLeaseReadResult>;
  renewOwnerLease(
    token: ZLinkLocationOwnerToken,
    leaseTtlMs: number,
    signal?: AbortSignal
  ): Promise<ZLinkOwnerLeaseRenewResult>;
  releaseOwnerLease(
    token: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkOwnerLeaseReleaseResult>;
}

export interface ZLinkLocationWatchStore {
  watch(filter: ZLinkLocationWatchFilter, signal?: AbortSignal): AsyncIterable<ZLinkLocationChanged>;
}

export interface ZLinkLocationChangeStampStore {
  getChangeStamp(scope: ZLinkLocationChangeStampScope, signal?: AbortSignal): Promise<bigint>;
}
