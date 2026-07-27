import {
  zlinkRuntimeDefaultLocationOptions,
  type ZLinkLocationOptionOverrides
} from '../../contracts/Locations/Options';
import { randomUUID } from 'node:crypto';
import type { RoutingId } from '../../contracts/Common';
import {
  ZLinkLocationAutoConnectType,
  ZLinkFrameworkRuntimeState,
  ZLinkLocationKind,
  ZLinkLocationTopologyState,
  ZLinkLocationWriteIntent,
  ZLinkLocationWriteStatus,
  type ZLinkLocationRuntimeQuery,
  type ZLinkLocationOwnerToken,
  type ZLinkOwnerLeaseRenewed,
  type ZLinkActorLocation,
  type ZLinkActorLocationFilter,
  type ZLinkActorLocationKey,
  type ZLinkLocationKey,
  type ZLinkLocationPage,
  type ZLinkLocationRuntimeStatus,
  type ZLinkLocationServiceSummary,
  type ZLinkLocationServiceSummaryFilter,
  type ZLinkMeshNodeDescriptor,
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
} from './internal-location-contracts';
import type {
  ZLinkActorLocationStore,
  ZLinkActorLocationQueryStore,
  ZLinkAuthorityStore,
  ZLinkClientServerLocationStore,
  ZLinkFanoutLocationStore,
  ZLinkOwnerLeaseStore,
  ZLinkPeerLocationStore,
  ZLinkRouteLocationStore,
  ZLinkSpotLocationStore,
  ZLinkSpotLocationQueryStore
} from './internal-store-contracts';
import type { ZLinkDomainLocationStore } from './domain-store-contract';
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
  readonly locationStore: ZLinkDomainLocationStore;
  readonly authorityStore: ZLinkAuthorityStore;
  readonly clientServerStore?: ZLinkClientServerLocationStore;
  readonly fanoutStore?: ZLinkFanoutLocationStore;
  readonly peerStore: ZLinkPeerLocationStore;
  readonly spotStore: ZLinkSpotLocationStore;
  readonly actorStore: ZLinkActorLocationStore;
  readonly routeStore: ZLinkRouteLocationStore;
  readonly ownerLeaseStore: ZLinkOwnerLeaseStore;
}

export interface ZLinkLocationRuntimeOptions {
  readonly stores: ZLinkLocationRuntimeStores;
  readonly options?: ZLinkLocationOptionOverrides;
  readonly ownerId?: string;
  readonly events?: ZLinkLocationEventSink;
  readonly leaseTracker?: ZLinkOwnerLeaseTracker;
  readonly now?: () => Date;
  readonly monotonicNowMs?: () => number;
  readonly setTimer?: (callback: () => void, delayMs: number) => unknown;
  readonly clearTimer?: (handle: unknown) => void;
  readonly metrics?: import('../diagnostics').ZLinkRuntimeMetrics;
  readonly meshNames?: readonly string[];
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
    this.name = 'ZLinkOwnerCleanupError';
  }
}

export class ZLinkLocationRuntime implements ZLinkLocationRuntimeQuery {
  readonly ownerId: string;
  private readonly options: Required<ZLinkLocationOptionOverrides>;
  private readonly stores: ZLinkLocationRuntimeStores;
  private readonly events?: ZLinkLocationEventSink;
  private readonly setTimer: (callback: () => void, delayMs: number) => unknown;
  private readonly clearTimer: (handle: unknown) => void;
  private readonly monotonicNowMs: () => number;
  private readonly liveRows: ZLinkLiveRowFilter;
  private readonly ownershipLostHandlers = new Set<(event: ZLinkOwnershipLostEvent) => void>();
  private readonly ownerLeaseRenewedHandlers = new Set<(renewal: ZLinkOwnerLeaseRenewed) => void>();
  private readonly ownerLeaseRenewalFailedHandlers = new Set<() => void>();
  private heartbeatTimer: unknown;
  private heartbeatRenewal?: Promise<void>;
  private nodeRidValue?: RoutingId;
  private started = false;
  private ownerCleanupComplete = true;
  private readonly metrics?: import('../diagnostics').ZLinkRuntimeMetrics;
  private readonly meshNames: readonly string[];
  private peerMetricCount = 0;
  private nextLeaseRenewAtMs?: number;
  private ownerToken?: ZLinkLocationOwnerToken;

  ownerLeaseHealthy = false;
  ownerLeaseRenewedAt?: Date;
  lastError?: string;

  constructor(runtimeOptions: ZLinkLocationRuntimeOptions) {
    this.stores = runtimeOptions.stores;
    this.options = { ...zlinkRuntimeDefaultLocationOptions, ...runtimeOptions.options };
    this.ownerId = runtimeOptions.ownerId ?? randomUUID().replaceAll('-', '');
    this.events = runtimeOptions.events;
    this.metrics = runtimeOptions.metrics;
    this.meshNames = [...new Set(runtimeOptions.meshNames ?? [])];
    this.monotonicNowMs = runtimeOptions.monotonicNowMs ?? (() => performance.now());
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

  get currentOwnerToken(): ZLinkLocationOwnerToken | undefined {
    return this.ownerToken;
  }

  addOwnershipLostHandler(handler: (event: ZLinkOwnershipLostEvent) => void): void {
    this.ownershipLostHandlers.add(handler);
  }

  removeOwnershipLostHandler(handler: (event: ZLinkOwnershipLostEvent) => void): void {
    this.ownershipLostHandlers.delete(handler);
  }

  addOwnerLeaseRenewedHandler(handler: (renewal: ZLinkOwnerLeaseRenewed) => void): void {
    this.ownerLeaseRenewedHandlers.add(handler);
  }

  removeOwnerLeaseRenewedHandler(handler: (renewal: ZLinkOwnerLeaseRenewed) => void): void {
    this.ownerLeaseRenewedHandlers.delete(handler);
  }

  addOwnerLeaseRenewalFailedHandler(handler: () => void): void {
    this.ownerLeaseRenewalFailedHandlers.add(handler);
  }

  removeOwnerLeaseRenewalFailedHandler(handler: () => void): void {
    this.ownerLeaseRenewalFailedHandlers.delete(handler);
  }

  async start(nodeRid: RoutingId, signal?: AbortSignal): Promise<void> {
    if (this.started) {
      return;
    }

    this.started = true;
    this.ownerCleanupComplete = false;
    this.nodeRidValue = nodeRid;
    const claim = await withTimeout(
      (claimSignal) => this.stores.ownerLeaseStore.claimOwnerLease(
        this.ownerId,
        this.options.ownerLeaseTtlMs,
        claimSignal
      ),
      this.options.ownerLeaseRenewTimeoutMs,
      signal
    );
    if (claim.kind !== 'claimed') {
      this.started = false;
      this.ownerCleanupComplete = true;
      throw new Error(`Owner lease claim failed with '${claim.kind}'.`);
    }
    this.ownerToken = claim.token;
    this.ownerLeaseHealthy = true;
    this.ownerLeaseRenewedAt = claim.storeNow;
    this.nextLeaseRenewAtMs = this.monotonicNowMs() + this.options.heartbeatIntervalMs;
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
    this.nextLeaseRenewAtMs = undefined;
    await this.heartbeatRenewal;

    await this.cleanupOwner(signal);
  }

  async cleanupOwner(signal?: AbortSignal): Promise<void> {
    if (this.ownerCleanupComplete) {
      return;
    }
    try {
      const owner = this.ownerToken;
      if (owner !== undefined) {
        await this.stores.locationStore.removeAllByOwner(owner, signal);
        await this.stores.ownerLeaseStore.releaseOwnerLease(owner, signal);
        this.ownerToken = undefined;
      }
      this.ownerCleanupComplete = true;
    } catch (error) {
      this.recordFailure(errorMessage(error));
      throw new ZLinkOwnerCleanupError(error);
    }
  }

  async renewOwnerLeaseOnce(signal?: AbortSignal): Promise<boolean> {
    if (this.ownerToken === undefined) {
      this.recordFailure('Owner lease renew requires a claimed owner token.');
      return false;
    }

    try {
      const result = await withTimeout(
        (renewSignal) => this.stores.ownerLeaseStore.renewOwnerLease(
            this.ownerToken as ZLinkLocationOwnerToken,
            this.options.ownerLeaseTtlMs,
            renewSignal
        ),
        this.options.ownerLeaseRenewTimeoutMs,
        signal
      );
      if (result.kind !== 'renewed') {
        this.ownerLeaseHealthy = false;
        this.recordFailure('Owner lease token is stale.');
        for (const handler of this.ownerLeaseRenewalFailedHandlers) handler();
        return false;
      }
      this.ownerLeaseHealthy = true;
      this.ownerLeaseRenewedAt = result.storeNow;
      this.lastError = undefined;
      for (const handler of this.ownerLeaseRenewedHandlers) handler(result);
      return true;
    } catch (error) {
      if (signal?.aborted === true) throw error;
      this.metrics?.count('zlink.location.owner_lease.renew.failures');
      this.recordFailure(errorMessage(error));
      for (const handler of this.ownerLeaseRenewalFailedHandlers) handler();
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

  async writeMeshNode(
    descriptor: Omit<
      ZLinkMeshNodeDescriptor,
      'ownerId' | 'leaseGeneration' | 'updatedAt'
    >,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    const owner = this.ownerToken;
    if (owner === undefined) {
      throw new Error('MeshNode descriptor write requires a claimed owner token.');
    }
    const stamped: ZLinkMeshNodeDescriptor = {
      ...descriptor,
      ownerId: owner.ownerId,
      leaseGeneration: owner.leaseGeneration,
      updatedAt: new Date(0)
    };
    const result = await this.guardWrite(() =>
      this.stores.locationStore.updateMeshNode(stamped, intent, signal));
    this.notifyIfStale(
      result,
      ZLinkLocationKind.Peer,
      `${descriptor.meshName}\0${String(descriptor.rid)}`
    );
    return result;
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
    const owner = this.ownerToken;
    if (owner === undefined) throw new Error('Spot location write requires a claimed owner token.');
    const stamped = { ...spot, ownerId: owner.ownerId, leaseGeneration: owner.leaseGeneration };
    const result = await this.guardWrite(() => this.stores.spotStore.updateSpot(stamped, intent, signal));
    const key = { meshName: spot.meshName, spotId: spot.spotId };
    if (result.status === ZLinkLocationWriteStatus.Stored) {
      this.events?.spotRowUpdated(key, { ...stamped, updatedAt: result.updatedAt });
    }
    this.notifyIfStale(result, ZLinkLocationKind.Spot, ZLinkLocationKeyCodec.encodeSpotKey(key));
    return result;
  }

  async writeActor(
    actor: ZLinkActorLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    const owner = this.ownerToken;
    if (owner === undefined) throw new Error('Actor location write requires a claimed owner token.');
    const stamped = {
      ...actor,
      actorType: actor.actorType,
      ownerId: owner.ownerId,
      leaseGeneration: owner.leaseGeneration
    };
    const result = await this.guardWrite(() => this.stores.actorStore.updateActor(stamped, intent, signal));
    const key = { meshName: stamped.meshName, actorId: stamped.actorId };
    if (result.status === ZLinkLocationWriteStatus.Stored) {
      this.events?.actorRowUpdated(key, { ...stamped, updatedAt: result.updatedAt });
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
      this.stores.peerStore.removePeer(
        key, { ownerId: this.ownerId, leaseGeneration: generation }, signal));
    if (result.status === ZLinkLocationWriteStatus.Stored) {
      this.events?.peerRowRemoved({ kind: ZLinkLocationKind.Peer, key });
    }
    this.notifyIfStale(result, ZLinkLocationKind.Peer, ZLinkLocationKeyCodec.encodePeerKey(key));
    return result;
  }

  async removeSpot(key: ZLinkSpotLocationKey, generation: bigint, signal?: AbortSignal): Promise<ZLinkLocationWriteStatus> {
    const status = await this.guardStatusWrite(() =>
      this.stores.spotStore.removeSpot(
        key, { ownerId: this.ownerId, leaseGeneration: generation }, signal));
    if (status === ZLinkLocationWriteStatus.Stored) {
      this.events?.spotRowRemoved(key);
    }
    this.notifyIfStaleStatus(status, ZLinkLocationKind.Spot, ZLinkLocationKeyCodec.encodeSpotKey(key));
    return status;
  }

  async removeActor(key: ZLinkActorLocationKey, generation: bigint, signal?: AbortSignal): Promise<ZLinkLocationWriteStatus> {
    const status = await this.guardStatusWrite(() =>
      this.stores.actorStore.removeActor(
        key, { ownerId: this.ownerId, leaseGeneration: generation }, signal));
    if (status === ZLinkLocationWriteStatus.Stored) {
      this.events?.actorRowRemoved(key);
    }
    this.notifyIfStaleStatus(status, ZLinkLocationKind.Actor, ZLinkLocationKeyCodec.encodeActorKey(key));
    return status;
  }

  async removeRoute(key: ZLinkRouteLocationKey, generation: bigint, signal?: AbortSignal): Promise<ZLinkLocationWriteResult> {
    const result = await this.guardWrite(() =>
      this.stores.routeStore.removeRoute(
        key, { ownerId: this.ownerId, leaseGeneration: generation }, signal));
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

  reportDiscoveryFailure(error: unknown): void {
    this.metrics?.count('zlink.location.discovery.failures');
    this.recordFailure(errorMessage(error));
  }

  async listPeerLocations(filter: ZLinkPeerLocationFilter, signal?: AbortSignal): Promise<readonly ZLinkPeerLocation[]> {
    const rows = await this.stores.peerStore.listPeers(filter, signal);
    const live = await this.filterLive(rows, (row) => row.ownerId, signal);
    this.metrics?.change('zlink.location.peers', live.length - this.peerMetricCount);
    this.peerMetricCount = live.length;
    return live;
  }

  async listLiveMeshNodes(
    meshName: string,
    signal?: AbortSignal
  ): Promise<readonly ZLinkMeshNodeDescriptor[]> {
    const rows = (await this.stores.locationStore.listMeshNodes(meshName, undefined, signal)).items;
    return this.filterLive(rows, (row) => row.ownerId, signal);
  }

  async listMeshNodeDescriptors(
    meshName: string,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkMeshNodeDescriptor>> {
    const rows = await this.stores.locationStore.listMeshNodes(meshName, this.pageRequest(page), signal);
    return {
      items: await this.filterLive(rows.items, (row) => row.ownerId, signal),
      continuationToken: rows.continuationToken
    };
  }

  async listSpotLocations(
    filter: ZLinkSpotLocationFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkSpotLocation>> {
    const queryStore = this.stores.spotStore as ZLinkSpotLocationStore
      & Partial<ZLinkSpotLocationQueryStore>;
    if (queryStore.listSpots === undefined) {
      throw new Error('The configured Spot location store does not provide operational list queries.');
    }
    const rows = await queryStore.listSpots(filter, this.pageRequest(page), signal);
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
    const queryStore = this.stores.actorStore as ZLinkActorLocationStore
      & Partial<ZLinkActorLocationQueryStore>;
    if (queryStore.listActors === undefined) {
      throw new Error('The configured Actor location store does not provide operational list queries.');
    }
    const rows = await queryStore.listActors(filter, this.pageRequest(page), signal);
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
    const entries: ZLinkLocationTopologyEntry[] = [];
    for (const meshName of this.meshNamesOf(filter.meshName)) {
      for (const descriptor of await this.listAllMeshNodeDescriptors(meshName, signal)) {
        const live = (await this.filterLive([descriptor], row => row.ownerId, signal)).length > 0;
        const entry: ZLinkLocationTopologyEntry = {
          meshName: descriptor.meshName,
          nodeRid: descriptor.rid,
          endpoint: descriptor.endpoint,
          draining: descriptor.state === ZLinkFrameworkRuntimeState.Draining,
          state: live ? ZLinkLocationTopologyState.Ready : ZLinkLocationTopologyState.Lost,
          updatedAt: descriptor.updatedAt
        };
        if ((filter.nodeRid === undefined || entry.nodeRid === filter.nodeRid)
          && (filter.state === undefined || entry.state === filter.state)) {
          entries.push(entry);
        }
      }
    }
    return this.pageInMemory(entries, page);
  }

  private pageRequest(page: ZLinkPageRequest | undefined): ZLinkPageRequest {
    return page?.pageSize === undefined
      ? { ...page, pageSize: this.options.listPageSize }
      : page;
  }

  async listServiceSummaries(
    filter: ZLinkLocationServiceSummaryFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkLocationServiceSummary>> {
    const summaries: ZLinkLocationServiceSummary[] = [];
    for (const meshName of this.meshNamesOf(filter.meshName)) {
      const descriptors = await this.listAllMeshNodeDescriptors(meshName, signal);
      if (descriptors.length === 0) continue;
      const live = await this.filterLive(descriptors, row => row.ownerId, signal);
      const liveOwners = new Set(live.map(row => row.ownerId));
      summaries.push({
        meshName,
        totalCount: descriptors.length,
        readyCount: descriptors.filter(row => liveOwners.has(row.ownerId)).length,
        errorCount: 0,
        stoppedCount: descriptors.filter(row => !liveOwners.has(row.ownerId)).length,
        lastUpdatedAt: descriptors.reduce(
          (latest, row) => row.updatedAt > latest ? row.updatedAt : latest,
          descriptors[0].updatedAt
        )
      });
    }
    return this.pageInMemory(summaries, page);
  }

  private meshNamesOf(meshName: string | undefined): readonly string[] {
    return meshName === undefined ? this.meshNames : [meshName];
  }

  private async listAllMeshNodeDescriptors(
    meshName: string,
    signal?: AbortSignal
  ): Promise<readonly ZLinkMeshNodeDescriptor[]> {
    const rows: ZLinkMeshNodeDescriptor[] = [];
    let continuationToken: string | undefined;
    do {
      const result = await this.stores.locationStore.listMeshNodes(
        meshName,
        { pageSize: this.options.listPageSize, continuationToken },
        signal
      );
      rows.push(...result.items);
      continuationToken = result.continuationToken;
    } while (continuationToken !== undefined);
    return rows;
  }

  private pageInMemory<T>(entries: readonly T[], page: ZLinkPageRequest | undefined): ZLinkLocationPage<T> {
    const normalized = this.pageRequest(page);
    const parsedOffset = Number.parseInt(normalized.continuationToken ?? '0', 10);
    const offset = Number.isFinite(parsedOffset) && parsedOffset >= 0 ? parsedOffset : 0;
    const pageSize = normalized.pageSize ?? this.options.listPageSize;
    const items = entries.slice(offset, offset + pageSize);
    const nextOffset = offset + items.length;
    return {
      items,
      continuationToken: nextOffset < entries.length ? String(nextOffset) : undefined
    };
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
    const scheduledAt = this.nextLeaseRenewAtMs
      ?? this.monotonicNowMs() + this.options.heartbeatIntervalMs;
    const delayMs = Math.max(0, scheduledAt - this.monotonicNowMs());
    this.heartbeatTimer = this.setTimer(() => {
      this.heartbeatTimer = undefined;
      this.metrics?.duration(
        'zlink.location.owner_lease.renew.lateness',
        Math.max(0, this.monotonicNowMs() - scheduledAt) / 1000
      );
      this.nextLeaseRenewAtMs = scheduledAt + this.options.heartbeatIntervalMs;
      const renewal = this.renewOwnerLeaseOnce().then(
        () => undefined,
        () => undefined
      ).finally(() => {
        if (this.heartbeatRenewal === renewal) {
          this.heartbeatRenewal = undefined;
        }
        this.scheduleHeartbeat();
      });
      this.heartbeatRenewal = renewal;
      void renewal;
    }, delayMs);
  }

  private async guardWrite(write: () => Promise<ZLinkLocationWriteResult>): Promise<ZLinkLocationWriteResult> {
    try {
      return await write();
    } catch (error) {
      this.recordFailure(errorMessage(error));
      throw error;
    }
  }

  private async guardStatusWrite(
    write: () => Promise<ZLinkLocationWriteStatus>
  ): Promise<ZLinkLocationWriteStatus> {
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

  private notifyIfStaleStatus(status: ZLinkLocationWriteStatus, kind: ZLinkLocationKind, key: string): void {
    if (status === ZLinkLocationWriteStatus.IgnoredStale || status === ZLinkLocationWriteStatus.RejectedConflict) {
      this.metrics?.count('zlink.location.write.conflicts');
    }
    if (status !== ZLinkLocationWriteStatus.IgnoredStale) {
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

async function withTimeout<T>(
  action: (signal: AbortSignal) => Promise<T>,
  timeoutMs: number,
  signal?: AbortSignal
): Promise<T> {
  const timeoutController = new AbortController();
  const operationSignal = signal === undefined
    ? timeoutController.signal
    : AbortSignal.any([signal, timeoutController.signal]);
  let timeout: ReturnType<typeof setTimeout> | undefined;
  const deadline = new Promise<never>((_resolve, reject) => {
    timeout = setTimeout(() => {
      const error = new Error(`Owner lease renewal exceeded ${timeoutMs}ms.`);
      timeoutController.abort(error);
      reject(error);
    }, timeoutMs);
    timeout.unref();
  });
  try {
    return await Promise.race([action(operationSignal), deadline]);
  } finally {
    if (timeout !== undefined) clearTimeout(timeout);
  }
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
