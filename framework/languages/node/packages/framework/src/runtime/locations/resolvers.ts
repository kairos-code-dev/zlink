import type { ActorRef, RoutingId, SpotId } from '../../contracts/Common';
import type {
  ZLinkMeshNodeLocationStore,
  ZLinkPeerLocationStore,
  ZLinkRouteLocationStore
} from '../../contracts/Locations/Stores';
import type { ZLinkAuthorityStore } from '../../contracts/Locations/Authority';
import {
  ZLinkLocationKind,
  ZLinkLocationRole,
  ZLinkLocationTopologyState,
  ZLinkFrameworkRuntimeState,
  ZLinkObjectRole,
  type ZLinkActorLocationStore,
  type ZLinkLocationReadiness,
  type ZLinkLocationRuntimeQuery,
  type ZLinkPeerLocationResolver,
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
import { decodeServiceReadySpotAuthority } from '../foundation/service-authority-payload-codec';
import { encodeAuthorityKey } from './authority-key-codec';
import {
  ZLinkSpotKind
} from '../../contracts/Spots';
import type {
  SpotHandle,
  ZLinkActorSpotHandleResolver,
  ZLinkSpotHandleResolver
} from '../spots/spot-handle';
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
  readonly locationStore: ZLinkMeshNodeLocationStore;
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
  private nextActorPlacement = 0n;

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

  async resolveEntrySpotNode(
    spotId: SpotId,
    meshNames: readonly string[],
    signal?: AbortSignal
  ): Promise<{ readonly meshName: string; readonly nodeRid: RoutingId; readonly spotId: SpotId } | undefined> {
    for (const meshName of meshNames) {
      const descriptors = await this.liveRows.filter(
        await this.options.stores.locationStore.listMeshNodes(meshName, signal),
        (descriptor) => descriptor.ownerId,
        signal
      );
      const descriptor = descriptors.find((candidate) =>
        candidate.entrySpotId === spotId
        && candidate.objectRole === ZLinkObjectRole.Server
        && candidate.state !== ZLinkFrameworkRuntimeState.Stopped
      );
      if (descriptor !== undefined) {
        return { meshName, nodeRid: descriptor.rid, spotId };
      }
    }
    return undefined;
  }

  async selectActorPlacement(
    meshName: string,
    actorType: string,
    excludedNodeRid: RoutingId,
    signal?: AbortSignal,
    excludedCandidateRids: ReadonlySet<string> = new Set()
  ): Promise<RoutingId | undefined> {
    const descriptors = await this.options.stores.locationStore.listMeshNodes(meshName, signal);
    const liveDescriptors = await this.liveRows.filter(
      descriptors,
      (descriptor) => descriptor.ownerId,
      signal
    );
    const candidates = liveDescriptors.filter((descriptor) => {
      const capacity = descriptor.populationCapacity.actors;
      return descriptor.state === ZLinkFrameworkRuntimeState.Serving
        && descriptor.objectRole === ZLinkObjectRole.Server
        && descriptor.placementWeight > 0
        && !routingIdsEqual(descriptor.rid, excludedNodeRid)
        && !excludedCandidateRids.has(String(descriptor.rid))
        && capacity.active + capacity.reserved < capacity.limit
        && descriptor.objectCapabilities.some((candidate) =>
          candidate.objectKind === 'actor' && candidate.stableType === actorType
        );
    });
    if (candidates.length === 0) return undefined;
    const totalWeight = candidates.reduce(
      (total, candidate) => total + BigInt(candidate.placementWeight),
      0n
    );
    let ticket = this.nextActorPlacement % totalWeight;
    this.nextActorPlacement++;
    for (const candidate of candidates) {
      const weight = BigInt(candidate.placementWeight);
      if (ticket < weight) return candidate.rid;
      ticket -= weight;
    }
    return candidates[candidates.length - 1]?.rid;
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

  async resolveSpotRef(
    meshName: string,
    spotId: RoutingId,
    signal?: AbortSignal
  ): Promise<ResolvedSpotHandle | undefined> {
    const row = await this.resolveSpotRow({ meshName, spotId }, signal);
    if (row !== undefined) {
      if (row.spotGeneration <= 0n) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.SpotRouteNotFound,
          `SPOT '${spotId}' location row has no valid Core lifecycle generation.`
        );
      }
      return {
        meshName: row.meshName,
        nodeRid: String(row.ownerNodeRid),
        spotId: String(row.spotId),
        spotKind: row.spotKind,
        spotGeneration: row.spotGeneration
      };
    }
    const entrySpot = await resolveEntrySpotPeerInMeshes(this, spotId, [meshName], signal);
    if (entrySpot !== undefined) {
      return {
        meshName: entrySpot.meshName,
        nodeRid: String(entrySpot.nodeRid),
        spotId: entrySpot.spotId,
        spotKind: ZLinkSpotKind.Entry
      };
    }
    return undefined;
  }

  async resolveSpotHandle(
    meshName: string,
    spotId: RoutingId,
    signal?: AbortSignal
  ): Promise<SpotHandle | undefined> {
    const initial = await this.resolveSpotRef(meshName, spotId, signal);
    if (initial === undefined) return undefined;
    return createSpotHandle(
      String(spotId),
      initial,
      (refreshSignal) => this.resolveSpotRef(meshName, spotId, refreshSignal)
    );
  }

  async resolveActorSpotRef(
    meshName: string,
    actorId: string,
    signal?: AbortSignal
  ): Promise<ResolvedSpotHandle | undefined> {
    const row = await this.resolveActorRow({ meshName, actorId }, signal);
    if (row === undefined) {
      return undefined;
    }
    const spotId = row.spotKind === ZLinkSpotKind.Entry
      ? row.ownerNodeRid
      : row.spotId;
    return {
      meshName: row.meshName,
      nodeRid: String(row.ownerNodeRid),
      spotId: String(spotId),
      spotKind: row.spotKind,
      spotGeneration: row.spotGeneration
    };
  }

  async resolveActorRef(actorId: string, signal?: AbortSignal): Promise<ActorRef | undefined> {
    return (await this.resolveActorRow({ meshName: '', actorId }, signal))?.actorRef;
  }

  async resolveActorSpotHandle(
    meshName: string,
    actorId: string,
    signal?: AbortSignal
  ): Promise<SpotHandle | undefined> {
    const initial = await this.resolveActorSpotRef(meshName, actorId, signal);
    if (initial === undefined) return undefined;
    return createSpotHandle(
      initial.spotId,
      initial,
      (refreshSignal) => this.resolveActorSpotRef(meshName, actorId, refreshSignal)
    );
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
    spotId: RoutingId,
    meshNames: readonly string[],
    signal?: AbortSignal
  ): Promise<ZLinkSpotLocation | undefined> {
    for (const meshName of meshNames) {
      const row = await this.resolveSpotRow({ meshName, spotId }, signal);
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
    const meshNames = key.meshName.length === 0
      ? this.options.spotMeshNames ?? []
      : [key.meshName];
    let stored: ZLinkActorLocation | undefined;
    for (const meshName of meshNames) {
      stored = await this.options.stores.actorStore.resolveActor({
        meshName,
        actorId: key.actorId
      }, signal);
      if (stored !== undefined) break;
    }
    const row = await this.liveRows.resolve(
      stored,
      (candidate) => candidate.ownerId,
      signal,
      (candidate) =>
        candidate.actorRef.generation > 0n
        && candidate.ownerNodeGeneration > 0n
        && candidate.membershipEpoch > 0n
    );
    if (row === undefined) {
      this.options.events?.actorResolveMiss(key);
      return undefined;
    }
    if (row.spotKind === ZLinkSpotKind.User) {
      const currentSpot = await this.resolveSpotRow({
        meshName: row.meshName,
        spotId: row.spotId
      }, signal);
      if (
        currentSpot === undefined
        || row.spotGeneration <= 0n
        || row.membershipEpoch <= 0n
        || row.spotGeneration !== currentSpot.spotGeneration
        || row.ownerNodeGeneration !== currentSpot.ownerNodeGeneration
        || !routingIdsEqual(row.ownerNodeRid, currentSpot.ownerNodeRid)
      ) {
        this.options.events?.actorResolveMiss(key);
        return undefined;
      }
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
    private readonly routerChannelIdForMesh: (meshName: string) => string = (meshName) => meshName,
    private readonly resolveLocalSpot?: (spotId: RoutingId) => ZLinkSpotRouteTarget | undefined
  ) {}

  async resolve(spotId: RoutingId, signal?: AbortSignal): Promise<ZLinkSpotRouteTarget> {
    const row = await this.rows.resolveSpotRowInMeshes(spotId, this.meshNames, signal);
    if (row !== undefined) {
      if (row.spotGeneration <= 0n) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.SpotRouteNotFound,
          `SPOT '${spotId}' location row has no valid Core lifecycle generation.`
        );
      }
      return {
        routerChannelId: this.routerChannelIdForMesh(row.meshName),
        targetNodeRid: row.ownerNodeRid,
        spotId: row.spotId,
        spotKind: row.spotKind,
        targetSpotGeneration: row.spotGeneration
      };
    }
    const local = this.resolveLocalSpot?.(spotId);
    if (local !== undefined) {
      return local;
    }
    const entrySpot = await resolveEntrySpotPeerInMeshes(this.rows, spotId, this.meshNames, signal);
    if (entrySpot !== undefined) {
      return {
        routerChannelId: this.routerChannelIdForMesh(entrySpot.meshName),
        targetNodeRid: entrySpot.nodeRid,
        spotId: entrySpot.spotId,
        spotKind: ZLinkSpotKind.Entry
      };
    }
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.SpotRouteNotFound,
      `SPOT '${spotId}' has no live location row in any registered spot mesh.`
    );
  }
}

export class ZLinkAuthoritySpotRouteResolver implements ZLinkSpotRouteResolver {
  constructor(
    private readonly store: ZLinkAuthorityStore,
    private readonly routerChannelIdForMesh: (meshName: string) => string,
    private readonly fallback?: ZLinkSpotRouteResolver
  ) {}

  async resolve(spotId: RoutingId, signal?: AbortSignal): Promise<ZLinkSpotRouteTarget> {
    const current = await this.store.readAuthority(
      encodeAuthorityKey('user_spot', String(spotId)),
      signal
    );
    if (current.kind === 'snapshot' && current.allocation.state === 'active') {
      const decoded = decodeServiceReadySpotAuthority(current.payload);
      if (
        decoded !== undefined
        && decoded.spotId === String(spotId)
        && decoded.ownerId === current.ownerId
        && decoded.ownerLeaseGeneration === current.ownerLeaseGeneration
      ) {
        return {
          routerChannelId: this.routerChannelIdForMesh(decoded.ownerMeshName),
          targetNodeRid: decoded.ownerNodeRid,
          spotId,
          spotKind: decoded.kind === 'instance_spot'
            ? ZLinkSpotKind.Instance
            : ZLinkSpotKind.User,
          stableType: decoded.stableType,
          targetSpotGeneration: current.objectGeneration,
          targetNodeGeneration: current.allocation.descriptorLifecycleGeneration,
          authorityOwnerGeneration: current.authorityOwnerGeneration,
          authorityStoreVersion: current.storeVersion.value
        };
      }
    }
    if (current.kind === 'snapshot') {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.SpotRouteNotFound,
        `SPOT '${spotId}' authority has not crossed the Ready barrier.`
      );
    }
    if (this.fallback !== undefined) {
      return await this.fallback.resolve(spotId, signal);
    }
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.SpotRouteNotFound,
      `SPOT '${spotId}' has no Ready authority.`
    );
  }
}

async function resolveEntrySpotPeerInMeshes(
  rows: Pick<ZLinkStoreLocationResolvers, 'resolveEntrySpotNode'>,
  spotId: SpotId,
  meshNames: readonly string[],
  signal?: AbortSignal
): Promise<{ readonly meshName: string; readonly nodeRid: RoutingId; readonly spotId: SpotId } | undefined> {
  return await rows.resolveEntrySpotNode(spotId, meshNames, signal);
}
