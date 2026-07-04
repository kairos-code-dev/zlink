import type { RoutingId } from '../Common';
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
  ZLinkOwnerLeaseRenewal,
  ZLinkOwnerLeaseSnapshot,
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

export interface IZLinkLocationStore extends
  IZLinkPeerLocationStore,
  IZLinkSpotLocationStore,
  IZLinkActorLocationStore,
  IZLinkRouteLocationStore,
  IZLinkOwnerLeaseStore {
  removeAllByOwner(ownerId: string, signal?: AbortSignal): Promise<number>;
}

export interface IZLinkPeerLocationStore {
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

export interface IZLinkSpotLocationStore {
  updateSpot(
    spot: ZLinkSpotLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult>;
  removeSpot(
    key: ZLinkSpotLocationKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult>;
  resolveSpot(key: ZLinkSpotLocationKey, signal?: AbortSignal): Promise<ZLinkSpotLocation | undefined>;
  listSpots(
    filter: ZLinkSpotLocationFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkSpotLocation>>;
}

export interface IZLinkActorLocationStore {
  updateActor(
    actor: ZLinkActorLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult>;
  removeActor(
    key: ZLinkActorLocationKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult>;
  resolveActor(key: ZLinkActorLocationKey, signal?: AbortSignal): Promise<ZLinkActorLocation | undefined>;
  listActors(
    filter: ZLinkActorLocationFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkActorLocation>>;
}

export interface IZLinkRouteLocationStore {
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

export interface IZLinkOwnerLeaseStore {
  renewOwnerLease(
    ownerId: string,
    nodeRid: RoutingId,
    leaseTtlMs: number,
    signal?: AbortSignal
  ): Promise<ZLinkOwnerLeaseRenewal>;
  removeOwnerLease(ownerId: string, signal?: AbortSignal): Promise<boolean>;
  listOwnerLeases(signal?: AbortSignal): Promise<ZLinkOwnerLeaseSnapshot>;
}

export interface IZLinkLocationWatchStore {
  watch(filter: ZLinkLocationWatchFilter, signal?: AbortSignal): AsyncIterable<ZLinkLocationChanged>;
}

export interface IZLinkLocationChangeStampStore {
  getChangeStamp(scope: ZLinkLocationChangeStampScope, signal?: AbortSignal): Promise<bigint>;
}
