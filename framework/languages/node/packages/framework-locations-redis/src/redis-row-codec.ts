import { RoutingId as BindingRoutingId } from '@zlink-systems/zlink';
import {
  ZLinkLocationKind,
  zlinkSpotKindFromWire,
  zlinkSpotKindToWire,
  type RoutingId,
  type ZLinkActorLocation,
  type ZLinkPeerLocation,
  type ZLinkRouteLocation,
  type ZLinkSpotLocation
} from '@zlink-systems/framework';
import {
  encodeActorKey,
  encodePeerKey,
  encodeRouteKey,
  encodeSpotKey,
  routingIdHex
} from './redis-row-keys';
import { asString, toNumber } from './redis-values';
import { fromUnixMs } from './redis-write-result';

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
    Draining: booleanOrFalse(row.draining),
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
    draining: row.Draining === undefined ? false : booleanOf(row.Draining),
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
    SpotKind: zlinkSpotKindToWire(row.spotKind),
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
    spotKind: zlinkSpotKindFromWire(numberOf(row.SpotKind)),
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
    LocationKind: zlinkSpotKindToWire(row.locationKind),
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
    locationKind: zlinkSpotKindFromWire(numberOf(row.LocationKind)),
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

function booleanOf(value: unknown): boolean {
  if (typeof value !== 'boolean') throw new TypeError('Expected a boolean value.');
  return value;
}

function booleanOrFalse(value: unknown): boolean {
  return value === undefined ? false : booleanOf(value);
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
