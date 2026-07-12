import type { RoutingId, SpotRef } from '../../contracts/Common';
import {
  ZLinkLocationKind,
  ZLinkLocationRole,
  ZLinkLocationTopologyState,
  type IZLinkActorAddressResolver,
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
  type ZLinkSpotRefResolver
} from '../../contracts/Locations';
import {
  ZLinkSpotKind
} from '../../contracts/Spots';
import { ZLinkFrameworkErrorKind, ZLinkFrameworkException } from '../../contracts/Errors';
import type {
  ZLinkSpotRouteResolver,
  ZLinkSpotRouteTarget
} from '../spots/spot-routing-internal';
import {
  isKnownZLinkLocationAutoConnectType,
  isKnownZLinkLocationRole
} from './canonical-codec';
import {
  ZLinkLiveRowFilter,
  ZLinkOwnerLeaseTracker
} from './lease-tracker';

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
  ZLinkSpotRefResolver,
  IZLinkActorAddressResolver {
  private readonly liveRows: ZLinkLiveRowFilter;

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

  async resolveSpotRef(spotRid: RoutingId, signal?: AbortSignal): Promise<SpotRef | undefined> {
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

  async resolveActorSpotRef(
    actorId: string,
    signal?: AbortSignal
  ): Promise<SpotRef | undefined> {
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
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.SpotRouteNotFound,
      `SPOT '${spotRid}' has no live location row in any registered spot mesh.`
    );
  }
}
