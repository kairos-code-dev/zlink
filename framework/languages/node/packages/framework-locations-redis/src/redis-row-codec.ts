import { RoutingId as BindingRoutingId } from '@zlink-systems/zlink';
import {
  ZLinkLocationKind,
  zlinkSpotKindFromWire,
  zlinkSpotKindToWire,
  type RoutingId,
  type ZLinkActorLocation,
  type ZLinkMeshNodeDescriptor,
  type ZLinkPeerLocation,
  type ZLinkRouteLocation,
  type ZLinkSpotLocation
} from '@zlink-systems/framework';
import {
  encodeActorKey,
  encodeMeshNodeKey,
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
  toJsonText?(row: TRow): string;
  fromJson(json: unknown, generation: bigint, updatedAt: Date): TRow;
  fromJsonText?(json: string, generation: bigint, updatedAt: Date): TRow;
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

export const kindMeshNode: LocationKind<ZLinkMeshNodeDescriptor> = {
  tag: 'mesh',
  encodeKey: (row) => encodeMeshNodeKey({ meshName: row.meshName, rid: row.rid }),
  meshOf: (row) => row.meshName,
  ownerOf: (row) => row.ownerId,
  generationOf: () => 0n,
  toJson: meshNodeToJson,
  toJsonText: meshNodeToJsonText,
  fromJson: meshNodeFromJson,
  fromJsonText: (json, generation, updatedAt) => meshNodeFromJson(
    JSON.parse(json.replace(
      /("(?:LifecycleGeneration|DescriptorRevision)":)([0-9]+)/g,
      '$1"$2"'
    )),
    generation,
    updatedAt
  )
};

export const kindSpot: LocationKind<ZLinkSpotLocation> = {
  tag: 'spot',
  encodeKey: (row) => encodeSpotKey({ meshName: row.meshName, spotRid: row.spotRid }),
  meshOf: (row) => row.meshName,
  ownerOf: (row) => row.ownerId,
  generationOf: () => 0n,
  toJson: spotToJson,
  toJsonText: spotToJsonText,
  fromJson: spotFromJson,
  fromJsonText: (json, generation, updatedAt) => spotFromJson(
    JSON.parse(json.replace(
      /("(?:SpotGeneration|OwnerNodeGeneration)":)([0-9]+)/g,
      '$1"$2"'
    )),
    generation,
    updatedAt
  )
};

export const kindActor: LocationKind<ZLinkActorLocation> = {
  tag: 'actor',
  encodeKey: (row) => encodeActorKey({
    meshName: row.meshName,
    actorId: row.actorId
  }),
  meshOf: (row) => row.meshName,
  ownerOf: (row) => row.ownerId,
  generationOf: () => 0n,
  toJson: actorToJson,
  toJsonText: actorToJsonText,
  fromJson: actorFromJson,
  fromJsonText: actorFromJsonText
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

function meshNodeToJson(row: ZLinkMeshNodeDescriptor): unknown {
  return {
    MeshName: row.meshName,
    Rid: routingIdHex(row.rid),
    LifecycleGeneration: requiredUnsignedNumber(row.lifecycleGeneration, 'LifecycleGeneration'),
    DescriptorRevision: requiredUnsignedNumber(row.descriptorRevision, 'DescriptorRevision'),
    Endpoint: row.endpoint,
    ChannelWeights: sortedWeights(row.channelWeights),
    Draining: row.draining,
    SecurityIdentity: row.securityIdentity,
    OwnerId: row.ownerId,
    UpdatedAt: formatDotNetDateTimeOffset(row.updatedAt)
  };
}

function meshNodeToJsonText(row: ZLinkMeshNodeDescriptor): string {
  return [
    '{',
    `"MeshName":${JSON.stringify(row.meshName)}`,
    `,"Rid":${JSON.stringify(routingIdHex(row.rid))}`,
    `,"LifecycleGeneration":${requiredUnsigned(row.lifecycleGeneration, 'LifecycleGeneration')}`,
    `,"DescriptorRevision":${requiredUnsigned(row.descriptorRevision, 'DescriptorRevision')}`,
    `,"Endpoint":${JSON.stringify(row.endpoint)}`,
    `,"ChannelWeights":${JSON.stringify(sortedWeights(row.channelWeights))}`,
    `,"Draining":${row.draining ? 'true' : 'false'}`,
    `,"SecurityIdentity":${JSON.stringify(row.securityIdentity)}`,
    `,"OwnerId":${JSON.stringify(row.ownerId)}`,
    `,"UpdatedAt":${JSON.stringify(formatDotNetDateTimeOffset(row.updatedAt))}`,
    '}'
  ].join('');
}

function meshNodeFromJson(
  json: unknown,
  _generation: bigint,
  updatedAt: Date
): ZLinkMeshNodeDescriptor {
  const row = objectOf(json);
  const weights = objectOf(row.ChannelWeights);
  return {
    meshName: stringOf(row.MeshName),
    rid: ridOf(row.Rid),
    lifecycleGeneration: unsignedBigIntOf(row.LifecycleGeneration),
    descriptorRevision: unsignedBigIntOf(row.DescriptorRevision),
    endpoint: stringOf(row.Endpoint),
    channelWeights: Object.fromEntries(
      Object.entries(weights).map(([name, weight]) => [name, numberOf(weight)])
    ),
    draining: booleanOf(row.Draining),
    securityIdentity: stringOf(row.SecurityIdentity),
    ownerId: stringOf(row.OwnerId),
    updatedAt
  };
}

function sortedWeights(weights: Readonly<Record<string, number>>): Readonly<Record<string, number>> {
  return Object.fromEntries(
    Object.entries(weights).sort(([left], [right]) => left.localeCompare(right))
  );
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
    SpotGeneration: requiredUnsignedNumber(row.spotGeneration, 'SpotGeneration'),
    OwnerNodeRid: routingIdHex(row.ownerNodeRid),
    OwnerNodeGeneration: requiredUnsignedNumber(row.ownerNodeGeneration, 'OwnerNodeGeneration'),
    SpotKind: zlinkSpotKindToWire(row.spotKind),
    SpotType: row.spotType,
    OwnerId: row.ownerId,
    UpdatedAt: formatDotNetDateTimeOffset(row.updatedAt)
  };
}

function spotToJsonText(row: ZLinkSpotLocation): string {
  return [
    '{',
    `"MeshName":${JSON.stringify(row.meshName)}`,
    `,"SpotRid":${JSON.stringify(routingIdHex(row.spotRid))}`,
    `,"SpotGeneration":${requiredUnsigned(row.spotGeneration, 'SpotGeneration')}`,
    `,"OwnerNodeRid":${JSON.stringify(routingIdHex(row.ownerNodeRid))}`,
    `,"OwnerNodeGeneration":${requiredUnsigned(row.ownerNodeGeneration, 'OwnerNodeGeneration')}`,
    `,"SpotKind":${zlinkSpotKindToWire(row.spotKind)}`,
    `,"SpotType":${JSON.stringify(row.spotType)}`,
    `,"OwnerId":${JSON.stringify(row.ownerId)}`,
    `,"UpdatedAt":${JSON.stringify(formatDotNetDateTimeOffset(row.updatedAt))}`,
    '}'
  ].join('');
}

function spotFromJson(json: unknown, _generation: bigint, updatedAt: Date): ZLinkSpotLocation {
  const row = objectOf(json);
  const ownerNodeRid = ridOf(row.OwnerNodeRid);
  return {
    meshName: stringOf(row.MeshName),
    spotRid: ridOf(row.SpotRid),
    spotGeneration: unsignedBigIntOf(row.SpotGeneration),
    spotType: stringOf(row.SpotType),
    ownerNodeRid,
    ownerNodeGeneration: unsignedBigIntOf(row.OwnerNodeGeneration),
    spotKind: zlinkSpotKindFromWire(numberOf(row.SpotKind)),
    ownerId: stringOf(row.OwnerId),
    updatedAt
  };
}

function actorToJson(row: ZLinkActorLocation): unknown {
  return {
    MeshName: row.meshName,
    ActorId: row.actorId,
    ActorType: row.actorType,
    ActorRef: actorRefToJson(row.actorRef),
    OwnerNodeRid: routingIdHex(row.ownerNodeRid),
    OwnerNodeGeneration: requiredUnsignedNumber(row.ownerNodeGeneration, 'OwnerNodeGeneration'),
    SpotRid: routingIdHex(row.spotRid),
    SpotGeneration: requiredUnsignedNumber(row.spotGeneration, 'SpotGeneration'),
    SpotKind: zlinkSpotKindToWire(row.spotKind),
    MembershipEpoch: requiredUnsignedNumber(row.membershipEpoch, 'MembershipEpoch'),
    OwnerId: row.ownerId,
    UpdatedAt: formatDotNetDateTimeOffset(row.updatedAt)
  };
}

function actorToJsonText(row: ZLinkActorLocation): string {
  const actorRef = row.actorRef;
  const ownerNodeGeneration = requiredUnsigned(row.ownerNodeGeneration, 'OwnerNodeGeneration');
  const spotRid = requiredValue(row.spotRid, 'SpotRid');
  const spotGeneration = requiredUnsigned(row.spotGeneration, 'SpotGeneration');
  const membershipEpoch = requiredUnsigned(row.membershipEpoch, 'MembershipEpoch');
  return [
    '{',
    `"MeshName":${JSON.stringify(row.meshName)}`,
    `,"ActorId":${JSON.stringify(row.actorId)}`,
    `,"ActorType":${JSON.stringify(requiredString(row.actorType, 'ActorType'))}`,
    ',"ActorRef":{',
    `"nodeRid":${JSON.stringify(routingIdHex(actorRef.nodeRid))}`,
    `,"actorId":${JSON.stringify(actorRef.actorId)}`,
    `,"generation":${requiredUnsigned(actorRef.generation, 'ActorRef.generation')}`,
    '}',
    `,"OwnerNodeRid":${JSON.stringify(routingIdHex(row.ownerNodeRid))}`,
    `,"OwnerNodeGeneration":${ownerNodeGeneration}`,
    `,"SpotRid":${JSON.stringify(routingIdHex(spotRid))}`,
    `,"SpotGeneration":${spotGeneration}`,
    `,"SpotKind":${zlinkSpotKindToWire(row.spotKind)}`,
    `,"MembershipEpoch":${membershipEpoch}`,
    `,"OwnerId":${JSON.stringify(row.ownerId)}`,
    `,"UpdatedAt":${JSON.stringify(formatDotNetDateTimeOffset(row.updatedAt))}`,
    '}'
  ].join('');
}

function actorFromJsonText(json: string, generation: bigint, updatedAt: Date): ZLinkActorLocation {
  const lossless = json.replace(
    /("(?:generation|OwnerNodeGeneration|SpotGeneration|MembershipEpoch)":)([0-9]+)/g,
    '$1"$2"'
  );
  return actorFromJson(JSON.parse(lossless), generation, updatedAt);
}

function actorFromJson(json: unknown, _generation: bigint, updatedAt: Date): ZLinkActorLocation {
  const row = objectOf(json);
  const ownerNodeRid = ridOf(row.OwnerNodeRid);
  const spotKind = zlinkSpotKindFromWire(numberOf(row.SpotKind));
  return {
    meshName: stringOf(row.MeshName),
    actorType: stringOf(row.ActorType),
    actorId: stringOf(row.ActorId),
    actorRef: actorRefOf(row.ActorRef),
    ownerNodeRid,
    ownerNodeGeneration: unsignedBigIntOf(row.OwnerNodeGeneration),
    spotKind,
    spotRid: ridOf(row.SpotRid),
    spotGeneration: unsignedBigIntOf(row.SpotGeneration),
    membershipEpoch: unsignedBigIntOf(row.MembershipEpoch),
    ownerId: stringOf(row.OwnerId),
    updatedAt
  };
}

function actorRefToJson(actorRef: ZLinkActorLocation['actorRef']): unknown {
  const value = requiredValue(actorRef, 'ActorRef');
  return {
    nodeRid: routingIdHex(value.nodeRid),
    actorId: value.actorId,
    generation: requiredUnsignedNumber(value.generation, 'ActorRef.generation')
  };
}

function requiredValue<T>(value: T | null | undefined, field: string): T {
  if (value === null || value === undefined) {
    throw new TypeError(`${field} is required by the exact Actor location contract.`);
  }
  return value;
}

function requiredString(value: string | null | undefined, field: string): string {
  return requiredValue(value, field);
}

function requiredUnsignedNumber(value: bigint | undefined, field: string): number {
  const exact = requiredValue(value, field);
  if (exact < 0n || exact > BigInt(Number.MAX_SAFE_INTEGER)) {
    throw new RangeError(`${field} cannot be represented as an exact JSON integer.`);
  }
  return Number(exact);
}

function unsignedBigIntOf(value: unknown): bigint {
  const result = typeof value === 'string'
    ? BigInt(value)
    : BigInt(numberOf(value));
  if (result < 0n) throw new RangeError('Location row generation is unsigned.');
  return result;
}

function requiredUnsigned(value: bigint | undefined, field: string): string {
  const exact = requiredValue(value, field);
  if (exact < 0n) throw new RangeError(`${field} is unsigned.`);
  return exact.toString();
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
  const json = asString(fields[0]);
  const generation = BigInt(asString(fields[1]));
  const updatedAt = fromUnixMs(toNumber(fields[2]));
  return kind.fromJsonText === undefined
    ? kind.fromJson(JSON.parse(json), generation, updatedAt)
    : kind.fromJsonText(json, generation, updatedAt);
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
    throw new TypeError('ActorRef is required by the exact Actor location contract.');
  }
  const row = objectOf(value);
  return {
    nodeRid: ridOf(row.nodeRid),
    actorId: stringOf(row.actorId),
    generation: unsignedBigIntOf(row.generation)
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
