import type { RoutingId } from '../Common';
import type {
  ZLinkActorLocationFilter,
  ZLinkLocationPage,
  ZLinkLocationRuntimeStatus,
  ZLinkLocationServiceSummary,
  ZLinkLocationServiceSummaryFilter,
  ZLinkLocationTopologyEntry,
  ZLinkLocationTopologyFilter,
  ZLinkPageRequest,
  ZLinkPeerLocation,
  ZLinkPeerLocationFilter,
  ZLinkRouteLocation,
  ZLinkRouteLocationKey,
  ZLinkRouteLocationFilter,
  ZLinkSpotAddress,
  ZLinkSpotLocation,
  ZLinkSpotLocationFilter,
  ZLinkActorLocation
} from './Models';

export interface IZLinkPeerLocationResolver {
  listPeers(filter: ZLinkPeerLocationFilter, signal?: AbortSignal): Promise<readonly ZLinkPeerLocation[]>;
}

export interface IZLinkSpotLocationResolver {
  resolveSpotAddress(
    meshName: string,
    spotRid: RoutingId,
    signal?: AbortSignal
  ): Promise<ZLinkSpotAddress | undefined>;
}

export interface IZLinkActorLocationResolver {
  resolveActorSpotAddress(
    actorType: string,
    actorId: string,
    signal?: AbortSignal
  ): Promise<ZLinkSpotAddress | undefined>;
}

export interface IZLinkRouteLocationResolver {
  resolveRoute(key: ZLinkRouteLocationKey, signal?: AbortSignal): Promise<ZLinkRouteLocation | undefined>;
}

export interface IZLinkLocationRuntimeQuery {
  getStatus(signal?: AbortSignal): Promise<ZLinkLocationRuntimeStatus>;
  listPeers(filter: ZLinkPeerLocationFilter, signal?: AbortSignal): Promise<readonly ZLinkPeerLocation[]>;
  listSpots(
    filter: ZLinkSpotLocationFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkSpotLocation>>;
  listActors(
    filter: ZLinkActorLocationFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkActorLocation>>;
  listRoutes(
    filter: ZLinkRouteLocationFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkRouteLocation>>;
  listTopology(
    filter: ZLinkLocationTopologyFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkLocationTopologyEntry>>;
  listServiceSummaries(
    filter: ZLinkLocationServiceSummaryFilter,
    signal?: AbortSignal
  ): Promise<readonly ZLinkLocationServiceSummary[]>;
}
