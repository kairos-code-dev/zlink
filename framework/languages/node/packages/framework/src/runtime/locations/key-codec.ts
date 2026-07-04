import type { RoutingId } from '../../contracts/Common';
import {
  type ZLinkActorLocationKey,
  type ZLinkPeerLocationKey,
  type ZLinkRouteLocationKey,
  type ZLinkSpotLocationKey
} from '../../contracts/Locations';
import { zlinkLocationAutoConnectTypeName, zlinkLocationRoleName } from './canonical-codec';

// Framework bookkeeping codec for in-memory row maps and generation guards.
// Backend extensions keep their own storage codec so transport key layout
// changes do not leak into runtime models.
export const ZLinkLocationKeyCodec = Object.freeze({
  encodePeerKey(key: ZLinkPeerLocationKey): string {
    const identity = key.nodeRid === undefined ? key.endpoint ?? '' : encodeRoutingIdHex(key.nodeRid);
    return encodeSegments(
      zlinkLocationAutoConnectTypeName(key.autoConnectType),
      key.meshName,
      zlinkLocationRoleName(key.role),
      identity
    );
  },

  encodeSpotKey(key: ZLinkSpotLocationKey): string {
    return encodeSegments(key.meshName, encodeRoutingIdHex(key.spotRid));
  },

  encodeActorKey(key: ZLinkActorLocationKey): string {
    return encodeSegments(key.actorId);
  },

  encodeRouteKey(key: ZLinkRouteLocationKey): string {
    return encodeSegments(String(key.routeKind), key.routeKey);
  },

  normalizeActorType
});

export function encodeZLinkLocationKeySegments(...segments: readonly string[]): string {
  return encodeSegments(...segments);
}

function normalizeActorType(actorType: string | undefined): string {
  return actorType ?? '';
}

function encodeSegments(...segments: readonly string[]): string {
  return segments.map((segment) => `${segment.length}:${segment}`).join('');
}

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
