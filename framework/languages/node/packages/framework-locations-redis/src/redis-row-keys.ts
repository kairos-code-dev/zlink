export interface PeerKey {
  readonly autoConnectType: ZLinkLocationAutoConnectType;
  readonly meshName: string;
  readonly role: ZLinkLocationRole;
  readonly nodeRid?: RoutingId;
  readonly endpoint?: string;
}

export interface SpotKey {
  readonly meshName: string;
  readonly spotRid: RoutingId;
}

export interface ActorKey {
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
  return encodeKeySegments(key.meshName, routingIdHex(key.spotRid));
}

export function encodeActorKey(key: ActorKey): string {
  return encodeKeySegments(key.actorId);
}

export function encodeRouteKey(key: RouteKey): string {
  return encodeKeySegments(String(key.routeKind), key.routeKey);
}

export function encodeKeySegments(...segments: readonly string[]): string {
  return segments.map((segment) => `${segment.length}:${segment}`).join('');
}

function zlinkLocationAutoConnectTypeName(type: ZLinkLocationAutoConnectType): string {
  switch (type) {
    case ZLinkLocationAutoConnectType.RouteMesh: return 'route-mesh';
    case ZLinkLocationAutoConnectType.ClientServer: return 'client-server';
    case ZLinkLocationAutoConnectType.DealerMesh: return 'dealer-mesh';
    case ZLinkLocationAutoConnectType.Fanout: return 'fanout';
    case ZLinkLocationAutoConnectType.SpotMesh: return 'spot-mesh';
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
  ZLinkLocationAutoConnectType,
  ZLinkLocationRole,
  type RoutingId
} from '@zlink-systems/framework';
