import type { RoutingId } from '../../contracts/Common';
import {
  ZLinkLocationKind,
  ZLinkLocationWriteIntent,
  ZLinkLocationWriteStatus,
  type IZLinkLocationChangeStampStore,
  type IZLinkLocationStore,
  type ZLinkActorLocation,
  type ZLinkActorLocationFilter,
  type ZLinkActorLocationKey,
  type ZLinkLocationChangeStampScope,
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
import { ZLinkLocationKeyCodec } from './key-codec';
import {
  matchesActorLocation,
  matchesPeerLocation,
  matchesRouteLocation,
  matchesSpotLocation
} from '../../location-store-integration';

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
    return [...this.peers.rows.values()].filter((row) => matchesPeerLocation(row, filter));
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
    return pageRows(this.spots, (row) => matchesSpotLocation(row, filter), page);
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
    return pageRows(this.actors, (row) => matchesActorLocation(row, filter), page);
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
    return pageRows(this.routes, (row) => matchesRouteLocation(row, filter), page);
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
