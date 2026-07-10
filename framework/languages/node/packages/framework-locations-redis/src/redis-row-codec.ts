import { RoutingId as BindingRoutingId } from '@zlink-systems/zlink';
import {
  ZLinkLocationKind,
  ZLinkLocationWriteIntent,
  ZLinkLocationWriteStatus,
  ZLinkSpotKind,
  type RoutingId,
  type ZLinkActorLocation,
  type ZLinkActorLocationFilter,
  type ZLinkLocationWriteResult,
  type ZLinkPeerLocation,
  type ZLinkPeerLocationFilter,
  type ZLinkRouteLocation,
  type ZLinkRouteLocationFilter,
  type ZLinkSpotLocation,
  type ZLinkSpotLocationFilter
} from '@zlink-systems/framework';
import {
  encodeActorKey,
  encodePeerKey,
  encodeRouteKey,
  encodeSpotKey,
  routingIdHex
} from './redis-row-keys';

export interface LocationKind<TRow> {
  readonly tag: string;
  encodeKey(row: TRow): string;
  meshOf(row: TRow): string | undefined;
  ownerOf(row: TRow): string;
  generationOf(row: TRow): bigint;
  toJson(row: TRow): unknown;
  fromJson(json: unknown, generation: bigint, updatedAt: Date): TRow;
}

export const kindPeer: LocationKind<ZLinkPeerLocation> = {
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

export const kindSpot: LocationKind<ZLinkSpotLocation> = {
  tag: 'spot',
  encodeKey: (row) => encodeSpotKey({ meshName: row.meshName, spotRid: row.spotRid }),
  meshOf: (row) => row.meshName,
  ownerOf: (row) => row.ownerId,
  generationOf: (row) => row.generation,
  toJson: spotToJson,
  fromJson: spotFromJson
};

export const kindActor: LocationKind<ZLinkActorLocation> = {
  tag: 'actor',
  encodeKey: (row) => encodeActorKey({ actorId: row.actorId }),
  meshOf: () => undefined,
  ownerOf: (row) => row.ownerId,
  generationOf: (row) => row.generation,
  toJson: actorToJson,
  fromJson: actorFromJson
};

export const kindRoute: LocationKind<ZLinkRouteLocation> = {
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
    Metadata: jsonRecordOrNull(peerMetadataOf(row)),
    Capabilities: jsonStringArrayOrNull(peerCapabilitiesOf(row)),
    OwnerId: row.ownerId,
    Generation: Number(row.generation),
    UpdatedAt: formatDotNetDateTimeOffset(row.updatedAt)
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
    UpdatedAt: formatDotNetDateTimeOffset(row.updatedAt)
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
    ActorId: row.actorId,
    ActorType: row.actorType ?? null,
    ActorRef: row.actorRef == null
      ? null
      : {
          nodeRid: routingIdHex(row.actorRef.nodeRid),
          actorId: row.actorRef.actorId,
          generation: Number(row.actorRef.generation)
        },
    NodeRid: routingIdHex(row.nodeRid),
    LocationKind: row.locationKind,
    SpotMeshName: row.spotMeshName,
    SpotRid: row.spotRid == null ? null : routingIdHex(row.spotRid),
    OwnerId: row.ownerId,
    Generation: Number(row.generation),
    UpdatedAt: formatDotNetDateTimeOffset(row.updatedAt)
  };
}

function actorFromJson(json: unknown, generation: bigint, updatedAt: Date): ZLinkActorLocation {
  const row = objectOf(json);
  return {
    actorType: optionalString(row.ActorType),
    actorId: stringOf(row.ActorId),
    actorRef: actorRefOf(row.ActorRef),
    nodeRid: ridOf(row.NodeRid),
    generation,
    locationKind: numberOf(row.LocationKind) as ZLinkSpotKind,
    spotMeshName: stringOf(row.SpotMeshName),
    spotRid: optionalRid(row.SpotRid),
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
    UpdatedAt: formatDotNetDateTimeOffset(row.updatedAt)
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

function peerMetadataOf(row: ZLinkPeerLocation): unknown {
  return row.metadata ?? (row as { readonly Metadata?: unknown }).Metadata;
}

function peerCapabilitiesOf(row: ZLinkPeerLocation): unknown {
  return row.capabilities ?? (row as { readonly Capabilities?: unknown }).Capabilities;
}

function jsonRecordOrNull(value: unknown): Record<string, string> | null {
  if (value === undefined || value === null) {
    return null;
  }
  if (value instanceof Map) {
    return Object.fromEntries([...value].map(([key, item]) => [String(key), String(item)]));
  }
  if (typeof value !== 'object') {
    return null;
  }
  return Object.fromEntries(
    Object.entries(value as Record<string, unknown>)
      .map(([key, item]) => [key, String(item)])
  );
}

function jsonStringArrayOrNull(value: unknown): string[] | null {
  if (value === undefined || value === null) {
    return null;
  }
  if (!Array.isArray(value)) {
    return null;
  }
  return value.map(String);
}

function formatDotNetDateTimeOffset(value: Date): string {
  if (value.getTime() === 0) {
    return '0001-01-01T00:00:00+00:00';
  }
  const iso = value.toISOString();
  const normalized = iso.endsWith('.000Z')
    ? iso.slice(0, -5)
    : iso.slice(0, -1);
  return `${normalized}+00:00`;
}

export function materialize<TRow>(
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

export function intentName(intent: ZLinkLocationWriteIntent): string {
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

export function toWriteResult(result: readonly unknown[]): ZLinkLocationWriteResult {
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

export function fromUnixMs(value: number): Date {
  return new Date(value);
}

export function ridOf(value: unknown): RoutingId {
  return BindingRoutingId.fromHex(stringOf(value)) as unknown as RoutingId;
}

function optionalRid(value: unknown): RoutingId | undefined {
  const text = optionalString(value);
  return text === undefined || text.length === 0 ? undefined : BindingRoutingId.fromHex(text) as unknown as RoutingId;
}

function actorRefOf(value: unknown): ZLinkActorLocation['actorRef'] {
  if (value === null || value === undefined) {
    return undefined;
  }
  const row = objectOf(value);
  return {
    nodeRid: ridOf(row.nodeRid),
    actorId: stringOf(row.actorId),
    generation: BigInt(numberOf(row.generation))
  };
}

export function matchesPeer(row: ZLinkPeerLocation, filter: ZLinkPeerLocationFilter): boolean {
  return (filter.autoConnectType === undefined || row.autoConnectType === filter.autoConnectType)
    && (filter.meshName === undefined || row.meshName === filter.meshName)
    && (filter.role === undefined || row.role === filter.role)
    && (filter.nodeRid === undefined || routingIdsEqual(row.nodeRid, filter.nodeRid))
    && (filter.endpoint === undefined || row.endpoint === filter.endpoint);
}

export function matchesSpot(row: ZLinkSpotLocation, filter: ZLinkSpotLocationFilter): boolean {
  return (filter.meshName === undefined || row.meshName === filter.meshName)
    && (filter.spotType === undefined || row.spotType === filter.spotType)
    && (filter.nodeRid === undefined || routingIdsEqual(row.nodeRid, filter.nodeRid))
    && (filter.spotKind === undefined || row.spotKind === filter.spotKind);
}

export function matchesActor(row: ZLinkActorLocation, filter: ZLinkActorLocationFilter): boolean {
  return (filter.actorType === undefined || row.actorType === filter.actorType)
    && (filter.nodeRid === undefined || routingIdsEqual(row.nodeRid, filter.nodeRid))
    && (filter.spotRid === undefined || routingIdsEqual(row.spotRid, filter.spotRid))
    && (filter.locationKind === undefined || row.locationKind === filter.locationKind);
}

export function matchesRoute(row: ZLinkRouteLocation, filter: ZLinkRouteLocationFilter): boolean {
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

export function kindTagOf(kind: ZLinkLocationKind): string {
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

export function asArray(value: unknown): readonly unknown[] {
  if (!Array.isArray(value)) {
    throw new TypeError('Redis command returned a non-array value.');
  }
  return value;
}

export function asString(value: unknown): string {
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

export function toNumber(value: unknown): number {
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
