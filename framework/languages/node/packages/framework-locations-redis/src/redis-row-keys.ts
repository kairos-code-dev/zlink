export interface PeerKey {
  readonly autoConnectType: ZLinkLocationAutoConnectType;
  readonly meshName: string;
  readonly role: ZLinkLocationRole;
  readonly nodeRid?: RoutingId;
  readonly endpoint?: string;
}

export interface SpotKey {
  readonly meshName: string;
  readonly spotId: SpotId;
}

export interface MeshNodeKey {
  readonly meshName: string;
  readonly rid: RoutingId;
}

export interface ClientServerServerKey {
  readonly channelName: string;
  readonly serverRid: RoutingId;
}

export interface FanoutPublisherKey {
  readonly channelName: string;
  readonly publisherRid: RoutingId;
}

export interface ActorKey {
  readonly meshName?: string;
  readonly actorId: string;
}

export interface RouteKey {
  readonly routeKind: number;
  readonly routeKey: string;
}

// Redis storage codec. This intentionally duplicates the framework runtime
// bookkeeping codec so Redis key layout stays a backend concern.
export function encodePeerKey(key: PeerKey): string {
  return encodeKeySegments(
    zlinkLocationAutoConnectTypeName(key.autoConnectType),
    key.meshName,
    zlinkLocationRoleName(key.role),
    key.nodeRid === undefined ? key.endpoint ?? '' : routingIdHex(key.nodeRid)
  );
}

export function encodeSpotKey(key: SpotKey): string {
  return encodeKeySegments(key.meshName, requireSpotId(key.spotId));
}

export function encodeMeshNodeKey(key: MeshNodeKey): string {
  return encodeKeySegments(key.meshName, routingIdHex(key.rid));
}

export function encodeClientServerServerKey(key: ClientServerServerKey): string {
  return encodeKeySegments(key.channelName, routingIdHex(key.serverRid));
}

export function encodeFanoutPublisherKey(key: FanoutPublisherKey): string {
  return encodeKeySegments(key.channelName, routingIdHex(key.publisherRid));
}

export function encodeActorKey(key: ActorKey): string {
  return key.meshName === undefined
    ? encodeKeySegments(key.actorId)
    : encodeKeySegments(key.meshName, key.actorId);
}

export function encodeRouteKey(key: RouteKey): string {
  return encodeKeySegments(String(key.routeKind), key.routeKey);
}

export function encodeKeySegments(...segments: readonly string[]): string {
  return segments.map(
    (segment) => `${Buffer.byteLength(segment, 'utf8')}:${segment}`
  ).join('');
}

function requireSpotId(value: string): string {
  const bytes = Buffer.byteLength(value, 'utf8');
  if (bytes < 1 || bytes > 255) {
    throw new TypeError('SpotId must contain 1..255 UTF-8 bytes.');
  }
  return value;
}

function zlinkLocationAutoConnectTypeName(type: ZLinkLocationAutoConnectType): string {
  switch (type) {
    case ZLinkLocationAutoConnectType.RouteMesh: return 'route-mesh';
    case ZLinkLocationAutoConnectType.Fanout: return 'fanout';
    default: throw new RangeError(`Unknown location auto-connect type: ${type}`);
  }
}

function zlinkLocationRoleName(role: ZLinkLocationRole): string {
  switch (role) {
    case ZLinkLocationRole.Router: return 'router';
    case ZLinkLocationRole.Dealer: return 'dealer';
    case ZLinkLocationRole.Pub: return 'pub';
    case ZLinkLocationRole.Sub: return 'sub';
    case ZLinkLocationRole.Spot: return 'spot';
    default: throw new RangeError(`Unknown location role: ${role}`);
  }
}

export function routingIdHex(routingId: RoutingId): string {
  if (typeof routingId === 'string') {
    return BindingRoutingId.from(routingId).toHex().toLowerCase();
  }
  return (routingId as unknown as { toHex(): string }).toHex().toLowerCase();
}

import { RoutingId as BindingRoutingId } from '@zlink-systems/zlink';
import {
  ZLinkLocationRole,
  type RoutingId,
  type SpotId
} from '@zlink-systems/framework';

enum ZLinkLocationAutoConnectType {
  Invalid = 0,
  RouteMesh = 1,
  Fanout = 2
}
