import { RoutingId as BindingRoutingId } from '@zlink-systems/zlink';
import type { RoutingId } from '../../contracts';
import { ZLinkSpotKind } from '../../contracts';
import type { ZLinkRemoteActorPacketTarget } from '../actors';

export interface ZLinkSpotRouteBridgeReplyPayload {
  readonly ok: boolean;
  readonly response?: unknown;
  readonly error?: unknown;
  readonly deferredResponse?: boolean;
  readonly actorPacketTarget?: ZLinkRemoteActorPacketTargetWire;
}

export interface ZLinkRemoteActorPacketTargetWire {
  readonly routerChannelId: string;
  readonly targetNodeRid: string;
  readonly targetNodeRidHex?: string;
  readonly spotRid: string;
  readonly spotRidHex?: string;
  readonly spotKind: ZLinkSpotKind;
}

export function encodeSpotRouteBridgeReply(payload: ZLinkSpotRouteBridgeReplyPayload): ZLinkSpotRouteBridgeReplyPayload {
  return payload;
}

export function isSpotRouteBridgeReplyPayload(value: unknown): boolean {
  return typeof value === 'object' &&
    value !== null &&
    (
      'ok' in value ||
      'response' in value ||
      'error' in value ||
      'actorPacketTarget' in value
    );
}

export function encodeRemoteActorPacketTarget(
  target: ZLinkRemoteActorPacketTarget | undefined
): ZLinkRemoteActorPacketTargetWire | undefined {
  if (target === undefined) {
    return undefined;
  }
  return {
    routerChannelId: target.routerChannelId,
    targetNodeRid: String(target.targetNodeRid),
    targetNodeRidHex: encodeRoutingIdHex(target.targetNodeRid),
    spotRid: String(target.spotRid),
    spotRidHex: encodeRoutingIdHex(target.spotRid),
    spotKind: target.spotKind ?? ZLinkSpotKind.User
  };
}

export function decodeRemoteActorPacketTarget(value: unknown): ZLinkRemoteActorPacketTarget | undefined {
  if (
    typeof value !== 'object' ||
    value === null ||
    typeof (value as { routerChannelId?: unknown }).routerChannelId !== 'string' ||
    typeof (value as { targetNodeRid?: unknown }).targetNodeRid !== 'string' ||
    typeof (value as { spotRid?: unknown }).spotRid !== 'string'
  ) {
    return undefined;
  }
  return {
    routerChannelId: (value as { routerChannelId: string }).routerChannelId,
    targetNodeRid: decodeWireRoutingId(
      (value as { targetNodeRid: string }).targetNodeRid,
      (value as { targetNodeRidHex?: unknown }).targetNodeRidHex
    ),
    spotRid: decodeWireRoutingId(
      (value as { spotRid: string }).spotRid,
      (value as { spotRidHex?: unknown }).spotRidHex
    ),
    spotKind: (value as { spotKind?: unknown }).spotKind === ZLinkSpotKind.Entry
      ? ZLinkSpotKind.Entry
      : ZLinkSpotKind.User
  };
}

export function normalizeRuntimeRoutingId(value: RoutingId | string): RoutingId {
  const raw = value as unknown;
  return raw instanceof BindingRoutingId
    ? raw as unknown as RoutingId
    : BindingRoutingId.from(String(value)) as unknown as RoutingId;
}

export function decodeWireRoutingId(text: string, hex: unknown): RoutingId {
  return typeof hex === 'string'
    ? BindingRoutingId.fromHex(hex) as unknown as RoutingId
    : normalizeRuntimeRoutingId(text);
}

export function encodeRoutingIdHex(routingId: RoutingId): string | undefined {
  const toHex = (routingId as unknown as { toHex?: () => string }).toHex;
  return typeof toHex === 'function' ? toHex.call(routingId) : undefined;
}
