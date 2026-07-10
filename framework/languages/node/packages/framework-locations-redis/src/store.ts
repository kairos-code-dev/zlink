import { createClient } from 'redis';
import { RedisStoreKeys } from './redis-store-key-prefixes';
import {
  configureOptions,
  type RedisCommandClient,
  type RedisCommandValue,
  type MutableZLinkRedisLocationOptions,
  type ZLinkRedisLocationOptions
} from './redis-options';
import {
  encodeActorKey,
  encodePeerKey,
  encodeRouteKey,
  encodeSpotKey,
  routingIdHex
} from './redis-row-keys';
import {
  LIST_LEASES_SCRIPT,
  REMOVE_ALL_BY_OWNER_SCRIPT,
  REMOVE_LEASE_SCRIPT,
  REMOVE_SCRIPT,
  RENEW_LEASE_SCRIPT,
  WRITE_SCRIPT
} from './redis-scripts';
import {
  asArray,
  asString,
  fromUnixMs,
  intentName,
  kindActor,
  kindPeer,
  kindRoute,
  kindSpot,
  kindTagOf,
  matchesActor,
  matchesPeer,
  matchesRoute,
  matchesSpot,
  materialize,
  ridOf,
  toNumber,
  toWriteResult,
  type LocationKind
} from './redis-row-codec';
import {
  ZLinkLocationWriteIntent,
  type IZLinkLocationChangeStampStore,
  type IZLinkLocationStore,
  type RoutingId,
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
} from '@zlink-systems/framework';

export class ZLinkRedisLocationStore implements IZLinkLocationStore, IZLinkLocationChangeStampStore {
  private readonly keys: RedisStoreKeys;
  private readonly providedClient?: RedisCommandClient;
  private client?: RedisCommandClient;

  constructor(options: ZLinkRedisLocationOptions | ((options: MutableZLinkRedisLocationOptions) => void)) {
    const resolved = typeof options === 'function' ? configureOptions(options) : options;
    if (resolved.keyPrefix.length === 0) {
      throw new Error('ZLinkRedisLocationOptions.keyPrefix is required.');
    }
    if (resolved.client === undefined && resolved.url === undefined && resolved.clientOptions === undefined) {
      throw new Error('ZLinkRedisLocationOptions requires url, clientOptions, or client.');
    }
    this.keys = new RedisStoreKeys(resolved.keyPrefix);
    this.providedClient = resolved.client;
    if (resolved.client === undefined) {
      this.client = createClient({
        disableOfflineQueue: true,
        ...(resolved.clientOptions ?? {}),
        socket: {
          reconnectStrategy: false,
          ...(resolved.clientOptions?.socket ?? {})
        },
        url: resolved.url ?? resolved.clientOptions?.url
      }) as RedisCommandClient;
      this.client.on?.('error', () => {});
    }
  }

  async updatePeer(
    peer: ZLinkPeerLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    return await this.write(kindPeer, peer, intent, signal);
  }

  async removePeer(
    key: ZLinkPeerLocationKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    return await this.remove(kindPeer, encodePeerKey(key), key.meshName, owner, signal);
  }

  async listPeers(filter: ZLinkPeerLocationFilter, signal?: AbortSignal): Promise<readonly ZLinkPeerLocation[]> {
    const rows = await this.loadAll(kindPeer, signal);
    return rows.filter((row) => matchesPeer(row, filter));
  }

  async updateSpot(
    spot: ZLinkSpotLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    return await this.write(kindSpot, spot, intent, signal);
  }

  async removeSpot(
    key: ZLinkSpotLocationKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    return await this.remove(kindSpot, encodeSpotKey(key), key.meshName, owner, signal);
  }

  async resolveSpot(key: ZLinkSpotLocationKey, signal?: AbortSignal): Promise<ZLinkSpotLocation | undefined> {
    return await this.resolve(kindSpot, encodeSpotKey(key), signal);
  }

  async listSpots(
    filter: ZLinkSpotLocationFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkSpotLocation>> {
    return await this.listPage(kindSpot, (row) => matchesSpot(row, filter), page, signal);
  }

  async updateActor(
    actor: ZLinkActorLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    return await this.write(kindActor, actor, intent, signal);
  }

  async removeActor(
    key: ZLinkActorLocationKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    return await this.remove(kindActor, encodeActorKey(key), undefined, owner, signal);
  }

  async resolveActor(key: ZLinkActorLocationKey, signal?: AbortSignal): Promise<ZLinkActorLocation | undefined> {
    return await this.resolve(kindActor, encodeActorKey(key), signal);
  }

  async listActors(
    filter: ZLinkActorLocationFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkActorLocation>> {
    return await this.listPage(kindActor, (row) => matchesActor(row, filter), page, signal);
  }

  async updateRoute(
    route: ZLinkRouteLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    return await this.write(kindRoute, route, intent, signal);
  }

  async removeRoute(
    key: ZLinkRouteLocationKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    return await this.remove(kindRoute, encodeRouteKey(key), undefined, owner, signal);
  }

  async resolveRoute(key: ZLinkRouteLocationKey, signal?: AbortSignal): Promise<ZLinkRouteLocation | undefined> {
    return await this.resolve(kindRoute, encodeRouteKey(key), signal);
  }

  async listRoutes(
    filter: ZLinkRouteLocationFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkRouteLocation>> {
    return await this.listPage(kindRoute, (row) => matchesRoute(row, filter), page, signal);
  }

  async renewOwnerLease(
    ownerId: string,
    nodeRid: RoutingId,
    leaseTtlMs: number,
    signal?: AbortSignal
  ): Promise<ZLinkOwnerLeaseRenewal> {
    try {
      const ttlMs = Math.max(1, Math.trunc(leaseTtlMs));
      const nowMs = toNumber(await this.eval(RENEW_LEASE_SCRIPT, [
        this.keys.lease(ownerId),
        this.keys.leaseIndex()
      ], [ownerId, routingIdHex(nodeRid), String(ttlMs)], signal));
      const storeNow = fromUnixMs(nowMs);
      return { leaseExpiresAt: new Date(storeNow.getTime() + ttlMs), storeNow };
    } catch (error) {
      throw error;
    }
  }

  async removeOwnerLease(ownerId: string, signal?: AbortSignal): Promise<boolean> {
    try {
      const removed = toNumber(await this.eval(REMOVE_LEASE_SCRIPT, [
        this.keys.lease(ownerId),
        this.keys.leaseIndex()
      ], [ownerId], signal));
      return removed !== 0;
    } catch (error) {
      throw error;
    }
  }

  async removeAllByOwner(ownerId: string, signal?: AbortSignal): Promise<number> {
    return toNumber(await this.eval(REMOVE_ALL_BY_OWNER_SCRIPT, [
      this.keys.ownerIndexPrefix(kindPeer.tag) + ownerId,
      this.keys.ownerIndexPrefix(kindSpot.tag) + ownerId,
      this.keys.ownerIndexPrefix(kindActor.tag) + ownerId,
      this.keys.ownerIndexPrefix(kindRoute.tag) + ownerId,
      this.keys.kindIndex(kindPeer.tag),
      this.keys.kindIndex(kindSpot.tag),
      this.keys.kindIndex(kindActor.tag),
      this.keys.kindIndex(kindRoute.tag)
    ], [
      this.keys.rowHashPrefix(kindPeer.tag),
      this.keys.rowHashPrefix(kindSpot.tag),
      this.keys.rowHashPrefix(kindActor.tag),
      this.keys.rowHashPrefix(kindRoute.tag),
      this.keys.stamp(kindPeer.tag, undefined),
      this.keys.stamp(kindSpot.tag, undefined),
      this.keys.stamp(kindActor.tag, undefined),
      this.keys.stamp(kindRoute.tag, undefined)
    ], signal));
  }

  async listOwnerLeases(signal?: AbortSignal): Promise<ZLinkOwnerLeaseSnapshot> {
    const raw = asArray(await this.eval(LIST_LEASES_SCRIPT, [
      this.keys.leaseIndex()
    ], [this.keys.leasePrefix()], signal));
    const storeNow = fromUnixMs(toNumber(raw[0]));
    const entries = asArray(raw[1]);
    const leases: ZLinkOwnerLease[] = [];
    for (let index = 0; index + 2 < entries.length; index += 3) {
      const ownerId = asString(entries[index]);
      const value = asString(entries[index + 1]);
      const remainingMs = toNumber(entries[index + 2]);
      const separator = value.indexOf('|');
      leases.push({
        ownerId,
        nodeRid: ridOf(value.slice(0, separator)),
        leaseExpiresAt: new Date(storeNow.getTime() + remainingMs),
        updatedAt: fromUnixMs(Number(value.slice(separator + 1)))
      });
    }
    return { leases, storeNow };
  }

  async getChangeStamp(scope: ZLinkLocationChangeStampScope, signal?: AbortSignal): Promise<bigint> {
    const value = await this.command(['GET', this.keys.stamp(kindTagOf(scope.kind), scope.meshName)], signal);
    return value === null ? 0n : BigInt(asString(value));
  }

  async dispose(): Promise<void> {
    const client = this.client;
    this.client = undefined;
    if (client !== undefined && client !== this.providedClient) {
      if (client.quit !== undefined) {
        await client.quit();
      } else {
        await client.disconnect?.();
      }
    }
  }

  private async write<TRow>(
    kind: LocationKind<TRow>,
    row: TRow,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    try {
      const rowKey = kind.encodeKey(row);
      const meshName = kind.meshOf(row);
      const raw = asArray(await this.eval(WRITE_SCRIPT, [
        this.keys.rowHash(kind.tag, rowKey),
        this.keys.generation(kind.tag, rowKey),
        this.keys.kindIndex(kind.tag)
      ], [
        intentName(intent),
        kind.ownerOf(row),
        String(kind.generationOf(row)),
        JSON.stringify(kind.toJson(row)),
        rowKey,
        this.keys.leasePrefix(),
        this.keys.ownerIndexPrefix(kind.tag),
        this.keys.stamp(kind.tag, meshName),
        meshName === undefined ? '' : this.keys.stamp(kind.tag, undefined),
        meshName === undefined ? '0' : '1',
        meshName ?? ''
      ], signal));
      return toWriteResult(raw);
    } catch (error) {
      throw error;
    }
  }

  private async remove<TRow>(
    kind: LocationKind<TRow>,
    rowKey: string,
    meshName: string | undefined,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    try {
      const raw = asArray(await this.eval(REMOVE_SCRIPT, [
        this.keys.rowHash(kind.tag, rowKey),
        this.keys.kindIndex(kind.tag)
      ], [
        owner.ownerId,
        String(owner.generation),
        rowKey,
        this.keys.ownerIndexPrefix(kind.tag),
        this.keys.stamp(kind.tag, meshName),
        meshName === undefined ? '' : this.keys.stamp(kind.tag, undefined)
      ], signal));
      return toWriteResult(raw);
    } catch (error) {
      throw error;
    }
  }

  private async resolve<TRow>(
    kind: LocationKind<TRow>,
    rowKey: string,
    signal?: AbortSignal
  ): Promise<TRow | undefined> {
    const fields = asArray(await this.command([
      'HMGET',
      this.keys.rowHash(kind.tag, rowKey),
      'json',
      'gen',
      'updatedAtMs'
    ], signal));
    return materialize(kind, fields);
  }

  private async listPage<TRow>(
    kind: LocationKind<TRow>,
    matches: (row: TRow) => boolean,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<TRow>> {
    const pageSize = page?.pageSize ?? 0;
    let members: readonly unknown[];
    let continuationToken: string | undefined;
    if (pageSize <= 0) {
      members = asArray(await this.command(['SMEMBERS', this.keys.kindIndex(kind.tag)], signal));
    } else {
      const scan = asArray(await this.command([
        'SSCAN',
        this.keys.kindIndex(kind.tag),
        page?.continuationToken ?? '0',
        'COUNT',
        String(pageSize)
      ], signal));
      continuationToken = asString(scan[0]) === '0' ? undefined : asString(scan[0]);
      members = asArray(scan[1]);
    }
    const rows = await this.loadRows(kind, members.map(asString), signal);
    return { items: rows.filter(matches), continuationToken };
  }

  private async loadAll<TRow>(kind: LocationKind<TRow>, signal?: AbortSignal): Promise<TRow[]> {
    const members = asArray(await this.command(['SMEMBERS', this.keys.kindIndex(kind.tag)], signal));
    return await this.loadRows(kind, members.map(asString), signal);
  }

  private async loadRows<TRow>(
    kind: LocationKind<TRow>,
    rowKeys: readonly string[],
    signal?: AbortSignal
  ): Promise<TRow[]> {
    const rows: TRow[] = [];
    for (const rowKey of rowKeys) {
      const row = await this.resolve(kind, rowKey, signal);
      if (row !== undefined) {
        rows.push(row);
      }
    }
    return rows;
  }

  private async eval(script: string, keys: readonly string[], args: readonly string[], signal?: AbortSignal): Promise<unknown> {
    return await this.command(['EVAL', script, String(keys.length), ...keys, ...args], signal);
  }

  private async command(args: RedisCommandValue[], signal?: AbortSignal): Promise<unknown> {
    signal?.throwIfAborted();
    const client = await this.connectedClient(signal);
    return await client.sendCommand(args);
  }

  private async connectedClient(signal?: AbortSignal): Promise<RedisCommandClient> {
    signal?.throwIfAborted();
    const client = this.providedClient ?? this.client;
    if (client === undefined) {
      throw new Error('Redis location store is disposed.');
    }
    if (client.isOpen !== true) {
      await client.connect();
    }
    return client;
  }

}
