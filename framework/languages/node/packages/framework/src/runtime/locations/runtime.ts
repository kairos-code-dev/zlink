import { randomUUID } from 'node:crypto';
import type { RoutingId } from '../../contracts/Common';
import {
  ZLinkLocationAutoConnectType,
  ZLinkLocationKind,
  ZLinkLocationTopologyState,
  ZLinkLocationWriteIntent,
  ZLinkLocationWriteStatus,
  zlinkDefaultLocationOptions,
  type ZLinkActorLocationStore,
  type ZLinkLocationRuntimeQuery,
  type ZLinkLocationStore,
  type ZLinkOwnerLeaseStore,
  type ZLinkPeerLocationStore,
  type ZLinkRouteLocationStore,
  type ZLinkSpotLocationStore,
  type ZLinkActorLocation,
  type ZLinkActorLocationFilter,
  type ZLinkActorLocationKey,
  type ZLinkLocationKey,
  type ZLinkLocationOptions,
  type ZLinkLocationPage,
  type ZLinkLocationRuntimeStatus,
  type ZLinkLocationServiceSummary,
  type ZLinkLocationServiceSummaryFilter,
  type ZLinkLocationTopologyEntry,
  type ZLinkLocationTopologyFilter,
  type ZLinkLocationWriteResult,
  type ZLinkPageRequest,
  type ZLinkPeerLocation,
  type ZLinkPeerLocationFilter,
  type ZLinkPeerLocationKey,
  type ZLinkRouteLocation,
  type ZLinkRouteLocationFilter,
  type ZLinkRouteLocationKey,
  type ZLinkSpotLocation,
  type ZLinkSpotLocationFilter,
  type ZLinkSpotLocationKey
} from '../../contracts/Locations';
import {
  zlinkLocationAutoConnectTypeName,
  zlinkLocationRoleName
} from './canonical-codec';
import { ZLinkLocationKeyCodec } from './key-codec';
import {
  ZLinkLiveRowFilter,
  ZLinkOwnerLeaseTracker
} from './lease-tracker';
import type { ZLinkOwnershipLostEvent } from './lifecycle-runtime';

export interface ZLinkLocationRuntimeStores {
  readonly locationStore: ZLinkLocationStore;
  readonly peerStore: ZLinkPeerLocationStore;
  readonly spotStore: ZLinkSpotLocationStore;
  readonly actorStore: ZLinkActorLocationStore;
  readonly routeStore: ZLinkRouteLocationStore;
  readonly ownerLeaseStore: ZLinkOwnerLeaseStore;
}

export interface ZLinkLocationRuntimeOptions {
  readonly stores: ZLinkLocationRuntimeStores;
  readonly options?: ZLinkLocationOptions;
  readonly ownerId?: string;
  readonly events?: ZLinkLocationEventSink;
  readonly leaseTracker?: ZLinkOwnerLeaseTracker;
  readonly now?: () => Date;
  readonly setTimer?: (callback: () => void, delayMs: number) => unknown;
  readonly clearTimer?: (handle: unknown) => void;
  readonly metrics?: import('../diagnostics').ZLinkRuntimeMetrics;
}

export interface ZLinkLocationEventSink {
  peerRowUpdated(key: ZLinkLocationKey, peer: ZLinkPeerLocation): void;
  peerRowRemoved(key: ZLinkLocationKey): void;
  desiredSetChanged(change: {
    readonly autoConnectType: ZLinkLocationAutoConnectType;
    readonly meshName: string;
    readonly connectedEndpoints: readonly string[];
    readonly disconnectedEndpoints: readonly string[];
  }): void;
  spotRowUpdated(key: ZLinkSpotLocationKey, spot: ZLinkSpotLocation): void;
  spotRowRemoved(key: ZLinkSpotLocationKey): void;
  spotResolveMiss(key: ZLinkSpotLocationKey): void;
  actorRowUpdated(key: ZLinkActorLocationKey, actor: ZLinkActorLocation): void;
  actorRowRemoved(key: ZLinkActorLocationKey): void;
  actorResolveMiss(key: ZLinkActorLocationKey): void;
  routeRowUpdated(key: ZLinkRouteLocationKey, route: ZLinkRouteLocation): void;
  routeRowRemoved(key: ZLinkRouteLocationKey): void;
  routeResolveMiss(key: ZLinkRouteLocationKey): void;
}

export class ZLinkOwnerCleanupError extends Error {
  constructor(cause: unknown) {
    super(`Owner cleanup failed: ${errorMessage(cause)}`, { cause });
  }
}

export class ZLinkLocationRuntime implements ZLinkLocationRuntimeQuery {
  readonly ownerId: string;
  private readonly options: Required<ZLinkLocationOptions>;
  private readonly stores: ZLinkLocationRuntimeStores;
  private readonly events?: ZLinkLocationEventSink;
  private readonly setTimer: (callback: () => void, delayMs: number) => unknown;
  private readonly clearTimer: (handle: unknown) => void;
  private readonly now: () => Date;
  private readonly liveRows: ZLinkLiveRowFilter;
  private readonly ownershipLostHandlers = new Set<(event: ZLinkOwnershipLostEvent) => void>();
  private heartbeatTimer: unknown;
  private nodeRidValue?: RoutingId;
  private started = false;
  private ownerCleanupComplete = true;
  private readonly metrics?: import('../diagnostics').ZLinkRuntimeMetrics;
  private peerMetricCount = 0;
  private nextLeaseRenewAtMs?: number;

  ownerLeaseHealthy = false;
  ownerLeaseRenewedAt?: Date;
  lastError?: string;

  constructor(runtimeOptions: ZLinkLocationRuntimeOptions) {
    this.stores = runtimeOptions.stores;
    this.options = { ...zlinkDefaultLocationOptions, ...runtimeOptions.options };
    this.ownerId = runtimeOptions.ownerId ?? randomUUID().replaceAll('-', '');
    this.events = runtimeOptions.events;
    this.metrics = runtimeOptions.metrics;
    this.now = runtimeOptions.now ?? (() => new Date());
    this.setTimer = runtimeOptions.setTimer ?? ((callback, delayMs) => setTimeout(callback, delayMs));
    this.clearTimer = runtimeOptions.clearTimer ?? ((handle) => clearTimeout(handle as NodeJS.Timeout));
    const leaseTracker = runtimeOptions.leaseTracker ?? new ZLinkOwnerLeaseTracker({
      store: this.stores.ownerLeaseStore,
      options: this.options
    });
    this.liveRows = new ZLinkLiveRowFilter(leaseTracker);
  }

  get isStarted(): boolean {
    return this.started;
  }

  get nodeRid(): RoutingId | undefined {
    return this.nodeRidValue;
  }

  addOwnershipLostHandler(handler: (event: ZLinkOwnershipLostEvent) => void): void {
    this.ownershipLostHandlers.add(handler);
  }

  removeOwnershipLostHandler(handler: (event: ZLinkOwnershipLostEvent) => void): void {
    this.ownershipLostHandlers.delete(handler);
  }

  async start(nodeRid: RoutingId, signal?: AbortSignal): Promise<void> {
    if (this.started) {
      return;
    }

    this.started = true;
    this.ownerCleanupComplete = false;
    this.nodeRidValue = nodeRid;
    await this.renewOwnerLeaseOnce(signal);
    this.scheduleHeartbeat();
  }

  async stop(signal?: AbortSignal): Promise<void> {
    if (!this.started && this.ownerCleanupComplete) {
      return;
    }

    this.started = false;
    if (this.heartbeatTimer !== undefined) {
      this.clearTimer(this.heartbeatTimer);
      this.heartbeatTimer = undefined;
    }

    await this.cleanupOwner(signal);
  }

  async cleanupOwner(signal?: AbortSignal): Promise<void> {
    if (this.ownerCleanupComplete) {
      return;
    }
    try {
      await this.stores.ownerLeaseStore.removeOwnerLease(this.ownerId, signal);
      await this.stores.locationStore.removeAllByOwner(this.ownerId, signal);
      this.ownerCleanupComplete = true;
    } catch (error) {
      this.recordFailure(errorMessage(error));
      throw new ZLinkOwnerCleanupError(error);
    }
  }

  async renewOwnerLeaseOnce(signal?: AbortSignal): Promise<boolean> {
    if (this.nodeRidValue === undefined) {
      this.recordFailure('Owner lease renew requires a node routing id.');
      return false;
    }

    try {
      const result = await this.stores.ownerLeaseStore.renewOwnerLease(
        this.ownerId,
        this.nodeRidValue,
        this.options.ownerLeaseTtlMs,
        signal
      );
      this.ownerLeaseHealthy = true;
      this.ownerLeaseRenewedAt = result.storeNow;
      this.lastError = undefined;
      return true;
    } catch (error) {
      this.metrics?.count('zlink.location.owner_lease.renew.failures');
      this.recordFailure(errorMessage(error));
      return false;
    }
  }

  async publishDraining(signal?: AbortSignal): Promise<boolean> {
    try {
      const peers = await this.stores.peerStore.listPeers({}, signal);
      for (const peer of peers) {
        if (peer.ownerId !== this.ownerId || peer.draining) continue;
        const result = await this.stores.peerStore.updatePeer(
          { ...peer, draining: true },
          ZLinkLocationWriteIntent.Renew,
          signal
        );
        if (result.status !== ZLinkLocationWriteStatus.Stored) return false;
      }
      return true;
    } catch (error) {
      this.recordFailure(errorMessage(error));
      return false;
    }
  }

  async writePeer(
    peer: ZLinkPeerLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    zlinkLocationAutoConnectTypeName(peer.autoConnectType);
    zlinkLocationRoleName(peer.role);
    const stamped = { ...peer, ownerId: this.ownerId };
    const result = await this.guardWrite(() => this.stores.peerStore.updatePeer(stamped, intent, signal));
    const key: ZLinkPeerLocationKey = {
      autoConnectType: peer.autoConnectType,
      meshName: peer.meshName,
      role: peer.role,
      nodeRid: peer.nodeRid,
      endpoint: peer.endpoint
    };
    const rowKey = ZLinkLocationKeyCodec.encodePeerKey(key);
    if (result.status === ZLinkLocationWriteStatus.Stored) {
      this.events?.peerRowUpdated({ kind: ZLinkLocationKind.Peer, key }, { ...stamped, generation: result.generation, updatedAt: result.updatedAt });
    }
    this.notifyIfStale(result, ZLinkLocationKind.Peer, rowKey);
    return result;
  }

  async writeSpot(
    spot: ZLinkSpotLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    const stamped = { ...spot, ownerId: this.ownerId };
    const result = await this.guardWrite(() => this.stores.spotStore.updateSpot(stamped, intent, signal));
    const key = { meshName: spot.meshName, spotRid: spot.spotRid };
    if (result.status === ZLinkLocationWriteStatus.Stored) {
      this.events?.spotRowUpdated(key, { ...stamped, generation: result.generation, updatedAt: result.updatedAt });
    }
    this.notifyIfStale(result, ZLinkLocationKind.Spot, ZLinkLocationKeyCodec.encodeSpotKey(key));
    return result;
  }

  async writeActor(
    actor: ZLinkActorLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    const stamped = {
      ...actor,
      actorType: actor.actorType,
      ownerId: this.ownerId
    };
    const result = await this.guardWrite(() => this.stores.actorStore.updateActor(stamped, intent, signal));
    const key = { actorId: stamped.actorId };
    if (result.status === ZLinkLocationWriteStatus.Stored) {
      this.events?.actorRowUpdated(key, { ...stamped, generation: result.generation, updatedAt: result.updatedAt });
    }
    this.notifyIfStale(result, ZLinkLocationKind.Actor, ZLinkLocationKeyCodec.encodeActorKey(key));
    return result;
  }

  async writeRoute(
    route: ZLinkRouteLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    const stamped = { ...route, ownerId: this.ownerId };
    const result = await this.guardWrite(() => this.stores.routeStore.updateRoute(stamped, intent, signal));
    const key = { routeKind: route.routeKind, routeKey: route.routeKey };
    if (result.status === ZLinkLocationWriteStatus.Stored) {
      this.events?.routeRowUpdated(key, { ...stamped, generation: result.generation, updatedAt: result.updatedAt });
    }
    this.notifyIfStale(result, ZLinkLocationKind.Route, ZLinkLocationKeyCodec.encodeRouteKey(key));
    return result;
  }

  async removePeer(key: ZLinkPeerLocationKey, generation: bigint, signal?: AbortSignal): Promise<ZLinkLocationWriteResult> {
    const result = await this.guardWrite(() =>
      this.stores.peerStore.removePeer(key, { ownerId: this.ownerId, generation }, signal));
    if (result.status === ZLinkLocationWriteStatus.Stored) {
      this.events?.peerRowRemoved({ kind: ZLinkLocationKind.Peer, key });
    }
    this.notifyIfStale(result, ZLinkLocationKind.Peer, ZLinkLocationKeyCodec.encodePeerKey(key));
    return result;
  }

  async removeSpot(key: ZLinkSpotLocationKey, generation: bigint, signal?: AbortSignal): Promise<ZLinkLocationWriteResult> {
    const result = await this.guardWrite(() =>
      this.stores.spotStore.removeSpot(key, { ownerId: this.ownerId, generation }, signal));
    if (result.status === ZLinkLocationWriteStatus.Stored) {
      this.events?.spotRowRemoved(key);
    }
    this.notifyIfStale(result, ZLinkLocationKind.Spot, ZLinkLocationKeyCodec.encodeSpotKey(key));
    return result;
  }

  async removeActor(key: ZLinkActorLocationKey, generation: bigint, signal?: AbortSignal): Promise<ZLinkLocationWriteResult> {
    const result = await this.guardWrite(() =>
      this.stores.actorStore.removeActor(key, { ownerId: this.ownerId, generation }, signal));
    if (result.status === ZLinkLocationWriteStatus.Stored) {
      this.events?.actorRowRemoved(key);
    }
    this.notifyIfStale(result, ZLinkLocationKind.Actor, ZLinkLocationKeyCodec.encodeActorKey(key));
    return result;
  }

  async removeRoute(key: ZLinkRouteLocationKey, generation: bigint, signal?: AbortSignal): Promise<ZLinkLocationWriteResult> {
    const result = await this.guardWrite(() =>
      this.stores.routeStore.removeRoute(key, { ownerId: this.ownerId, generation }, signal));
    if (result.status === ZLinkLocationWriteStatus.Stored) {
      this.events?.routeRowRemoved(key);
    }
    this.notifyIfStale(result, ZLinkLocationKind.Route, ZLinkLocationKeyCodec.encodeRouteKey(key));
    return result;
  }

  async getStatus(): Promise<ZLinkLocationRuntimeStatus> {
    return {
      storeHealthy: this.lastError === undefined,
      watchEnabled: false,
      pollingIntervalMs: this.options.pollingIntervalMs,
      lastRefreshAt: this.ownerLeaseRenewedAt,
      lastError: this.lastError,
      ownerLeaseHealthy: this.ownerLeaseHealthy,
      ownerLeaseRenewedAt: this.ownerLeaseRenewedAt
    };
  }

  async listPeerLocations(filter: ZLinkPeerLocationFilter, signal?: AbortSignal): Promise<readonly ZLinkPeerLocation[]> {
    const rows = await this.stores.peerStore.listPeers(filter, signal);
    const live = await this.filterLive(rows, (row) => row.ownerId, signal);
    this.metrics?.change('zlink.location.peers', live.length - this.peerMetricCount);
    this.peerMetricCount = live.length;
    return live;
  }

  async listSpotLocations(
    filter: ZLinkSpotLocationFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkSpotLocation>> {
    const rows = await this.stores.spotStore.listSpots(filter, this.pageRequest(page), signal);
    return {
      items: await this.filterLive(rows.items, (row) => row.ownerId, signal),
      continuationToken: rows.continuationToken
    };
  }

  async listActorLocations(
    filter: ZLinkActorLocationFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkActorLocation>> {
    const rows = await this.stores.actorStore.listActors(filter, this.pageRequest(page), signal);
    return {
      items: await this.filterLive(rows.items, (row) => row.ownerId, signal),
      continuationToken: rows.continuationToken
    };
  }

  async listRouteLocations(
    filter: ZLinkRouteLocationFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkRouteLocation>> {
    const rows = await this.stores.routeStore.listRoutes(filter, this.pageRequest(page), signal);
    return {
      items: await this.filterLive(rows.items, (row) => row.ownerId, signal),
      continuationToken: rows.continuationToken
    };
  }

  async listTopology(
    filter: ZLinkLocationTopologyFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkLocationTopologyEntry>> {
    const [peers, spots, actors, routes] = await Promise.all([
      this.listPeerLocations({
        meshName: filter.meshName,
        role: filter.role,
        nodeRid: filter.nodeRid
      }, signal),
      this.listSpotLocations({
        meshName: filter.meshName,
        nodeRid: filter.nodeRid
      }, page, signal),
      this.listActorLocations({
        nodeRid: filter.nodeRid
      }, page, signal),
      this.listRouteLocations({
        ownerNodeRid: filter.nodeRid
      }, page, signal)
    ]);
    const entries: ZLinkLocationTopologyEntry[] = [];
    if (filter.kind === undefined || filter.kind === ZLinkLocationKind.Peer) {
      entries.push(...peers.map((peer) => ({
        kind: ZLinkLocationKind.Peer,
        meshName: peer.meshName,
        role: peer.role,
        nodeRid: peer.nodeRid,
        endpoint: peer.endpoint,
        state: ZLinkLocationTopologyState.Discovered,
        desiredCount: 1,
        readyCount: 0,
        errorCode: 0,
        updatedAt: peer.updatedAt
      })));
    }
    if (filter.kind === undefined || filter.kind === ZLinkLocationKind.Spot) {
      entries.push(...spots.items.map((spot) => ({
        kind: ZLinkLocationKind.Spot,
        meshName: spot.meshName,
        nodeRid: spot.nodeRid,
        spotRid: spot.spotRid,
        state: ZLinkLocationTopologyState.Discovered,
        desiredCount: 1,
        readyCount: 0,
        errorCode: 0,
        updatedAt: spot.updatedAt
      })));
    }
    if (filter.kind === undefined || filter.kind === ZLinkLocationKind.Actor) {
      entries.push(...actors.items.map((actor) => ({
        kind: ZLinkLocationKind.Actor,
        meshName: actor.spotMeshName,
        nodeRid: actor.nodeRid,
        spotRid: actor.spotRid,
        actorId: actor.actorId,
        state: ZLinkLocationTopologyState.Discovered,
        desiredCount: 1,
        readyCount: 0,
        errorCode: 0,
        updatedAt: actor.updatedAt
      })));
    }
    if (filter.kind === undefined || filter.kind === ZLinkLocationKind.Route) {
      entries.push(...routes.items.map((route) => ({
        kind: ZLinkLocationKind.Route,
        nodeRid: route.ownerNodeRid,
        state: ZLinkLocationTopologyState.Discovered,
        desiredCount: 1,
        readyCount: 0,
        errorCode: 0,
        updatedAt: route.updatedAt
      })));
    }
    const filtered = filter.state === undefined
      ? entries
      : entries.filter((entry) => entry.state === filter.state);
    return { items: filtered, continuationToken: spots.continuationToken ?? actors.continuationToken ?? routes.continuationToken };
  }

  private pageRequest(page: ZLinkPageRequest | undefined): ZLinkPageRequest {
    return page?.pageSize === undefined
      ? { ...page, pageSize: this.options.listPageSize }
      : page;
  }

  async listServiceSummaries(
    filter: ZLinkLocationServiceSummaryFilter,
    signal?: AbortSignal
  ): Promise<readonly ZLinkLocationServiceSummary[]> {
    const peers = await this.listPeerLocations({
      autoConnectType: filter.autoConnectType,
      meshName: filter.meshName,
      role: filter.role
    }, signal);
    const summaries = new Map<string, ZLinkLocationServiceSummary>();
    for (const peer of peers) {
      const key = `${peer.meshName}\n${peer.autoConnectType}\n${peer.role}`;
      const current = summaries.get(key);
      summaries.set(key, {
        meshName: peer.meshName,
        autoConnectType: peer.autoConnectType,
        role: peer.role,
        totalCount: (current?.totalCount ?? 0) + 1,
        readyCount: current?.readyCount ?? 0,
        errorCount: current?.errorCount ?? 0,
        stoppedCount: current?.stoppedCount ?? 0,
        updatedAt: current === undefined || peer.updatedAt > current.updatedAt ? peer.updatedAt : current.updatedAt
      });
    }
    return [...summaries.values()];
  }

  private async filterLive<TRow>(
    rows: readonly TRow[],
    ownerIdOf: (row: TRow) => string,
    signal?: AbortSignal
  ): Promise<TRow[]> {
    return await this.liveRows.filter(rows, ownerIdOf, signal);
  }

  private scheduleHeartbeat(): void {
    if (!this.started) {
      return;
    }
    this.nextLeaseRenewAtMs = this.now().getTime() + this.options.heartbeatIntervalMs;
    this.heartbeatTimer = this.setTimer(() => {
      this.heartbeatTimer = undefined;
      const expectedAt = this.nextLeaseRenewAtMs;
      if (expectedAt !== undefined) {
        this.metrics?.duration(
          'zlink.location.owner_lease.renew.lateness',
          Math.max(0, this.now().getTime() - expectedAt) / 1000
        );
      }
      void this.renewOwnerLeaseOnce().finally(() => this.scheduleHeartbeat());
    }, this.options.heartbeatIntervalMs);
  }

  private async guardWrite(write: () => Promise<ZLinkLocationWriteResult>): Promise<ZLinkLocationWriteResult> {
    try {
      return await write();
    } catch (error) {
      this.recordFailure(errorMessage(error));
      throw error;
    }
  }

  private notifyIfStale(result: ZLinkLocationWriteResult, kind: ZLinkLocationKind, key: string): void {
    if (result.status === ZLinkLocationWriteStatus.IgnoredStale || result.status === ZLinkLocationWriteStatus.RejectedConflict) {
      this.metrics?.count('zlink.location.write.conflicts');
    }
    if (result.status !== ZLinkLocationWriteStatus.IgnoredStale) {
      return;
    }
    for (const handler of this.ownershipLostHandlers) {
      handler({ kind, key });
    }
  }

  private recordFailure(message: string): void {
    this.metrics?.count('zlink.location.store.errors');
    this.ownerLeaseHealthy = false;
    this.lastError = message;
  }
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
