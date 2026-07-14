import type { ActorRef, RoutingId } from '../../contracts/Common';
import {
  ZLinkLocationAutoConnectType,
  ZLinkLocationKind,
  ZLinkLocationRole,
  ZLinkLocationTopologyState,
  type ZLinkActorLocationStore,
  type ZLinkLocationReadiness,
  type ZLinkLocationRuntimeQuery,
  type ZLinkPeerLocationResolver,
  type ZLinkPeerLocationStore,
  type ZLinkRouteLocationStore,
  type ZLinkSpotLocationStore,
  type ZLinkActorLocation,
  type ZLinkActorLocationKey,
  type ZLinkPeerLocation,
  type ZLinkPeerLocationFilter,
  type ZLinkRouteLocation,
  type ZLinkRouteLocationKey,
  type ZLinkSpotLocation,
  type ZLinkSpotLocationKey,
} from '../../contracts/Locations';
import {
  ZLinkSpotKind,
  type SpotHandle,
  type ZLinkActorSpotHandleResolver,
  type ZLinkSpotHandleResolver
} from '../../contracts/Spots';
import { ZLinkFrameworkErrorKind, ZLinkFrameworkException } from '../../contracts/Errors';
import type {
  ZLinkSpotRouteResolver,
  ZLinkSpotRouteTarget
} from '../spots/spot-routing-internal';
import { createSpotHandle, type ResolvedSpotHandle } from '../spots/spot-handle';
import {
  isKnownZLinkLocationAutoConnectType,
  isKnownZLinkLocationRole
} from './canonical-codec';
import {
  ZLinkLiveRowFilter,
  ZLinkOwnerLeaseTracker
} from './lease-tracker';
import { routingIdsEqual } from '../routing-id';

export interface ZLinkStoreLocationResolverStores {
  readonly peerStore: ZLinkPeerLocationStore;
  readonly spotStore: ZLinkSpotLocationStore;
  readonly actorStore: ZLinkActorLocationStore;
  readonly routeStore: ZLinkRouteLocationStore;
}

export interface ZLinkLocationResolverEventSink {
  spotResolveMiss(key: ZLinkSpotLocationKey): void;
  actorResolveMiss(key: ZLinkActorLocationKey): void;
  routeResolveMiss(key: ZLinkRouteLocationKey): void;
}

export interface ZLinkStoreLocationResolversOptions {
  readonly stores: ZLinkStoreLocationResolverStores;
  readonly leaseTracker: ZLinkOwnerLeaseTracker;
  readonly events?: ZLinkLocationResolverEventSink;
  readonly spotMeshNames?: readonly string[];
}

export class ZLinkStoreLocationResolvers implements
  ZLinkPeerLocationResolver,
  ZLinkSpotHandleResolver,
  ZLinkActorSpotHandleResolver {
  private readonly liveRows: ZLinkLiveRowFilter;
  private nextActorPlacement = 0;

  constructor(private readonly options: ZLinkStoreLocationResolversOptions) {
    this.liveRows = new ZLinkLiveRowFilter(options.leaseTracker);
  }

  async listLivePeers(filter: ZLinkPeerLocationFilter, signal?: AbortSignal): Promise<readonly ZLinkPeerLocation[]> {
    const rows = await this.options.stores.peerStore.listPeers(filter, signal);
    return await this.liveRows.filter(
      rows,
      (row) => row.ownerId,
      signal,
      (row) => isKnownZLinkLocationAutoConnectType(row.autoConnectType) && isKnownZLinkLocationRole(row.role)
    );
  }

  async selectActorPlacement(
    meshName: string,
    actorType: string,
    excludedNodeRid: RoutingId,
    signal?: AbortSignal
  ): Promise<RoutingId | undefined> {
    const capability = `actor:${actorType}`;
    const peers = await this.listLivePeers({
      autoConnectType: ZLinkLocationAutoConnectType.SpotMesh,
      meshName,
      role: ZLinkLocationRole.Spot
    }, signal);
    const candidates = peers.filter((peer) =>
      !peer.draining
      && peer.nodeRid !== undefined
      && !routingIdsEqual(peer.nodeRid, excludedNodeRid)
      && peer.capabilities?.includes(capability) === true
    );
    if (candidates.length === 0) return undefined;
    const selected = candidates[this.nextActorPlacement % candidates.length]?.nodeRid;
    this.nextActorPlacement = (this.nextActorPlacement + 1) % Number.MAX_SAFE_INTEGER;
    return selected;
  }

  async resolveRoute(key: ZLinkRouteLocationKey, signal?: AbortSignal): Promise<ZLinkRouteLocation | undefined> {
    const row = await this.liveRows.resolve(
      await this.options.stores.routeStore.resolveRoute(key, signal),
      (candidate) => candidate.ownerId,
      signal
    );
    if (row === undefined) {
      this.options.events?.routeResolveMiss(key);
      return undefined;
    }
    return row;
  }

  async resolveSpotRef(spotRid: RoutingId, signal?: AbortSignal): Promise<ResolvedSpotHandle | undefined> {
    const row = await this.resolveSpotRowInMeshes(spotRid, this.options.spotMeshNames ?? [], signal);
    if (row === undefined) {
      return undefined;
    }
    return {
      meshName: row.meshName,
      nodeRid: String(row.nodeRid),
      spotRid: String(row.spotRid),
      spotKind: row.spotKind
    };
  }

  async resolveSpotHandle(spotRid: RoutingId, signal?: AbortSignal): Promise<SpotHandle | undefined> {
    if (await this.resolveSpotRef(spotRid, signal) === undefined) return undefined;
    return createSpotHandle(String(spotRid), (refreshSignal) => this.resolveSpotRef(spotRid, refreshSignal));
  }

  async resolveActorSpotRef(
    actorId: string,
    signal?: AbortSignal
  ): Promise<ResolvedSpotHandle | undefined> {
    const row = await this.resolveActorRow({ actorId }, signal);
    if (row === undefined) {
      return undefined;
    }
    const spotRid = row.locationKind === ZLinkSpotKind.Entry || row.spotRid === undefined
      ? row.nodeRid
      : row.spotRid;
    return {
      meshName: row.spotMeshName,
      nodeRid: String(row.nodeRid),
      spotRid: String(spotRid),
      spotKind: row.locationKind
    };
  }

  async resolveActorRef(actorId: string, signal?: AbortSignal): Promise<ActorRef | undefined> {
    return (await this.resolveActorRow({ actorId }, signal))?.actorRef;
  }

  async resolveActorSpotHandle(actorId: string, signal?: AbortSignal): Promise<SpotHandle | undefined> {
    const initial = await this.resolveActorSpotRef(actorId, signal);
    if (initial === undefined) return undefined;
    return createSpotHandle(initial.spotRid, (refreshSignal) => this.resolveActorSpotRef(actorId, refreshSignal));
  }

  async resolveSpotRow(
    key: ZLinkSpotLocationKey,
    signal?: AbortSignal
  ): Promise<ZLinkSpotLocation | undefined> {
    const row = await this.liveRows.resolve(
      await this.options.stores.spotStore.resolveSpot(key, signal),
      (candidate) => candidate.ownerId,
      signal
    );
    if (row === undefined) {
      this.options.events?.spotResolveMiss(key);
      return undefined;
    }
    return row;
  }

  async resolveSpotRowInMeshes(
    spotRid: RoutingId,
    meshNames: readonly string[],
    signal?: AbortSignal
  ): Promise<ZLinkSpotLocation | undefined> {
    for (const meshName of meshNames) {
      const row = await this.resolveSpotRow({ meshName, spotRid }, signal);
      if (row !== undefined) {
        return row;
      }
    }
    return undefined;
  }

  async resolveActorRow(
    key: ZLinkActorLocationKey,
    signal?: AbortSignal
  ): Promise<ZLinkActorLocation | undefined> {
    const row = await this.liveRows.resolve(
      await this.options.stores.actorStore.resolveActor({ actorId: key.actorId }, signal),
      (candidate) => candidate.ownerId,
      signal,
      (candidate) => candidate.actorRef !== undefined
    );
    if (row === undefined) {
      this.options.events?.actorResolveMiss({ actorId: key.actorId });
      return undefined;
    }
    return row;
  }
}

export class DefaultZLinkLocationReadiness implements ZLinkLocationReadiness {
  constructor(private readonly query: ZLinkLocationRuntimeQuery) {}

  async isPeerReady(
    meshName: string,
    role: ZLinkLocationRole,
    nodeRid?: RoutingId,
    signal?: AbortSignal
  ): Promise<boolean> {
    try {
      const page = await this.query.listTopology({
        meshName,
        role,
        nodeRid,
        kind: ZLinkLocationKind.Peer,
        state: ZLinkLocationTopologyState.Ready
      }, undefined, signal);
      return page.items.length > 0;
    } catch {
      return false;
    }
  }
}

export class ZLinkLocationSpotRouteResolver implements ZLinkSpotRouteResolver {
  constructor(
    private readonly rows: ZLinkStoreLocationResolvers,
    private readonly meshNames: readonly string[],
    private readonly routerChannelIdForMesh: (meshName: string) => string = (meshName) => meshName
  ) {}

  async resolve(spotRid: RoutingId, signal?: AbortSignal): Promise<ZLinkSpotRouteTarget> {
    const row = await this.rows.resolveSpotRowInMeshes(spotRid, this.meshNames, signal);
    if (row !== undefined) {
      return {
        routerChannelId: this.routerChannelIdForMesh(row.meshName),
        targetNodeRid: row.nodeRid,
        spotRid: row.spotRid,
        spotKind: row.spotKind
      };
    }
    for (const meshName of this.meshNames) {
      const peers = await this.rows.listLivePeers({
        autoConnectType: ZLinkLocationAutoConnectType.SpotMesh,
        meshName,
        nodeRid: spotRid,
        role: ZLinkLocationRole.Spot
      }, signal);
      const peer = peers.find((candidate) => routingIdsEqual(candidate.nodeRid, spotRid));
      if (peer?.nodeRid !== undefined) {
        return {
          routerChannelId: this.routerChannelIdForMesh(meshName),
          targetNodeRid: peer.nodeRid,
          spotRid: peer.nodeRid,
          spotKind: ZLinkSpotKind.Entry
        };
      }
    }
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.SpotRouteNotFound,
      `SPOT '${spotRid}' has no live location row in any registered spot mesh.`
    );
  }
}
