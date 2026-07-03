import { createClient, type RedisClientOptions } from 'redis';
import { RoutingId as BindingRoutingId } from '@zlink-systems/zlink';
import {
  ZLinkLocationKind,
  ZLinkLocationAutoConnectType,
  ZLinkLocationRole,
  ZLinkLocationWriteIntent,
  ZLinkLocationWriteStatus,
  ZLinkSpotKind,
  zlinkLocationAutoConnectTypeName,
  zlinkLocationRoleName,
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

type RedisCommandValue = string | Buffer | number;

interface RedisCommandClient {
  isOpen?: boolean;
  connect(): Promise<unknown>;
  sendCommand(args: RedisCommandValue[]): Promise<unknown>;
  quit?(): Promise<unknown>;
  disconnect?(): Promise<unknown>;
  on?(event: 'error', listener: (error: unknown) => void): unknown;
}

export interface ZLinkRedisLocationOptions {
  readonly url?: string;
  readonly client?: RedisCommandClient;
  readonly clientOptions?: RedisClientOptions;
  readonly keyPrefix: string;
}

export class ZLinkRedisLocationStore implements IZLinkLocationStore, IZLinkLocationChangeStampStore {
  private readonly keyPrefix: string;
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
    this.keyPrefix = resolved.keyPrefix;
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

  async removePeersByOwner(ownerId: string, signal?: AbortSignal): Promise<number> {
    return await this.removeByOwner(kindPeer, ownerId, signal);
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

  async removeSpotsByOwner(ownerId: string, signal?: AbortSignal): Promise<number> {
    return await this.removeByOwner(kindSpot, ownerId, signal);
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

  async removeActorsByOwner(ownerId: string, signal?: AbortSignal): Promise<number> {
    return await this.removeByOwner(kindActor, ownerId, signal);
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

  async removeRoutesByOwner(ownerId: string, signal?: AbortSignal): Promise<number> {
    return await this.removeByOwner(kindRoute, ownerId, signal);
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
  ): Promise<ZLinkLocationWriteResult> {
    try {
      const ttlMs = Math.max(1, Math.trunc(leaseTtlMs));
      const nowMs = toNumber(await this.eval(RENEW_LEASE_SCRIPT, [
        this.leaseKey(ownerId),
        this.leaseIndexKey()
      ], [ownerId, routingIdHex(nodeRid), String(ttlMs)], signal));
      return stored(0n, fromUnixMs(nowMs));
    } catch {
      return storeUnavailable();
    }
  }

  async removeOwnerLease(ownerId: string, signal?: AbortSignal): Promise<ZLinkLocationWriteResult> {
    try {
      const nowMs = toNumber(await this.eval(REMOVE_LEASE_SCRIPT, [
        this.leaseKey(ownerId),
        this.leaseIndexKey()
      ], [ownerId], signal));
      return stored(0n, fromUnixMs(nowMs));
    } catch {
      return storeUnavailable();
    }
  }

  async listOwnerLeases(signal?: AbortSignal): Promise<ZLinkOwnerLeaseSnapshot> {
    const raw = asArray(await this.eval(LIST_LEASES_SCRIPT, [
      this.leaseIndexKey()
    ], [this.leaseKeyPrefix()], signal));
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
    const value = await this.command(['GET', this.stampKey(kindTagOf(scope.kind), scope.meshName)], signal);
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
        this.rowHashKey(kind.tag, rowKey),
        this.generationKey(kind.tag, rowKey),
        this.kindIndexKey(kind.tag)
      ], [
        intentName(intent),
        kind.ownerOf(row),
        String(kind.generationOf(row)),
        JSON.stringify(kind.toJson(row)),
        rowKey,
        this.leaseKeyPrefix(),
        this.ownerIndexKeyPrefix(kind.tag),
        this.stampKey(kind.tag, meshName),
        meshName === undefined ? '' : this.stampKey(kind.tag, undefined),
        meshName === undefined ? '0' : '1',
        meshName ?? ''
      ], signal));
      return toWriteResult(raw);
    } catch {
      return storeUnavailable();
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
        this.rowHashKey(kind.tag, rowKey),
        this.kindIndexKey(kind.tag)
      ], [
        owner.ownerId,
        String(owner.generation),
        rowKey,
        this.ownerIndexKeyPrefix(kind.tag),
        this.stampKey(kind.tag, meshName),
        meshName === undefined ? '' : this.stampKey(kind.tag, undefined)
      ], signal));
      return toWriteResult(raw);
    } catch {
      return storeUnavailable();
    }
  }

  private async removeByOwner<TRow>(
    kind: LocationKind<TRow>,
    ownerId: string,
    signal?: AbortSignal
  ): Promise<number> {
    return toNumber(await this.eval(REMOVE_BY_OWNER_SCRIPT, [
      this.ownerIndexKeyPrefix(kind.tag) + ownerId,
      this.kindIndexKey(kind.tag)
    ], [
      this.rowHashKeyPrefix(kind.tag),
      this.stampKey(kind.tag, undefined)
    ], signal));
  }

  private async resolve<TRow>(
    kind: LocationKind<TRow>,
    rowKey: string,
    signal?: AbortSignal
  ): Promise<TRow | undefined> {
    const fields = asArray(await this.command([
      'HMGET',
      this.rowHashKey(kind.tag, rowKey),
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
      members = asArray(await this.command(['SMEMBERS', this.kindIndexKey(kind.tag)], signal));
    } else {
      const scan = asArray(await this.command([
        'SSCAN',
        this.kindIndexKey(kind.tag),
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
    const members = asArray(await this.command(['SMEMBERS', this.kindIndexKey(kind.tag)], signal));
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

  private rowHashKey(tag: string, rowKey: string): string {
    return this.rowHashKeyPrefix(tag) + rowKey;
  }

  private rowHashKeyPrefix(tag: string): string {
    return `${this.keyPrefix}:row:${tag}:`;
  }

  private generationKey(tag: string, rowKey: string): string {
    return `${this.keyPrefix}:gen:${tag}:${rowKey}`;
  }

  private kindIndexKey(tag: string): string {
    return `${this.keyPrefix}:keys:${tag}`;
  }

  private ownerIndexKeyPrefix(tag: string): string {
    return `${this.keyPrefix}:own:${tag}:`;
  }

  private leaseKey(ownerId: string): string {
    return this.leaseKeyPrefix() + ownerId;
  }

  private leaseKeyPrefix(): string {
    return `${this.keyPrefix}:lease:`;
  }

  private leaseIndexKey(): string {
    return `${this.keyPrefix}:leases`;
  }

  private stampKey(tag: string, meshName: string | undefined): string {
    return meshName === undefined
      ? `${this.keyPrefix}:stamp:${tag}`
      : `${this.keyPrefix}:stamp:${tag}:${meshName}`;
  }
}

export class MutableZLinkRedisLocationOptions {
  url?: string;
  client?: RedisCommandClient;
  clientOptions?: RedisClientOptions;
  keyPrefix = '';

  setUrl(url: string): this {
    this.url = url;
    return this;
  }

  setClient(client: RedisCommandClient): this {
    this.client = client;
    return this;
  }

  setClientOptions(options: RedisClientOptions): this {
    this.clientOptions = options;
    return this;
  }

  setKeyPrefix(keyPrefix: string): this {
    this.keyPrefix = keyPrefix;
    return this;
  }
}

function configureOptions(
  configure: (options: MutableZLinkRedisLocationOptions) => void
): ZLinkRedisLocationOptions {
  const options = new MutableZLinkRedisLocationOptions();
  configure(options);
  return options;
}

interface PeerKey {
  readonly autoConnectType: ZLinkLocationAutoConnectType;
  readonly meshName: string;
  readonly role: ZLinkLocationRole;
  readonly nodeRid?: RoutingId;
  readonly endpoint?: string;
}

interface SpotKey {
  readonly meshName: string;
  readonly spotRid: RoutingId;
}

interface ActorKey {
  readonly actorType?: string;
  readonly actorId: string;
}

interface RouteKey {
  readonly routeKind: number;
  readonly routeKey: string;
}

function encodePeerKey(key: PeerKey): string {
  return encodeKeySegments(
    zlinkLocationAutoConnectTypeName(key.autoConnectType),
    key.meshName,
    zlinkLocationRoleName(key.role),
    key.nodeRid === undefined ? key.endpoint ?? '' : routingIdHex(key.nodeRid)
  );
}

function encodeSpotKey(key: SpotKey): string {
  return encodeKeySegments(key.meshName, routingIdHex(key.spotRid));
}

function encodeActorKey(key: ActorKey): string {
  return encodeKeySegments(key.actorType ?? '', key.actorId);
}

function encodeRouteKey(key: RouteKey): string {
  return encodeKeySegments(String(key.routeKind), key.routeKey);
}

function encodeKeySegments(...segments: readonly string[]): string {
  return segments.map((segment) => `${segment.length}:${segment}`).join('');
}

interface LocationKind<TRow> {
  readonly tag: string;
  encodeKey(row: TRow): string;
  meshOf(row: TRow): string | undefined;
  ownerOf(row: TRow): string;
  generationOf(row: TRow): bigint;
  toJson(row: TRow): unknown;
  fromJson(json: unknown, generation: bigint, updatedAt: Date): TRow;
}

const kindPeer: LocationKind<ZLinkPeerLocation> = {
  tag: 'peer',
  encodeKey: (row) => encodePeerKey({
    autoConnectType: row.autoConnectType,
    meshName: row.meshName,
    role: row.role,
    nodeRid: row.nodeRid,
    endpoint: row.endpoint
  }),
  meshOf: (row) => row.meshName,
  ownerOf: (row) => row.ownerId,
  generationOf: (row) => row.generation,
  toJson: peerToJson,
  fromJson: peerFromJson
};

const kindSpot: LocationKind<ZLinkSpotLocation> = {
  tag: 'spot',
  encodeKey: (row) => encodeSpotKey({ meshName: row.meshName, spotRid: row.spotRid }),
  meshOf: (row) => row.meshName,
  ownerOf: (row) => row.ownerId,
  generationOf: (row) => row.generation,
  toJson: spotToJson,
  fromJson: spotFromJson
};

const kindActor: LocationKind<ZLinkActorLocation> = {
  tag: 'actor',
  encodeKey: (row) => encodeActorKey({ actorType: row.actorType, actorId: row.actorId }),
  meshOf: () => undefined,
  ownerOf: (row) => row.ownerId,
  generationOf: (row) => row.generation,
  toJson: actorToJson,
  fromJson: actorFromJson
};

const kindRoute: LocationKind<ZLinkRouteLocation> = {
  tag: 'route',
  encodeKey: (row) => encodeRouteKey({ routeKind: row.routeKind, routeKey: row.routeKey }),
  meshOf: () => undefined,
  ownerOf: (row) => row.ownerId,
  generationOf: (row) => row.generation,
  toJson: routeToJson,
  fromJson: routeFromJson
};

function peerToJson(row: ZLinkPeerLocation): unknown {
  return {
    AutoConnectType: row.autoConnectType,
    MeshName: row.meshName,
    NodeRid: row.nodeRid === undefined ? null : routingIdHex(row.nodeRid),
    Role: row.role,
    Endpoint: row.endpoint,
    Weight: row.weight,
    Value: Number(row.value),
    Metadata: row.metadata ?? null,
    Capabilities: row.capabilities ?? null,
    OwnerId: row.ownerId,
    Generation: Number(row.generation),
    UpdatedAt: row.updatedAt.toISOString()
  };
}

function peerFromJson(json: unknown, generation: bigint, updatedAt: Date): ZLinkPeerLocation {
  const row = objectOf(json);
  return {
    autoConnectType: numberOf(row.AutoConnectType),
    meshName: stringOf(row.MeshName),
    nodeRid: optionalRid(row.NodeRid),
    role: numberOf(row.Role),
    endpoint: stringOf(row.Endpoint),
    weight: numberOf(row.Weight),
    value: BigInt(numberOf(row.Value)),
    metadata: optionalRecord(row.Metadata),
    capabilities: optionalStringArray(row.Capabilities),
    ownerId: stringOf(row.OwnerId),
    generation,
    updatedAt
  };
}

function spotToJson(row: ZLinkSpotLocation): unknown {
  return {
    MeshName: row.meshName,
    SpotRid: routingIdHex(row.spotRid),
    SpotType: row.spotType ?? null,
    NodeRid: routingIdHex(row.nodeRid),
    SpotKind: row.spotKind,
    RouteEndpoint: row.routeEndpoint ?? null,
    OwnerId: row.ownerId,
    Generation: Number(row.generation),
    UpdatedAt: row.updatedAt.toISOString()
  };
}

function spotFromJson(json: unknown, generation: bigint, updatedAt: Date): ZLinkSpotLocation {
  const row = objectOf(json);
  return {
    meshName: stringOf(row.MeshName),
    spotRid: ridOf(row.SpotRid),
    spotType: optionalString(row.SpotType),
    nodeRid: ridOf(row.NodeRid),
    spotKind: numberOf(row.SpotKind) as ZLinkSpotKind,
    routeEndpoint: optionalString(row.RouteEndpoint),
    ownerId: stringOf(row.OwnerId),
    generation,
    updatedAt
  };
}

function actorToJson(row: ZLinkActorLocation): unknown {
  return {
    ActorType: row.actorType,
    ActorId: row.actorId,
    ActorRef: row.actorRef,
    NodeRid: routingIdHex(row.nodeRid),
    Generation: Number(row.generation),
    LocationKind: row.locationKind,
    SpotMeshName: row.spotMeshName,
    SpotRid: row.spotRid === undefined ? null : routingIdHex(row.spotRid),
    SpotKind: row.spotKind,
    OwnerId: row.ownerId,
    UpdatedAt: row.updatedAt.toISOString()
  };
}

function actorFromJson(json: unknown, generation: bigint, updatedAt: Date): ZLinkActorLocation {
  const row = objectOf(json);
  return {
    actorType: stringOf(row.ActorType),
    actorId: stringOf(row.ActorId),
    actorRef: stringOf(row.ActorRef),
    nodeRid: ridOf(row.NodeRid),
    generation,
    locationKind: numberOf(row.LocationKind) as ZLinkSpotKind,
    spotMeshName: stringOf(row.SpotMeshName),
    spotRid: optionalRid(row.SpotRid),
    spotKind: numberOf(row.SpotKind) as ZLinkSpotKind,
    ownerId: stringOf(row.OwnerId),
    updatedAt
  };
}

function routeToJson(row: ZLinkRouteLocation): unknown {
  return {
    RouteKind: row.routeKind,
    RouteKey: row.routeKey,
    OwnerNodeRid: routingIdHex(row.ownerNodeRid),
    OwnerId: row.ownerId,
    Generation: Number(row.generation),
    Value: Buffer.from(row.value).toString('base64'),
    UpdatedAt: row.updatedAt.toISOString()
  };
}

function routeFromJson(json: unknown, generation: bigint, updatedAt: Date): ZLinkRouteLocation {
  const row = objectOf(json);
  return {
    routeKind: numberOf(row.RouteKind),
    routeKey: stringOf(row.RouteKey),
    ownerNodeRid: ridOf(row.OwnerNodeRid),
    ownerId: stringOf(row.OwnerId),
    generation,
    value: Buffer.from(stringOf(row.Value), 'base64'),
    updatedAt
  };
}

function materialize<TRow>(
  kind: LocationKind<TRow>,
  fields: readonly unknown[]
): TRow | undefined {
  if (fields[0] === null || fields[0] === undefined) {
    return undefined;
  }
  return kind.fromJson(
    JSON.parse(asString(fields[0])),
    BigInt(asString(fields[1])),
    fromUnixMs(toNumber(fields[2]))
  );
}

function intentName(intent: ZLinkLocationWriteIntent): string {
  switch (intent) {
    case ZLinkLocationWriteIntent.NewClaim:
      return 'new';
    case ZLinkLocationWriteIntent.Renew:
      return 'renew';
    case ZLinkLocationWriteIntent.Takeover:
      return 'takeover';
    default:
      throw new RangeError(`Unknown location write intent: ${intent}`);
  }
}

function toWriteResult(result: readonly unknown[]): ZLinkLocationWriteResult {
  const status = asString(result[0]);
  if (status === 'stored') {
    return stored(BigInt(asString(result[1])), fromUnixMs(toNumber(result[2])));
  }
  if (status === 'conflict') {
    return rejectedConflict();
  }
  return ignoredStale();
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

function storeUnavailable(): ZLinkLocationWriteResult {
  return { status: ZLinkLocationWriteStatus.StoreUnavailable, generation: 0n, updatedAt: new Date(0) };
}

function fromUnixMs(value: number): Date {
  return new Date(value);
}

function routingIdHex(routingId: RoutingId): string {
  if (typeof routingId === 'string') {
    return BindingRoutingId.from(routingId).toHex().toLowerCase();
  }
  return (routingId as unknown as { toHex(): string }).toHex().toLowerCase();
}

function ridOf(value: unknown): RoutingId {
  return BindingRoutingId.fromHex(stringOf(value)) as unknown as RoutingId;
}

function optionalRid(value: unknown): RoutingId | undefined {
  const text = optionalString(value);
  return text === undefined || text.length === 0 ? undefined : BindingRoutingId.fromHex(text) as unknown as RoutingId;
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

function routingIdsEqual(left: RoutingId | undefined, right: RoutingId | undefined): boolean {
  if (left === undefined || right === undefined) {
    return left === right;
  }
  return routingIdHex(left) === routingIdHex(right);
}

function kindTagOf(kind: ZLinkLocationKind): string {
  switch (kind) {
    case ZLinkLocationKind.Peer:
      return kindPeer.tag;
    case ZLinkLocationKind.Spot:
      return kindSpot.tag;
    case ZLinkLocationKind.Actor:
      return kindActor.tag;
    case ZLinkLocationKind.Route:
      return kindRoute.tag;
    default:
      throw new RangeError(`Unknown location kind: ${kind}`);
  }
}

function asArray(value: unknown): readonly unknown[] {
  if (!Array.isArray(value)) {
    throw new TypeError('Redis command returned a non-array value.');
  }
  return value;
}

function asString(value: unknown): string {
  if (typeof value === 'string') {
    return value;
  }
  if (Buffer.isBuffer(value)) {
    return value.toString();
  }
  if (typeof value === 'number' || typeof value === 'bigint') {
    return String(value);
  }
  throw new TypeError('Redis command returned a non-string value.');
}

function toNumber(value: unknown): number {
  return Number(asString(value));
}

function objectOf(value: unknown): Record<string, unknown> {
  if (value === null || typeof value !== 'object' || Array.isArray(value)) {
    throw new TypeError('Location row JSON is not an object.');
  }
  return value as Record<string, unknown>;
}

function stringOf(value: unknown): string {
  if (typeof value !== 'string') {
    throw new TypeError('Location row JSON field is not a string.');
  }
  return value;
}

function optionalString(value: unknown): string | undefined {
  return value === null || value === undefined ? undefined : stringOf(value);
}

function numberOf(value: unknown): number {
  if (typeof value !== 'number') {
    throw new TypeError('Location row JSON field is not a number.');
  }
  return value;
}

function optionalRecord(value: unknown): Readonly<Record<string, string>> | undefined {
  if (value === null || value === undefined) {
    return undefined;
  }
  const row = objectOf(value);
  const result: Record<string, string> = {};
  for (const [key, entry] of Object.entries(row)) {
    result[key] = stringOf(entry);
  }
  return result;
}

function optionalStringArray(value: unknown): readonly string[] | undefined {
  if (value === null || value === undefined) {
    return undefined;
  }
  if (!Array.isArray(value)) {
    throw new TypeError('Location row JSON field is not an array.');
  }
  return value.map(stringOf);
}

const PROLOGUE = `
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
`;

const WRITE_SCRIPT = PROLOGUE + `
local intent = ARGV[1]
local owner = ARGV[2]
local currentOwner = redis.call('HGET', KEYS[1], 'owner')

local function bumpStamps()
    redis.call('INCR', ARGV[8])
    if ARGV[9] ~= '' then redis.call('INCR', ARGV[9]) end
end

local function storeRow(gen)
    redis.call('HSET', KEYS[1],
        'owner', owner, 'gen', gen, 'json', ARGV[4], 'updatedAtMs', nowMs)
    if ARGV[10] == '1' then
        redis.call('HSET', KEYS[1], 'mesh', ARGV[11])
    end
    redis.call('SADD', KEYS[3], ARGV[5])
    redis.call('SADD', ARGV[7] .. owner, ARGV[5])
    if currentOwner and currentOwner ~= owner then
        redis.call('SREM', ARGV[7] .. currentOwner, ARGV[5])
    end
    bumpStamps()
end

if intent == 'new' then
    if currentOwner and redis.call('EXISTS', ARGV[6] .. currentOwner) == 1 then
        return {'conflict', 0, nowMs}
    end
    local gen = redis.call('INCR', KEYS[2])
    storeRow(gen)
    return {'stored', gen, nowMs}
end

if intent == 'takeover' then
    local gen = redis.call('INCR', KEYS[2])
    storeRow(gen)
    return {'stored', gen, nowMs}
end

if currentOwner and currentOwner == owner
    and tonumber(redis.call('HGET', KEYS[1], 'gen')) == tonumber(ARGV[3]) then
    local gen = tonumber(ARGV[3])
    redis.call('HSET', KEYS[1], 'json', ARGV[4], 'updatedAtMs', nowMs)
    bumpStamps()
    return {'stored', gen, nowMs}
end
return {'stale', 0, nowMs}
`;

const REMOVE_SCRIPT = PROLOGUE + `
local currentOwner = redis.call('HGET', KEYS[1], 'owner')
if not currentOwner
    or currentOwner ~= ARGV[1]
    or tonumber(redis.call('HGET', KEYS[1], 'gen')) ~= tonumber(ARGV[2]) then
    return {'stale', 0, nowMs}
end

redis.call('DEL', KEYS[1])
redis.call('SREM', KEYS[2], ARGV[3])
redis.call('SREM', ARGV[4] .. currentOwner, ARGV[3])
redis.call('INCR', ARGV[5])
if ARGV[6] ~= '' then redis.call('INCR', ARGV[6]) end
return {'stored', tonumber(ARGV[2]), nowMs}
`;

const REMOVE_BY_OWNER_SCRIPT = `
if redis.replicate_commands then redis.replicate_commands() end
local rowKeys = redis.call('SMEMBERS', KEYS[1])
local removed = 0
for _, rowKey in ipairs(rowKeys) do
    local rowHash = ARGV[1] .. rowKey
    local mesh = redis.call('HGET', rowHash, 'mesh')
    if redis.call('DEL', rowHash) == 1 then
        removed = removed + 1
        redis.call('SREM', KEYS[2], rowKey)
        if mesh then
            redis.call('INCR', ARGV[2] .. ':' .. mesh)
        end
        redis.call('INCR', ARGV[2])
    end
end
redis.call('DEL', KEYS[1])
return removed
`;

const RENEW_LEASE_SCRIPT = PROLOGUE + `
redis.call('SET', KEYS[1], ARGV[2] .. '|' .. nowMs, 'PX', ARGV[3])
redis.call('SADD', KEYS[2], ARGV[1])
return nowMs
`;

const REMOVE_LEASE_SCRIPT = PROLOGUE + `
redis.call('DEL', KEYS[1])
redis.call('SREM', KEYS[2], ARGV[1])
return nowMs
`;

const LIST_LEASES_SCRIPT = PROLOGUE + `
local owners = redis.call('SMEMBERS', KEYS[1])
local out = {}
for _, ownerId in ipairs(owners) do
    local leaseKey = ARGV[1] .. ownerId
    local pttl = redis.call('PTTL', leaseKey)
    if pttl < 0 then
        redis.call('SREM', KEYS[1], ownerId)
    else
        out[#out + 1] = ownerId
        out[#out + 1] = redis.call('GET', leaseKey)
        out[#out + 1] = pttl
    end
end
return {nowMs, out}
`;
