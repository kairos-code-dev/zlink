import { randomUUID } from 'node:crypto';
import type { ActorRef, RoutingId, SpotRef } from '../../contracts/Common';
import {
  ZLinkRouteKind,
  ZLinkLocationKind,
  ZLinkLocationAutoConnectType,
  ZLinkLocationRole,
  ZLinkLocationWriteIntent,
  ZLinkLocationWriteStatus,
  zlinkDefaultLocationOptions,
  type IZLinkLocationRuntimeQuery,
  type IZLinkLocationReadiness,
  type IZLinkActorAddressResolver,
  type IZLinkPeerLocationResolver,
  type ZLinkSpotRefResolver,
  type IZLinkActorLocationStore,
  type IZLinkLocationChangeStampStore,
  type IZLinkLocationWatchStore,
  type IZLinkLocationStore,
  type IZLinkOwnerLeaseStore,
  type IZLinkPeerLocationStore,
  type IZLinkRouteLocationStore,
  type IZLinkSpotLocationStore,
  type ZLinkActorLocationKey,
  type ZLinkActorLocation,
  type ZLinkActorLocationFilter,
  type ZLinkLocationChangeStampScope,
  type ZLinkLocationRuntimeStatus,
  type ZLinkLocationServiceSummary,
  type ZLinkLocationServiceSummaryFilter,
  ZLinkLocationTopologyState,
  type ZLinkLocationKey,
  type ZLinkLocationTopologyEntry,
  type ZLinkLocationTopologyFilter,
  type ZLinkLocationOwnerToken,
  type ZLinkLocationPage,
  type ZLinkLocationWriteResult,
  type ZLinkOwnerLease,
  type ZLinkOwnerLeaseRenewal,
  type ZLinkOwnerLeaseSnapshot,
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
import type { ZLinkLocationOptions } from '../../contracts/Locations';
import {
  ZLinkSpotKind,
} from '../../contracts/Spots';
import type {
  ZLinkSpotRouteTarget,
  ZLinkSpotRouteResolver
} from '../spots/spot-routing-internal';
import { ZLinkFrameworkErrorKind, ZLinkFrameworkException } from '../../contracts/Errors';
import { zlinkLocationAutoConnectTypeName, zlinkLocationRoleName } from './canonical-codec';
import { ZLinkLocationKeyCodec } from './key-codec';

function encodeRoutingIdHex(routingId: RoutingId): string {
  const value = routingId as unknown as { toHex?: () => string };
  if (typeof value.toHex === 'function') {
    return value.toHex.call(routingId).toLowerCase();
  }

  if (typeof routingId !== 'string') {
    throw new TypeError('RoutingId must be a string or expose toHex().');
  }

  return Buffer.from(routingId, 'utf8').toString('hex');
}

export class ZLinkInMemoryLocationStore implements IZLinkLocationStore, IZLinkLocationChangeStampStore {
  private readonly leases = new Map<string, ZLinkOwnerLease>();
  private readonly peers = new RowTable<ZLinkPeerLocation>();
  private readonly spots = new RowTable<ZLinkSpotLocation>();
  private readonly actors = new RowTable<ZLinkActorLocation>();
  private readonly routes = new RowTable<ZLinkRouteLocation>();
  private readonly stamps = new Map<string, bigint>();

  constructor(private readonly now: () => Date = () => new Date()) {}

  async updatePeer(
    peer: ZLinkPeerLocation,
    intent: ZLinkLocationWriteIntent
  ): Promise<ZLinkLocationWriteResult> {
    return this.write(
      this.peers,
      ZLinkLocationKeyCodec.encodePeerKey({
        autoConnectType: peer.autoConnectType,
        meshName: peer.meshName,
        role: peer.role,
        nodeRid: peer.nodeRid,
        endpoint: peer.endpoint
      }),
      peer,
      intent,
      peer.ownerId,
      peer.generation,
      (row) => row.ownerId,
      (row) => row.generation,
      (row, generation, updatedAt) => ({ ...row, generation, updatedAt }),
      ZLinkLocationKind.Peer,
      peer.meshName
    );
  }

  async removePeer(key: ZLinkPeerLocationKey, owner: ZLinkLocationOwnerToken): Promise<ZLinkLocationWriteResult> {
    return this.remove(
      this.peers,
      ZLinkLocationKeyCodec.encodePeerKey(key),
      owner,
      (row) => row.ownerId,
      (row) => row.generation,
      ZLinkLocationKind.Peer,
      key.meshName
    );
  }

  async listPeers(filter: ZLinkPeerLocationFilter): Promise<readonly ZLinkPeerLocation[]> {
    return [...this.peers.rows.values()].filter((row) => matchesPeer(row, filter));
  }

  async updateSpot(
    spot: ZLinkSpotLocation,
    intent: ZLinkLocationWriteIntent
  ): Promise<ZLinkLocationWriteResult> {
    return this.write(
      this.spots,
      ZLinkLocationKeyCodec.encodeSpotKey({ meshName: spot.meshName, spotRid: spot.spotRid }),
      spot,
      intent,
      spot.ownerId,
      spot.generation,
      (row) => row.ownerId,
      (row) => row.generation,
      (row, generation, updatedAt) => ({ ...row, generation, updatedAt }),
      ZLinkLocationKind.Spot,
      spot.meshName
    );
  }

  async removeSpot(key: ZLinkSpotLocationKey, owner: ZLinkLocationOwnerToken): Promise<ZLinkLocationWriteResult> {
    return this.remove(
      this.spots,
      ZLinkLocationKeyCodec.encodeSpotKey(key),
      owner,
      (row) => row.ownerId,
      (row) => row.generation,
      ZLinkLocationKind.Spot,
      key.meshName
    );
  }

  async resolveSpot(key: ZLinkSpotLocationKey): Promise<ZLinkSpotLocation | undefined> {
    return this.spots.rows.get(ZLinkLocationKeyCodec.encodeSpotKey(key));
  }

  async listSpots(
    filter: ZLinkSpotLocationFilter,
    page: ZLinkPageRequest = {}
  ): Promise<ZLinkLocationPage<ZLinkSpotLocation>> {
    return pageRows(this.spots, (row) => matchesSpot(row, filter), page);
  }

  async updateActor(
    actor: ZLinkActorLocation,
    intent: ZLinkLocationWriteIntent
  ): Promise<ZLinkLocationWriteResult> {
    return this.write(
      this.actors,
      ZLinkLocationKeyCodec.encodeActorKey({ actorId: actor.actorId }),
      actor,
      intent,
      actor.ownerId,
      actor.generation,
      (row) => row.ownerId,
      (row) => row.generation,
      (row, generation, updatedAt) => ({ ...row, generation, updatedAt }),
      ZLinkLocationKind.Actor,
      undefined
    );
  }

  async removeActor(key: ZLinkActorLocationKey, owner: ZLinkLocationOwnerToken): Promise<ZLinkLocationWriteResult> {
    return this.remove(
      this.actors,
      ZLinkLocationKeyCodec.encodeActorKey(key),
      owner,
      (row) => row.ownerId,
      (row) => row.generation,
      ZLinkLocationKind.Actor,
      undefined
    );
  }

  async resolveActor(key: ZLinkActorLocationKey): Promise<ZLinkActorLocation | undefined> {
    return this.actors.rows.get(ZLinkLocationKeyCodec.encodeActorKey(key));
  }

  async listActors(
    filter: ZLinkActorLocationFilter,
    page: ZLinkPageRequest = {}
  ): Promise<ZLinkLocationPage<ZLinkActorLocation>> {
    return pageRows(this.actors, (row) => matchesActor(row, filter), page);
  }

  async updateRoute(
    route: ZLinkRouteLocation,
    intent: ZLinkLocationWriteIntent
  ): Promise<ZLinkLocationWriteResult> {
    return this.write(
      this.routes,
      ZLinkLocationKeyCodec.encodeRouteKey({ routeKind: route.routeKind, routeKey: route.routeKey }),
      route,
      intent,
      route.ownerId,
      route.generation,
      (row) => row.ownerId,
      (row) => row.generation,
      (row, generation, updatedAt) => ({ ...row, generation, updatedAt }),
      ZLinkLocationKind.Route,
      undefined
    );
  }

  async removeRoute(key: ZLinkRouteLocationKey, owner: ZLinkLocationOwnerToken): Promise<ZLinkLocationWriteResult> {
    return this.remove(
      this.routes,
      ZLinkLocationKeyCodec.encodeRouteKey(key),
      owner,
      (row) => row.ownerId,
      (row) => row.generation,
      ZLinkLocationKind.Route,
      undefined
    );
  }

  async resolveRoute(key: ZLinkRouteLocationKey): Promise<ZLinkRouteLocation | undefined> {
    return this.routes.rows.get(ZLinkLocationKeyCodec.encodeRouteKey(key));
  }

  async listRoutes(
    filter: ZLinkRouteLocationFilter,
    page: ZLinkPageRequest = {}
  ): Promise<ZLinkLocationPage<ZLinkRouteLocation>> {
    return pageRows(this.routes, (row) => matchesRoute(row, filter), page);
  }

  async renewOwnerLease(
    ownerId: string,
    nodeRid: RoutingId,
    leaseTtlMs: number
  ): Promise<ZLinkOwnerLeaseRenewal> {
    const updatedAt = this.now();
    const leaseExpiresAt = new Date(updatedAt.getTime() + leaseTtlMs);
    this.leases.set(ownerId, {
      ownerId,
      nodeRid,
      leaseExpiresAt,
      updatedAt
    });
    return { leaseExpiresAt, storeNow: updatedAt };
  }

  async removeOwnerLease(ownerId: string): Promise<boolean> {
    return this.leases.delete(ownerId);
  }

  async removeAllByOwner(ownerId: string): Promise<number> {
    let removed = 0;
    removed += this.removeByOwner(this.peers, ownerId, (row) => row.ownerId, ZLinkLocationKind.Peer, (row) => row.meshName);
    removed += this.removeByOwner(this.spots, ownerId, (row) => row.ownerId, ZLinkLocationKind.Spot, (row) => row.meshName);
    removed += this.removeByOwner(this.actors, ownerId, (row) => row.ownerId, ZLinkLocationKind.Actor, () => undefined);
    removed += this.removeByOwner(this.routes, ownerId, (row) => row.ownerId, ZLinkLocationKind.Route, () => undefined);
    return removed;
  }

  async listOwnerLeases(): Promise<ZLinkOwnerLeaseSnapshot> {
    return {
      leases: [...this.leases.values()],
      storeNow: this.now()
    };
  }

  async getChangeStamp(scope: ZLinkLocationChangeStampScope): Promise<bigint> {
    return this.stamps.get(stampKey(scope)) ?? 0n;
  }

  private write<TRow>(
    table: RowTable<TRow>,
    key: string,
    row: TRow,
    intent: ZLinkLocationWriteIntent,
    ownerId: string,
    generation: bigint,
    ownerOf: (row: TRow) => string,
    generationOf: (row: TRow) => bigint,
    finalize: (row: TRow, generation: bigint, updatedAt: Date) => TRow,
    kind: ZLinkLocationKind,
    meshName: string | undefined
  ): ZLinkLocationWriteResult {
    const updatedAt = this.now();
    const current = table.rows.get(key);

    if (intent === ZLinkLocationWriteIntent.NewClaim
      && current !== undefined
      && this.isOwnerLive(ownerOf(current), updatedAt)) {
      return rejectedConflict();
    }

    if (intent === ZLinkLocationWriteIntent.NewClaim || intent === ZLinkLocationWriteIntent.Takeover) {
      const next = (table.generations.get(key) ?? 0n) + 1n;
      table.generations.set(key, next);
      table.rows.set(key, finalize(row, next, updatedAt));
      this.bump(kind, meshName);
      return stored(next, updatedAt);
    }

    if (current !== undefined
      && ownerOf(current) === ownerId
      && generationOf(current) === generation) {
      table.rows.set(key, finalize(row, generation, updatedAt));
      this.bump(kind, meshName);
      return stored(generation, updatedAt);
    }

    return ignoredStale();
  }

  private remove<TRow>(
    table: RowTable<TRow>,
    key: string,
    owner: ZLinkLocationOwnerToken,
    ownerOf: (row: TRow) => string,
    generationOf: (row: TRow) => bigint,
    kind: ZLinkLocationKind,
    meshName: string | undefined
  ): ZLinkLocationWriteResult {
    const current = table.rows.get(key);
    if (current === undefined || ownerOf(current) !== owner.ownerId || generationOf(current) !== owner.generation) {
      return ignoredStale();
    }

    table.rows.delete(key);
    this.bump(kind, meshName);
    return stored(owner.generation, this.now());
  }

  private removeByOwner<TRow>(
    table: RowTable<TRow>,
    ownerId: string,
    ownerOf: (row: TRow) => string,
    kind: ZLinkLocationKind,
    meshOf: (row: TRow) => string | undefined
  ): number {
    let removed = 0;
    for (const [key, row] of [...table.rows.entries()]) {
      if (ownerOf(row) === ownerId) {
        table.rows.delete(key);
        this.bump(kind, meshOf(row));
        removed++;
      }
    }
    return removed;
  }

  private isOwnerLive(ownerId: string, now: Date): boolean {
    const lease = this.leases.get(ownerId);
    return lease !== undefined && lease.leaseExpiresAt.getTime() > now.getTime();
  }

  private bump(kind: ZLinkLocationKind, meshName: string | undefined): void {
    this.bumpScope({ kind, meshName });
    if (meshName !== undefined) {
      this.bumpScope({ kind });
    }
  }

  private bumpScope(scope: ZLinkLocationChangeStampScope): void {
    const key = stampKey(scope);
    this.stamps.set(key, (this.stamps.get(key) ?? 0n) + 1n);
  }
}

export interface ZLinkLocationRuntimeStores {
  readonly locationStore: IZLinkLocationStore;
  readonly peerStore: IZLinkPeerLocationStore;
  readonly spotStore: IZLinkSpotLocationStore;
  readonly actorStore: IZLinkActorLocationStore;
  readonly routeStore: IZLinkRouteLocationStore;
  readonly ownerLeaseStore: IZLinkOwnerLeaseStore;
}

export interface ZLinkLocationRuntimeOptions {
  readonly stores: ZLinkLocationRuntimeStores;
  readonly options?: ZLinkLocationOptions;
  readonly ownerId?: string;
  readonly events?: ZLinkLocationEventSink;
  readonly now?: () => Date;
  readonly setTimer?: (callback: () => void, delayMs: number) => unknown;
  readonly clearTimer?: (handle: unknown) => void;
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

export interface ZLinkOwnershipLostEvent {
  readonly kind: ZLinkLocationKind;
  readonly key: string;
}

export class ZLinkLocationRuntime implements IZLinkLocationRuntimeQuery {
  readonly ownerId: string;
  private readonly options: Required<ZLinkLocationOptions>;
  private readonly stores: ZLinkLocationRuntimeStores;
  private readonly events?: ZLinkLocationEventSink;
  private readonly now: () => Date;
  private readonly setTimer: (callback: () => void, delayMs: number) => unknown;
  private readonly clearTimer: (handle: unknown) => void;
  private readonly queryLeaseTracker: ZLinkOwnerLeaseTracker;
  private readonly ownershipLostHandlers = new Set<(event: ZLinkOwnershipLostEvent) => void>();
  private heartbeatTimer: unknown;
  private nodeRidValue?: RoutingId;
  private started = false;

  ownerLeaseHealthy = false;
  ownerLeaseRenewedAt?: Date;
  lastError?: string;

  constructor(runtimeOptions: ZLinkLocationRuntimeOptions) {
    this.stores = runtimeOptions.stores;
    this.options = { ...zlinkDefaultLocationOptions, ...runtimeOptions.options };
    this.ownerId = runtimeOptions.ownerId ?? randomUUID().replaceAll('-', '');
    this.events = runtimeOptions.events;
    this.now = runtimeOptions.now ?? (() => new Date());
    this.setTimer = runtimeOptions.setTimer ?? ((callback, delayMs) => setTimeout(callback, delayMs));
    this.clearTimer = runtimeOptions.clearTimer ?? ((handle) => clearTimeout(handle as NodeJS.Timeout));
    this.queryLeaseTracker = new ZLinkOwnerLeaseTracker({
      store: this.stores.ownerLeaseStore,
      options: this.options
    });
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
    this.nodeRidValue = nodeRid;
    await this.renewOwnerLeaseOnce(signal);
    this.scheduleHeartbeat();
  }

  async stop(signal?: AbortSignal): Promise<void> {
    if (!this.started) {
      return;
    }

    this.started = false;
    if (this.heartbeatTimer !== undefined) {
      this.clearTimer(this.heartbeatTimer);
      this.heartbeatTimer = undefined;
    }

    try {
      await this.stores.ownerLeaseStore.removeOwnerLease(this.ownerId, signal);
      await this.stores.locationStore.removeAllByOwner(this.ownerId, signal);
    } catch (error) {
      this.recordFailure(errorMessage(error));
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
    return await this.filterLive(rows, (row) => row.ownerId, signal);
  }

  async listSpotLocations(
    filter: ZLinkSpotLocationFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkSpotLocation>> {
    const rows = await this.stores.spotStore.listSpots(filter, page, signal);
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
    const rows = await this.stores.actorStore.listActors(filter, page, signal);
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
    const rows = await this.stores.routeStore.listRoutes(filter, page, signal);
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
    const live: TRow[] = [];
    for (const row of rows) {
      if (await this.queryLeaseTracker.isOwnerLive(ownerIdOf(row), signal)) {
        live.push(row);
      }
    }
    return live;
  }

  private scheduleHeartbeat(): void {
    if (!this.started) {
      return;
    }
    this.heartbeatTimer = this.setTimer(() => {
      this.heartbeatTimer = undefined;
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
    if (result.status !== ZLinkLocationWriteStatus.IgnoredStale) {
      return;
    }
    for (const handler of this.ownershipLostHandlers) {
      handler({ kind, key });
    }
  }

  private recordFailure(message: string): void {
    this.ownerLeaseHealthy = false;
    this.lastError = message;
  }
}

export interface ZLinkAutoConnectLocal {
  readonly autoConnectType: ZLinkLocationAutoConnectType;
  readonly meshName: string;
  readonly role: ZLinkLocationRole;
  readonly nodeRid?: RoutingId;
  readonly endpoint: string;
}

export interface ZLinkAutoConnectTarget {
  readonly targetKey: string;
  readonly nodeRid?: RoutingId;
  readonly role: ZLinkLocationRole;
  readonly endpoint: string;
  readonly metadata?: Readonly<Record<string, string>>;
  readonly ownerId?: string;
}

export interface IZLinkAutoConnectExecutor {
  connect(target: ZLinkAutoConnectTarget): boolean;
  disconnect(target: ZLinkAutoConnectTarget): void;
}

export const ZLinkAutoConnectPlanner = Object.freeze({
  isRoleAllowed(type: ZLinkLocationAutoConnectType, role: ZLinkLocationRole): boolean {
    switch (type) {
      case ZLinkLocationAutoConnectType.RouteMesh:
        return role === ZLinkLocationRole.Router;
      case ZLinkLocationAutoConnectType.ClientServer:
        return role === ZLinkLocationRole.Router || role === ZLinkLocationRole.Dealer;
      case ZLinkLocationAutoConnectType.DealerMesh:
        return role === ZLinkLocationRole.Dealer;
      case ZLinkLocationAutoConnectType.Fanout:
        return role === ZLinkLocationRole.Pub || role === ZLinkLocationRole.Sub;
      case ZLinkLocationAutoConnectType.SpotMesh:
        return role === ZLinkLocationRole.Spot || role === ZLinkLocationRole.Router;
      default:
        return false;
    }
  },

  computeDesired(
    local: ZLinkAutoConnectLocal,
    peers: readonly ZLinkPeerLocation[]
  ): ReadonlyMap<string, ZLinkAutoConnectTarget> {
    const desired = new Map<string, ZLinkAutoConnectTarget>();
    for (const peer of peers) {
      if (peer.autoConnectType !== local.autoConnectType
        || peer.meshName !== local.meshName
        || !this.isRoleAllowed(peer.autoConnectType, peer.role)
        || peer.endpoint.length === 0
        || isAutoConnectSelf(local, peer)
        || !shouldDialAutoConnectPeer(local, peer)) {
        continue;
      }

      const target: ZLinkAutoConnectTarget = {
        targetKey: autoConnectTargetKeyOf(peer),
        nodeRid: peer.nodeRid,
        role: peer.role,
        endpoint: peer.endpoint,
        metadata: peer.metadata,
        ownerId: peer.ownerId
      };
      desired.set(target.targetKey, target);
    }
    return desired;
  },

  targetKeyOf(peer: ZLinkPeerLocation): string {
    return autoConnectTargetKeyOf(peer);
  }
});

export interface ZLinkOwnerLeaseTrackerOptions {
  readonly store: IZLinkOwnerLeaseStore;
  readonly options?: ZLinkLocationOptions;
  readonly monotonicNowMs?: () => number;
}

export class ZLinkOwnerLeaseTracker {
  private readonly store: IZLinkOwnerLeaseStore;
  private readonly options: Required<ZLinkLocationOptions>;
  private readonly monotonicNowMs: () => number;
  private snapshot?: OwnerLeaseTrackerSnapshot;
  private refresh?: Promise<OwnerLeaseTrackerSnapshot>;
  private liveOwnerFingerprint?: string;
  private liveOwnerVersion = 0;

  constructor(options: ZLinkOwnerLeaseTrackerOptions) {
    this.store = options.store;
    this.options = { ...zlinkDefaultLocationOptions, ...options.options };
    this.monotonicNowMs = options.monotonicNowMs ?? (() => performance.now());
  }

  async isOwnerLive(ownerId: string, signal?: AbortSignal): Promise<boolean> {
    const snapshot = await this.getSnapshot(signal);
    const lease = snapshot.leases.get(ownerId);
    if (lease === undefined) {
      return false;
    }
    return this.remainingLeaseMs(lease, snapshot) > 0;
  }

  async getLiveOwnerSetVersion(signal?: AbortSignal): Promise<number> {
    const snapshot = await this.getSnapshot(signal);
    const live = [...snapshot.leases.values()]
      .filter((lease) => this.remainingLeaseMs(lease, snapshot) > 0)
      .map((lease) => lease.ownerId)
      .sort()
      .join('\n');
    if (live !== this.liveOwnerFingerprint) {
      this.liveOwnerFingerprint = live;
      this.liveOwnerVersion++;
    }
    return this.liveOwnerVersion;
  }

  private async getSnapshot(signal?: AbortSignal): Promise<OwnerLeaseTrackerSnapshot> {
    const current = this.snapshot;
    if (current !== undefined && this.monotonicNowMs() - current.fetchedAtMs < this.options.pollingIntervalMs) {
      return current;
    }

    if (this.refresh !== undefined) {
      return this.refresh;
    }

    this.refresh = this.refreshSnapshot(signal);
    try {
      return await this.refresh;
    } finally {
      this.refresh = undefined;
    }
  }

  private async refreshSnapshot(signal?: AbortSignal): Promise<OwnerLeaseTrackerSnapshot> {
    const listed = await this.store.listOwnerLeases(signal);
    const snapshot = {
      leases: new Map(listed.leases.map((lease) => [lease.ownerId, lease])),
      storeNow: listed.storeNow,
      fetchedAtMs: this.monotonicNowMs()
    };
    this.snapshot = snapshot;
    return snapshot;
  }

  private remainingLeaseMs(lease: ZLinkOwnerLease, snapshot: OwnerLeaseTrackerSnapshot): number {
    const elapsedMs = this.monotonicNowMs() - snapshot.fetchedAtMs;
    return lease.leaseExpiresAt.getTime() - snapshot.storeNow.getTime() - elapsedMs;
  }
}

export interface ZLinkStoreLocationResolversOptions {
  readonly stores: ZLinkLocationRuntimeStores;
  readonly leaseTracker: ZLinkOwnerLeaseTracker;
  readonly events?: ZLinkLocationEventSink;
  readonly spotMeshNames?: readonly string[];
}

export class ZLinkStoreLocationResolvers implements
  IZLinkPeerLocationResolver,
  ZLinkSpotRefResolver,
  IZLinkActorAddressResolver {
  constructor(private readonly options: ZLinkStoreLocationResolversOptions) {}

  async listLivePeers(filter: ZLinkPeerLocationFilter, signal?: AbortSignal): Promise<readonly ZLinkPeerLocation[]> {
    const rows = await this.options.stores.peerStore.listPeers(filter, signal);
    const live: ZLinkPeerLocation[] = [];
    for (const row of rows) {
      if (!isKnownAutoConnectType(row.autoConnectType)
        || !isKnownLocationRole(row.role)
        || !(await this.options.leaseTracker.isOwnerLive(row.ownerId, signal))) {
        continue;
      }
      live.push(row);
    }
    return live;
  }

  async resolveRoute(key: ZLinkRouteLocationKey, signal?: AbortSignal): Promise<ZLinkRouteLocation | undefined> {
    const row = await this.options.stores.routeStore.resolveRoute(key, signal);
    if (row === undefined || !(await this.options.leaseTracker.isOwnerLive(row.ownerId, signal))) {
      this.options.events?.routeResolveMiss(key);
      return undefined;
    }
    return row;
  }

  async resolveSpotRef(spotRid: RoutingId, signal?: AbortSignal): Promise<SpotRef | undefined> {
    for (const meshName of this.options.spotMeshNames ?? []) {
      const row = await this.resolveSpotRow({ meshName, spotRid }, signal);
      if (row !== undefined) {
        return {
          meshName: row.meshName,
          nodeRid: String(row.nodeRid),
          spotRid: String(row.spotRid),
          spotKind: row.spotKind
        };
      }
    }
    return undefined;
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
    const row = await this.options.stores.spotStore.resolveSpot(key, signal);
    if (row === undefined || !(await this.options.leaseTracker.isOwnerLive(row.ownerId, signal))) {
      this.options.events?.spotResolveMiss(key);
      return undefined;
    }
    return row;
  }

  async resolveActorRow(
    key: ZLinkActorLocationKey,
    signal?: AbortSignal
  ): Promise<ZLinkActorLocation | undefined> {
    const row = await this.options.stores.actorStore.resolveActor({ actorId: key.actorId }, signal);
    if (row === undefined
      || row.actorRef === undefined
      || !(await this.options.leaseTracker.isOwnerLive(row.ownerId, signal))) {
      this.options.events?.actorResolveMiss({ actorId: key.actorId });
      return undefined;
    }
    return row;
  }
}

export class ZLinkLocationReadiness implements IZLinkLocationReadiness {
  constructor(private readonly query: IZLinkLocationRuntimeQuery) {}

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
    for (const meshName of this.meshNames) {
      const row = await this.rows.resolveSpotRow({ meshName, spotRid }, signal);
      if (row !== undefined) {
        return {
          routerChannelId: this.routerChannelIdForMesh(row.meshName),
          targetNodeRid: row.nodeRid,
          spotRid: row.spotRid,
          spotKind: row.spotKind
        };
      }
    }
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.SpotRouteNotFound,
      `SPOT '${spotRid}' has no live location row in any registered spot mesh.`
    );
  }
}

export interface ZLinkAutoConnectReconcilerOptions {
  readonly local: ZLinkAutoConnectLocal;
  readonly localRow?: ZLinkPeerLocation;
  readonly runtime: ZLinkLocationRuntime;
  readonly peerResolver: IZLinkPeerLocationResolver;
  readonly executor: IZLinkAutoConnectExecutor;
  readonly events?: ZLinkLocationEventSink;
  readonly options?: ZLinkLocationOptions;
  readonly monotonicNowMs?: () => number;
}

export class ZLinkAutoConnectReconciler {
  private readonly local: ZLinkAutoConnectLocal;
  private readonly localRow?: ZLinkPeerLocation;
  private readonly runtime: ZLinkLocationRuntime;
  private readonly peerResolver: IZLinkPeerLocationResolver;
  private readonly executor: IZLinkAutoConnectExecutor;
  private readonly events?: ZLinkLocationEventSink;
  private readonly options: Required<ZLinkLocationOptions>;
  private readonly monotonicNowMs: () => number;
  private readonly active = new Map<string, ZLinkAutoConnectTarget>();
  private localGeneration = 0n;
  private localPublished = false;
  private storeFailedValue = false;
  private recoveryDeferUntilMs = 0;
  private meshMemberRidHexes?: ReadonlySet<string>;

  constructor(options: ZLinkAutoConnectReconcilerOptions) {
    this.local = options.local;
    this.localRow = options.localRow;
    this.runtime = options.runtime;
    this.peerResolver = options.peerResolver;
    this.executor = options.executor;
    this.events = options.events;
    this.options = { ...zlinkDefaultLocationOptions, ...options.options };
    this.monotonicNowMs = options.monotonicNowMs ?? (() => performance.now());
  }

  get storeFailed(): boolean {
    return this.storeFailedValue;
  }

  get activeTargets(): readonly ZLinkAutoConnectTarget[] {
    return [...this.active.values()];
  }

  knowsPeer(nodeRid: RoutingId): boolean | undefined {
    if (this.meshMemberRidHexes === undefined) {
      return undefined;
    }
    return this.meshMemberRidHexes.has(encodeRoutingIdHex(nodeRid));
  }

  async tick(signal?: AbortSignal): Promise<void> {
    try {
      await this.publishLocal(signal);
    } catch {
      this.storeFailedValue = true;
      this.localPublished = false;
      return;
    }

    let rows: readonly ZLinkPeerLocation[];
    try {
      rows = await this.peerResolver.listLivePeers({
        autoConnectType: this.local.autoConnectType,
        meshName: this.local.meshName
      }, signal);
    } catch {
      this.storeFailedValue = true;
      this.localPublished = false;
      return;
    }

    const traceAutoConnect = process.env.ZLINK_AUTOCONNECT_TRACE === '1';
    if (traceAutoConnect) {
      console.error(
        `[zlink-autoconnect] tick local type=${zlinkLocationAutoConnectTypeName(this.local.autoConnectType)} ` +
        `mesh=${this.local.meshName} role=${zlinkLocationRoleName(this.local.role)} ` +
        `rid=${formatAutoConnectRid(this.local.nodeRid)} endpoint=${this.local.endpoint} rows=${rows.length}`
      );
      for (const row of rows) {
        console.error(
          `[zlink-autoconnect] row rid=${formatAutoConnectRid(row.nodeRid)} endpoint=${row.endpoint} ` +
          `role=${zlinkLocationRoleName(row.role)} decision=${formatAutoConnectDecision(this.local, row)}`
        );
      }
    }

    if (this.storeFailedValue) {
      this.storeFailedValue = false;
      this.recoveryDeferUntilMs = this.monotonicNowMs() + this.options.heartbeatIntervalMs;
    }

    this.meshMemberRidHexes = new Set(rows
      .map((row) => row.nodeRid)
      .filter((nodeRid): nodeRid is RoutingId => nodeRid !== undefined)
      .map((nodeRid) => encodeRoutingIdHex(nodeRid)));

    const desired = ZLinkAutoConnectPlanner.computeDesired(this.local, rows);
    const connectedEndpoints: string[] = [];
    const disconnectedEndpoints: string[] = [];
    for (const [key, target] of desired) {
      const current = this.active.get(key);
      if (current === undefined) {
        if (traceAutoConnect) {
          console.error(
            `[zlink-autoconnect] dial start rid=${formatAutoConnectRid(target.nodeRid)} endpoint=${target.endpoint}`
          );
        }
        const connected = this.executor.connect(target);
        if (traceAutoConnect) {
          console.error(
            `[zlink-autoconnect] ${connected ? 'dial ok' : 'dial skipped'} ` +
              `rid=${formatAutoConnectRid(target.nodeRid)} endpoint=${target.endpoint}`
          );
        }
        if (connected) {
          connectedEndpoints.push(target.endpoint);
          this.active.set(key, target);
        }
        continue;
      }

      if (current.endpoint !== target.endpoint || current.ownerId !== target.ownerId) {
        this.executor.disconnect(current);
        if (traceAutoConnect) {
          console.error(
            `[zlink-autoconnect] dial start rid=${formatAutoConnectRid(target.nodeRid)} endpoint=${target.endpoint}`
          );
        }
        const connected = this.executor.connect(target);
        if (traceAutoConnect) {
          console.error(
            `[zlink-autoconnect] ${connected ? 'dial ok' : 'dial skipped'} ` +
              `rid=${formatAutoConnectRid(target.nodeRid)} endpoint=${target.endpoint}`
          );
        }
        disconnectedEndpoints.push(current.endpoint);
        this.active.delete(key);
        if (connected) {
          connectedEndpoints.push(target.endpoint);
          this.active.set(key, target);
        }
      }
    }

    if (this.monotonicNowMs() < this.recoveryDeferUntilMs) {
      this.publishDesiredSetChange(connectedEndpoints, disconnectedEndpoints);
      return;
    }

    for (const [key, target] of [...this.active]) {
      if (!desired.has(key)) {
        this.executor.disconnect(target);
        disconnectedEndpoints.push(target.endpoint);
        this.active.delete(key);
      }
    }

    this.publishDesiredSetChange(connectedEndpoints, disconnectedEndpoints);
  }

  async shutdown(signal?: AbortSignal): Promise<void> {
    if (this.localPublished && this.localRow !== undefined) {
      await this.runtime.removePeer({
        autoConnectType: this.localRow.autoConnectType,
        meshName: this.localRow.meshName,
        role: this.localRow.role,
        nodeRid: this.localRow.nodeRid,
        endpoint: this.localRow.endpoint
      }, this.localGeneration, signal);
      this.localPublished = false;
    }

    for (const target of this.active.values()) {
      this.executor.disconnect(target);
    }
    this.active.clear();
  }

  private async publishLocal(signal?: AbortSignal): Promise<void> {
    if (this.localRow === undefined || this.localPublished) {
      return;
    }

    const claimed = await this.runtime.writePeer(this.localRow, ZLinkLocationWriteIntent.NewClaim, signal);
    if (claimed.status === ZLinkLocationWriteStatus.Stored) {
      this.localGeneration = claimed.generation;
      this.localPublished = true;
      return;
    }

    if (claimed.status === ZLinkLocationWriteStatus.RejectedConflict && this.localGeneration > 0n) {
      const renewed = await this.runtime.writePeer({
        ...this.localRow,
        generation: this.localGeneration
      }, ZLinkLocationWriteIntent.Renew, signal);
      this.localPublished = renewed.status === ZLinkLocationWriteStatus.Stored;
    }
  }

  private publishDesiredSetChange(
    connectedEndpoints: readonly string[],
    disconnectedEndpoints: readonly string[]
  ): void {
    if (connectedEndpoints.length === 0 && disconnectedEndpoints.length === 0) {
      return;
    }
    this.events?.desiredSetChanged({
      autoConnectType: this.local.autoConnectType,
      meshName: this.local.meshName,
      connectedEndpoints,
      disconnectedEndpoints
    });
  }
}

export interface ZLinkAutoConnectLoopOptions {
  readonly reconciler: ZLinkAutoConnectReconciler;
  readonly local: ZLinkAutoConnectLocal;
  readonly options?: ZLinkLocationOptions;
  readonly changeStampStore?: IZLinkLocationChangeStampStore;
  readonly watchStore?: IZLinkLocationWatchStore;
  readonly leaseTracker?: ZLinkOwnerLeaseTracker;
  readonly setTimer?: (callback: () => void, delayMs: number) => unknown;
  readonly clearTimer?: (handle: unknown) => void;
}

export class ZLinkAutoConnectLoop {
  private readonly reconciler: ZLinkAutoConnectReconciler;
  private readonly options: Required<ZLinkLocationOptions>;
  private readonly changeStampScope: ZLinkLocationChangeStampScope;
  private readonly changeStampStore?: IZLinkLocationChangeStampStore;
  private readonly watchStore?: IZLinkLocationWatchStore;
  private readonly leaseTracker?: ZLinkOwnerLeaseTracker;
  private readonly setTimer: (callback: () => void, delayMs: number) => unknown;
  private readonly clearTimer: (handle: unknown) => void;
  private controller?: AbortController;
  private timer?: unknown;
  private watchTask?: Promise<void>;
  private lastStamp?: bigint;
  private lastLiveOwnerSetVersion?: number;
  private lastTickFailed = false;

  constructor(options: ZLinkAutoConnectLoopOptions) {
    this.reconciler = options.reconciler;
    this.options = { ...zlinkDefaultLocationOptions, ...options.options };
    this.changeStampScope = { kind: ZLinkLocationKind.Peer, meshName: options.local.meshName };
    this.changeStampStore = options.changeStampStore;
    this.watchStore = options.watchStore;
    this.leaseTracker = options.leaseTracker;
    this.setTimer = options.setTimer ?? ((callback, delayMs) => setTimeout(callback, delayMs));
    this.clearTimer = options.clearTimer ?? ((handle) => clearTimeout(handle as NodeJS.Timeout));
  }

  async start(signal?: AbortSignal): Promise<void> {
    if (this.controller !== undefined) {
      return;
    }
    await this.tick(signal);
    this.controller = new AbortController();
    this.scheduleNext();
    if (this.watchStore !== undefined) {
      this.watchTask = this.watch(this.controller.signal);
    }
  }

  async stop(signal?: AbortSignal): Promise<void> {
    const controller = this.controller;
    this.controller = undefined;
    if (controller !== undefined) {
      controller.abort();
    }
    if (this.timer !== undefined) {
      this.clearTimer(this.timer);
      this.timer = undefined;
    }
    await this.watchTask?.catch(() => undefined);
    this.watchTask = undefined;
    await this.reconciler.shutdown(signal);
  }

  async tick(signal?: AbortSignal): Promise<void> {
    if (this.changeStampStore !== undefined && !this.lastTickFailed) {
      try {
        const stamp = await this.changeStampStore.getChangeStamp(this.changeStampScope, signal);
        const liveOwners = this.leaseTracker === undefined
          ? 0
          : await this.leaseTracker.getLiveOwnerSetVersion(signal);
        if (this.lastStamp === stamp && this.lastLiveOwnerSetVersion === liveOwners) {
          return;
        }
        const tickFailed = await this.runReconcile(signal);
        if (!tickFailed) {
          this.lastStamp = stamp;
          this.lastLiveOwnerSetVersion = liveOwners;
        }
        return;
      } catch {
        // A failed stamp read degrades to a full reconcile tick.
      }
    }

    await this.runReconcile(signal);
    this.lastStamp = undefined;
    this.lastLiveOwnerSetVersion = undefined;
  }

  private async runReconcile(signal?: AbortSignal): Promise<boolean> {
    await this.reconciler.tick(signal);
    this.lastTickFailed = this.reconciler.storeFailed;
    return this.lastTickFailed;
  }

  private scheduleNext(): void {
    if (this.controller === undefined || this.controller.signal.aborted) {
      return;
    }
    this.timer = this.setTimer(() => {
      this.timer = undefined;
      void this.tick(this.controller?.signal)
        .catch(() => undefined)
        .finally(() => this.scheduleNext());
    }, this.options.pollingIntervalMs);
  }

  private async watch(signal: AbortSignal): Promise<void> {
    while (!signal.aborted && this.watchStore !== undefined) {
      try {
        for await (const _ of this.watchStore.watch({ kind: ZLinkLocationKind.Peer, meshName: this.changeStampScope.meshName }, signal)) {
          await this.tick(signal);
        }
      } catch (error) {
        if (isAbortError(error)) {
          return;
        }
        await sleep(this.options.pollingIntervalMs, signal);
      }
    }
  }
}

export enum ZLinkActorClaimStatus {
  Claimed = 'claimed',
  AlreadyOwned = 'alreadyOwned',
  Conflict = 'conflict'
}

export interface ZLinkActorClaimResult {
  readonly status: ZLinkActorClaimStatus;
  readonly existing?: ZLinkActorLocation;
}

export interface ZLinkActorClaimActivation<TActor> {
  readonly activated?: TActor;
  readonly existingLocation?: ZLinkActorLocation;
}

export class ZLinkLocationLifecycle {
  private readonly actors = new Map<string, TrackedActor>();
  private readonly spots = new Map<string, TrackedSpot>();
  private readonly routes = new Map<string, bigint>();
  private readonly ownershipLostHandler = (event: ZLinkOwnershipLostEvent) => this.onOwnershipLost(event);

  constructor(
    private readonly runtime: ZLinkLocationRuntime,
    private readonly actorStore: IZLinkActorLocationStore,
    private readonly entrySpotMeshName = ''
  ) {
    this.runtime.addOwnershipLostHandler(this.ownershipLostHandler);
  }

  dispose(): void {
    this.runtime.removeOwnershipLostHandler(this.ownershipLostHandler);
  }

  async executeActorClaimThenActivate<TActor>(
    actorType: string,
    actorId: string,
    nodeRid: RoutingId,
    deactivate: (() => Promise<void>) | undefined,
    activate: () => Promise<TActor>
  ): Promise<ZLinkActorClaimActivation<TActor>> {
    const claim = await this.claimActor(actorType, actorId, nodeRid, deactivate);
    if (claim.status === ZLinkActorClaimStatus.Conflict) {
      return { existingLocation: claim.existing };
    }
    try {
      return { activated: await activate() };
    } catch (error) {
      if (claim.status === ZLinkActorClaimStatus.Claimed) {
        await this.releaseActor(actorType, actorId);
      }
      throw error;
    }
  }

  async claimActor(
    actorType: string,
    actorId: string,
    nodeRid: RoutingId,
    deactivate?: () => Promise<void>
  ): Promise<ZLinkActorClaimResult> {
    const normalizedType = ZLinkLocationKeyCodec.normalizeActorType(actorType);
    const key = { actorId };
    const canonical = ZLinkLocationKeyCodec.encodeActorKey(key);
    if (this.actors.has(canonical)) {
      return { status: ZLinkActorClaimStatus.AlreadyOwned };
    }

    const row: ZLinkActorLocation = {
      actorType: normalizedType,
      actorId,
      actorRef: undefined,
      nodeRid,
      generation: 0n,
      locationKind: ZLinkSpotKind.Entry,
      spotMeshName: this.entrySpotMeshName,
      spotRid: undefined,
      ownerId: '',
      updatedAt: new Date(0)
    };
    const result = await this.runtime.writeActor(row, ZLinkLocationWriteIntent.NewClaim);
    if (result.status === ZLinkLocationWriteStatus.Stored) {
      this.actors.set(canonical, {
        row: { ...row, generation: result.generation, ownerId: this.runtime.ownerId, updatedAt: result.updatedAt },
        deactivate
      });
      return { status: ZLinkActorClaimStatus.Claimed };
    }

    if (result.status === ZLinkLocationWriteStatus.RejectedConflict) {
      return {
        status: ZLinkActorClaimStatus.Conflict,
        existing: await this.actorStore.resolveActor(key)
      };
    }

    return { status: ZLinkActorClaimStatus.Conflict };
  }

  async setActorRef(actorType: string, actorId: string, actorRef: ActorRef): Promise<void> {
    await this.renewActor(actorType, actorId, (row) => ({ ...row, actorRef }));
  }

  async notifyActorJoinedSpot(actorType: string, actorId: string, spotMeshName: string, spotRid: RoutingId): Promise<void> {
    await this.renewActor(actorType, actorId, (row) => ({
      ...row,
      locationKind: ZLinkSpotKind.User,
      spotMeshName,
      spotRid
    }));
  }

  async notifyActorLeftSpot(actorType: string, actorId: string): Promise<void> {
    await this.renewActor(actorType, actorId, (row) => ({
      ...row,
      locationKind: ZLinkSpotKind.Entry,
      spotMeshName: this.entrySpotMeshName,
      spotRid: undefined
    }));
  }

  async releaseActor(actorType: string, actorId: string): Promise<void> {
    const key = { actorId };
    const canonical = ZLinkLocationKeyCodec.encodeActorKey(key);
    const tracked = this.actors.get(canonical);
    if (tracked === undefined) {
      return;
    }
    this.actors.delete(canonical);
    await this.runtime.removeActor(key, tracked.row.generation);
  }

  ownsActor(actorType: string, actorId: string): boolean {
    return this.actors.has(ZLinkLocationKeyCodec.encodeActorKey({
      actorId
    }));
  }

  async claimSpot(
    meshName: string,
    spotRid: RoutingId,
    spotType: string | undefined,
    nodeRid: RoutingId,
    spotKind: ZLinkSpotKind,
    routeEndpoint?: string,
    deactivate?: () => Promise<void>
  ): Promise<ZLinkLocationWriteStatus> {
    const row: ZLinkSpotLocation = {
      meshName,
      spotRid,
      spotType,
      nodeRid,
      spotKind,
      routeEndpoint,
      ownerId: '',
      generation: 0n,
      updatedAt: new Date(0)
    };
    const result = await this.runtime.writeSpot(row, ZLinkLocationWriteIntent.NewClaim);
    if (result.status === ZLinkLocationWriteStatus.Stored) {
      this.spots.set(
        ZLinkLocationKeyCodec.encodeSpotKey({ meshName, spotRid }),
        { generation: result.generation, deactivate }
      );
    }
    return result.status;
  }

  async releaseSpot(meshName: string, spotRid: RoutingId): Promise<void> {
    const key = { meshName, spotRid };
    const canonical = ZLinkLocationKeyCodec.encodeSpotKey(key);
    const tracked = this.spots.get(canonical);
    if (tracked === undefined) {
      return;
    }
    this.spots.delete(canonical);
    await this.runtime.removeSpot(key, tracked.generation);
  }

  async bindActorSessionRoute(sessionRid: RoutingId, actorId: string, ownerNodeRid: RoutingId): Promise<void> {
    const routeKey = encodeRoutingIdHex(sessionRid);
    const row: ZLinkRouteLocation = {
      routeKind: ZLinkRouteKind.ActorSession,
      routeKey,
      ownerNodeRid,
      ownerId: '',
      generation: 0n,
      value: Buffer.from(actorId, 'utf8'),
      updatedAt: new Date(0)
    };
    let result = await this.runtime.writeRoute(row, ZLinkLocationWriteIntent.NewClaim);
    if (result.status === ZLinkLocationWriteStatus.RejectedConflict) {
      result = await this.runtime.writeRoute(row, ZLinkLocationWriteIntent.Takeover);
    }
    if (result.status === ZLinkLocationWriteStatus.Stored) {
      this.routes.set(
        ZLinkLocationKeyCodec.encodeRouteKey({ routeKind: ZLinkRouteKind.ActorSession, routeKey }),
        result.generation
      );
    }
  }

  async removeActorSessionRoute(sessionRid: RoutingId): Promise<void> {
    const key = {
      routeKind: ZLinkRouteKind.ActorSession,
      routeKey: encodeRoutingIdHex(sessionRid)
    };
    const canonical = ZLinkLocationKeyCodec.encodeRouteKey(key);
    const generation = this.routes.get(canonical);
    if (generation === undefined) {
      return;
    }
    this.routes.delete(canonical);
    await this.runtime.removeRoute(key, generation);
  }

  private async renewActor(
    actorType: string,
    actorId: string,
    mutate: (row: ZLinkActorLocation) => ZLinkActorLocation
  ): Promise<void> {
    const canonical = ZLinkLocationKeyCodec.encodeActorKey({
      actorId
    });
    const tracked = this.actors.get(canonical);
    if (tracked === undefined) {
      return;
    }
    tracked.row = mutate(tracked.row);
    const result = await this.runtime.writeActor(tracked.row, ZLinkLocationWriteIntent.Renew);
    if (result.status === ZLinkLocationWriteStatus.Stored) {
      tracked.row = { ...tracked.row, generation: result.generation, updatedAt: result.updatedAt };
    }
  }

  private onOwnershipLost(event: ZLinkOwnershipLostEvent): void {
    let deactivate: (() => Promise<void>) | undefined;
    if (event.kind === ZLinkLocationKind.Actor) {
      const tracked = this.actors.get(event.key);
      this.actors.delete(event.key);
      deactivate = tracked?.deactivate;
    } else if (event.kind === ZLinkLocationKind.Spot) {
      const tracked = this.spots.get(event.key);
      this.spots.delete(event.key);
      deactivate = tracked?.deactivate;
    } else if (event.kind === ZLinkLocationKind.Route) {
      this.routes.delete(event.key);
    }

    if (deactivate !== undefined) {
      void deactivate().catch(() => undefined);
    }
  }
}

interface TrackedActor {
  row: ZLinkActorLocation;
  readonly deactivate?: () => Promise<void>;
}

interface TrackedSpot {
  readonly generation: bigint;
  readonly deactivate?: () => Promise<void>;
}

class RowTable<TRow> {
  readonly rows = new Map<string, TRow>();
  readonly generations = new Map<string, bigint>();
}

function pageRows<TRow>(
  table: RowTable<TRow>,
  matches: (row: TRow) => boolean,
  page: ZLinkPageRequest
): ZLinkLocationPage<TRow> {
  const ordered = [...table.rows.entries()]
    .filter(([, row]) => matches(row))
    .sort(([left], [right]) => left.localeCompare(right))
    .map(([, row]) => row);
  const offset = parseContinuationToken(page.continuationToken);
  const size = page.pageSize !== undefined && page.pageSize > 0 ? page.pageSize : Number.MAX_SAFE_INTEGER;
  const items = ordered.slice(offset, offset + size);
  const nextOffset = offset + items.length;
  return {
    items,
    continuationToken: nextOffset < ordered.length ? String(nextOffset) : undefined
  };
}

function parseContinuationToken(token: string | undefined): number {
  if (token === undefined) {
    return 0;
  }
  const parsed = Number.parseInt(token, 10);
  return Number.isFinite(parsed) && parsed >= 0 ? parsed : 0;
}

function matchesPeer(row: ZLinkPeerLocation, filter: ZLinkPeerLocationFilter): boolean {
  return (filter.autoConnectType === undefined || row.autoConnectType === filter.autoConnectType)
    && (filter.meshName === undefined || row.meshName === filter.meshName)
    && (filter.role === undefined || row.role === filter.role)
    && (filter.nodeRid === undefined || routingIdsEqual(row.nodeRid, filter.nodeRid))
    && (filter.endpoint === undefined || row.endpoint === filter.endpoint);
}

function matchesSpot(row: ZLinkSpotLocation, filter: ZLinkSpotLocationFilter): boolean {
  return (filter.meshName === undefined || row.meshName === filter.meshName)
    && (filter.spotType === undefined || row.spotType === filter.spotType)
    && (filter.nodeRid === undefined || routingIdsEqual(row.nodeRid, filter.nodeRid))
    && (filter.spotKind === undefined || row.spotKind === filter.spotKind);
}

function matchesActor(row: ZLinkActorLocation, filter: ZLinkActorLocationFilter): boolean {
  return (filter.actorType === undefined || row.actorType === filter.actorType)
    && (filter.nodeRid === undefined || routingIdsEqual(row.nodeRid, filter.nodeRid))
    && (filter.spotRid === undefined || routingIdsEqual(row.spotRid, filter.spotRid))
    && (filter.locationKind === undefined || row.locationKind === filter.locationKind);
}

function matchesRoute(row: ZLinkRouteLocation, filter: ZLinkRouteLocationFilter): boolean {
  return (filter.routeKind === undefined || row.routeKind === filter.routeKind)
    && (filter.ownerNodeRid === undefined || routingIdsEqual(row.ownerNodeRid, filter.ownerNodeRid))
    && (filter.ownerId === undefined || row.ownerId === filter.ownerId);
}

interface OwnerLeaseTrackerSnapshot {
  readonly leases: ReadonlyMap<string, ZLinkOwnerLease>;
  readonly storeNow: Date;
  readonly fetchedAtMs: number;
}

function autoConnectTargetKeyOf(peer: ZLinkPeerLocation): string {
  const identity = peer.nodeRid === undefined ? peer.endpoint : encodeRoutingIdHex(peer.nodeRid);
  return `${zlinkLocationRoleName(peer.role)}|${identity}`;
}

function isAutoConnectSelf(local: ZLinkAutoConnectLocal, peer: ZLinkPeerLocation): boolean {
  if (local.nodeRid !== undefined && peer.nodeRid !== undefined && routingIdsEqual(local.nodeRid, peer.nodeRid)) {
    return true;
  }
  return peer.endpoint === local.endpoint;
}

function shouldDialAutoConnectPeer(local: ZLinkAutoConnectLocal, peer: ZLinkPeerLocation): boolean {
  switch (local.autoConnectType) {
    case ZLinkLocationAutoConnectType.RouteMesh:
      return local.role === ZLinkLocationRole.Router
        && peer.role === ZLinkLocationRole.Router
        && localIsPairwiseInitiator(local, peer);
    case ZLinkLocationAutoConnectType.ClientServer:
      return local.role === ZLinkLocationRole.Dealer && peer.role === ZLinkLocationRole.Router;
    case ZLinkLocationAutoConnectType.DealerMesh:
      return local.role === ZLinkLocationRole.Dealer
        && peer.role === ZLinkLocationRole.Dealer
        && localIsPairwiseInitiator(local, peer);
    case ZLinkLocationAutoConnectType.Fanout:
      return local.role === ZLinkLocationRole.Sub && peer.role === ZLinkLocationRole.Pub;
    case ZLinkLocationAutoConnectType.SpotMesh:
      return local.role === ZLinkLocationRole.Spot
        && peer.role === ZLinkLocationRole.Spot
        && localIsPairwiseInitiator(local, peer);
    default:
      return false;
  }
}

function formatAutoConnectDecision(local: ZLinkAutoConnectLocal, peer: ZLinkPeerLocation): string {
  if (peer.autoConnectType !== local.autoConnectType) {
    return `skip:type=${zlinkLocationAutoConnectTypeName(peer.autoConnectType)}`;
  }
  if (peer.meshName !== local.meshName) {
    return `skip:mesh=${peer.meshName}`;
  }
  if (!ZLinkAutoConnectPlanner.isRoleAllowed(peer.autoConnectType, peer.role)) {
    return `skip:role=${zlinkLocationRoleName(peer.role)}`;
  }
  if (peer.endpoint.length === 0) {
    return 'skip:empty-endpoint';
  }
  if (isAutoConnectSelf(local, peer)) {
    return 'skip:self';
  }
  if (!shouldDialAutoConnectPeer(local, peer)) {
    return `skip:not-initiator localRid=${formatAutoConnectRid(local.nodeRid)}`;
  }
  return 'dial';
}

function formatAutoConnectRid(rid: RoutingId | undefined): string {
  return rid === undefined ? '<none>' : encodeRoutingIdHex(rid);
}

function localIsPairwiseInitiator(local: ZLinkAutoConnectLocal, peer: ZLinkPeerLocation): boolean {
  if (local.endpoint.length === 0) {
    return true;
  }
  if (local.nodeRid !== undefined && peer.nodeRid !== undefined) {
    const byRid = encodeRoutingIdHex(local.nodeRid).localeCompare(encodeRoutingIdHex(peer.nodeRid));
    if (byRid !== 0) {
      return byRid < 0;
    }
  }
  return local.endpoint.localeCompare(peer.endpoint) < 0;
}

function isKnownAutoConnectType(type: ZLinkLocationAutoConnectType): boolean {
  try {
    zlinkLocationAutoConnectTypeName(type);
    return true;
  } catch {
    return false;
  }
}

function isKnownLocationRole(role: ZLinkLocationRole): boolean {
  try {
    zlinkLocationRoleName(role);
    return true;
  } catch {
    return false;
  }
}

function isAbortError(error: unknown): boolean {
  return error instanceof Error && error.name === 'AbortError';
}

function sleep(delayMs: number, signal: AbortSignal): Promise<void> {
  return new Promise((resolve, reject) => {
    if (signal.aborted) {
      reject(new DOMException('The operation was aborted.', 'AbortError'));
      return;
    }
    const timer = setTimeout(resolve, delayMs);
    signal.addEventListener('abort', () => {
      clearTimeout(timer);
      reject(new DOMException('The operation was aborted.', 'AbortError'));
    }, { once: true });
  });
}

function routingIdsEqual(left: RoutingId | undefined, right: RoutingId | undefined): boolean {
  if (left === undefined || right === undefined) {
    return left === right;
  }
  return encodeRoutingIdHex(left) === encodeRoutingIdHex(right);
}

function stampKey(scope: ZLinkLocationChangeStampScope): string {
  return `${scope.kind}:${scope.meshName ?? ''}`;
}

function stored(generation: bigint, updatedAt: Date): ZLinkLocationWriteResult {
  return { status: ZLinkLocationWriteStatus.Stored, generation, updatedAt };
}

function ignoredStale(): ZLinkLocationWriteResult {
  return { status: ZLinkLocationWriteStatus.IgnoredStale, generation: 0n, updatedAt: new Date(0) };
}

function rejectedConflict(): ZLinkLocationWriteResult {
  return { status: ZLinkLocationWriteStatus.RejectedConflict, generation: 0n, updatedAt: new Date(0) };
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
