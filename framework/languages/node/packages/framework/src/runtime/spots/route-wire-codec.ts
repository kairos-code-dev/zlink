import { RoutingId as BindingRoutingId } from '@zlink-systems/zlink';
import type { RoutingId, ZLinkSessionActor } from '../../contracts';
import { ZLinkSpotKind } from '../../contracts';
import type { ZLinkRemoteActorPacketTarget } from '../actors';
import {
  ZLINK_REMOTE_ACTOR_JOIN_PACKET,
  ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_ERROR_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_RESPONSE_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET
} from '../actors/actor-remote-wire';

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

export function sessionActorPacketTargetKey(actor: ZLinkSessionActor): string {
  return `${String(actor.ref.nodeRid)}:${actor.actorId}:${String(actor.ref.generation)}`;
}

export function decodeRemoteActorPacketRelayPayload(payload: unknown): {
  readonly actorId: string;
  readonly routerChannelId?: string;
  readonly boundSessionTargetNodeRid?: string;
  readonly boundSessionSpotRid?: string;
  readonly header: string;
  readonly payload: string;
} {
  if (
    typeof payload !== 'object' ||
    payload === null ||
    (payload as { packetName?: unknown }).packetName !== ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET ||
    typeof (payload as { actorId?: unknown }).actorId !== 'string' ||
    typeof (payload as { header?: unknown }).header !== 'string' ||
    typeof (payload as { payload?: unknown }).payload !== 'string'
  ) {
    throw new Error('Remote actor packet relay payload is invalid.');
  }
  return {
    actorId: (payload as { actorId: string }).actorId,
    routerChannelId: typeof (payload as { routerChannelId?: unknown }).routerChannelId === 'string'
      ? (payload as { routerChannelId: string }).routerChannelId
      : undefined,
    boundSessionTargetNodeRid: typeof (payload as { boundSessionTargetNodeRid?: unknown }).boundSessionTargetNodeRid === 'string'
      ? (payload as { boundSessionTargetNodeRid: string }).boundSessionTargetNodeRid
      : undefined,
    boundSessionSpotRid: typeof (payload as { boundSessionSpotRid?: unknown }).boundSessionSpotRid === 'string'
      ? (payload as { boundSessionSpotRid: string }).boundSessionSpotRid
      : undefined,
    header: (payload as { header: string }).header,
    payload: (payload as { payload: string }).payload
  };
}

export function decodeRemoteActorJoinPayload(payload: unknown): {
  readonly spotRid: string;
  readonly actorId: string;
  readonly actorType: string;
  readonly actorNodeRid: string;
  readonly actorNodeRidHex?: string;
  readonly actorGeneration: string;
  readonly sourceSpotRid?: string;
  readonly routerChannelId?: string;
  readonly boundSessionRouterChannelId?: string;
  readonly boundSessionTargetNodeRid?: string;
  readonly boundSessionSpotRid?: string;
  readonly request: string;
} {
  if (
    typeof payload !== 'object' ||
    payload === null ||
    (payload as { packetName?: unknown }).packetName !== ZLINK_REMOTE_ACTOR_JOIN_PACKET ||
    typeof (payload as { spotRid?: unknown }).spotRid !== 'string' ||
    typeof (payload as { actorId?: unknown }).actorId !== 'string' ||
    typeof (payload as { actorType?: unknown }).actorType !== 'string' ||
    typeof (payload as { actorNodeRid?: unknown }).actorNodeRid !== 'string' ||
    typeof (payload as { actorGeneration?: unknown }).actorGeneration !== 'string' ||
    typeof (payload as { request?: unknown }).request !== 'string'
  ) {
    throw new Error('Remote actor join payload is invalid.');
  }
  return {
    spotRid: (payload as { spotRid: string }).spotRid,
    actorId: (payload as { actorId: string }).actorId,
    actorType: (payload as { actorType: string }).actorType,
    actorNodeRid: (payload as { actorNodeRid: string }).actorNodeRid,
    actorNodeRidHex: typeof (payload as { actorNodeRidHex?: unknown }).actorNodeRidHex === 'string'
      ? (payload as { actorNodeRidHex: string }).actorNodeRidHex
      : undefined,
    actorGeneration: (payload as { actorGeneration: string }).actorGeneration,
    sourceSpotRid: typeof (payload as { sourceSpotRid?: unknown }).sourceSpotRid === 'string'
      ? (payload as { sourceSpotRid: string }).sourceSpotRid
      : undefined,
    routerChannelId: typeof (payload as { routerChannelId?: unknown }).routerChannelId === 'string'
      ? (payload as { routerChannelId: string }).routerChannelId
      : undefined,
    boundSessionRouterChannelId: typeof (payload as { boundSessionRouterChannelId?: unknown }).boundSessionRouterChannelId === 'string'
      ? (payload as { boundSessionRouterChannelId: string }).boundSessionRouterChannelId
      : undefined,
    boundSessionTargetNodeRid: typeof (payload as { boundSessionTargetNodeRid?: unknown }).boundSessionTargetNodeRid === 'string'
      ? (payload as { boundSessionTargetNodeRid: string }).boundSessionTargetNodeRid
      : undefined,
    boundSessionSpotRid: typeof (payload as { boundSessionSpotRid?: unknown }).boundSessionSpotRid === 'string'
      ? (payload as { boundSessionSpotRid: string }).boundSessionSpotRid
      : undefined,
    request: (payload as { request: string }).request
  };
}

export function decodeRemoteBoundSessionSendPayload(payload: unknown): {
  readonly actorId: string;
  readonly message: unknown;
  readonly boundPacketName?: string;
  readonly metadata?: Record<string, string>;
} {
  if (
    typeof payload !== 'object' ||
    payload === null ||
    (payload as { packetName?: unknown }).packetName !== ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET ||
    typeof (payload as { actorId?: unknown }).actorId !== 'string'
  ) {
    throw new Error('Remote bound session send payload is invalid.');
  }
  return {
    actorId: (payload as { actorId: string }).actorId,
    message: (payload as { message?: unknown }).message,
    boundPacketName: typeof (payload as { boundPacketName?: unknown }).boundPacketName === 'string'
      ? (payload as { boundPacketName: string }).boundPacketName
      : undefined,
    metadata: metadataRecordOf((payload as { metadata?: unknown }).metadata)
  };
}

export function decodeRemoteBoundSessionResponsePayload(payload: unknown): {
  readonly actorId: string;
  readonly message: unknown;
  readonly boundPacketName: string;
  readonly requestSeq: bigint;
  readonly metadata?: Record<string, string>;
  readonly compressPayload: boolean;
  readonly actorPacketTarget?: unknown;
} {
  const decoded = decodeRemoteBoundSessionControlPayload(payload, ZLINK_REMOTE_BOUND_SESSION_RESPONSE_PACKET);
  return {
    actorId: decoded.actorId,
    message: (payload as { message?: unknown }).message,
    boundPacketName: decoded.boundPacketName,
    requestSeq: decoded.requestSeq,
    metadata: decoded.metadata,
    compressPayload: decoded.compressPayload,
    actorPacketTarget: decoded.actorPacketTarget
  };
}

export function decodeRemoteBoundSessionErrorPayload(payload: unknown): {
  readonly actorId: string;
  readonly error: unknown;
  readonly boundPacketName: string;
  readonly requestSeq: bigint;
  readonly metadata?: Record<string, string>;
  readonly actorPacketTarget?: unknown;
} {
  const decoded = decodeRemoteBoundSessionControlPayload(payload, ZLINK_REMOTE_BOUND_SESSION_ERROR_PACKET);
  return {
    actorId: decoded.actorId,
    error: (payload as { error?: unknown }).error,
    boundPacketName: decoded.boundPacketName,
    requestSeq: decoded.requestSeq,
    metadata: decoded.metadata,
    actorPacketTarget: decoded.actorPacketTarget
  };
}

export function streamMetadataMap(metadata: unknown): ReadonlyMap<string, string> {
  if (metadata instanceof Map) {
    return new Map(metadata);
  }
  const maybeValues = metadata as { values?: unknown } | undefined;
  if (maybeValues?.values instanceof Map) {
    return new Map(maybeValues.values);
  }
  return new Map();
}

function decodeRemoteBoundSessionControlPayload(payload: unknown, packetName: string): {
  readonly actorId: string;
  readonly boundPacketName: string;
  readonly requestSeq: bigint;
  readonly metadata?: Record<string, string>;
  readonly compressPayload: boolean;
  readonly actorPacketTarget?: unknown;
} {
  if (
    typeof payload !== 'object' ||
    payload === null ||
    (payload as { packetName?: unknown }).packetName !== packetName ||
    typeof (payload as { actorId?: unknown }).actorId !== 'string' ||
    typeof (payload as { boundPacketName?: unknown }).boundPacketName !== 'string' ||
    typeof (payload as { requestSeq?: unknown }).requestSeq !== 'string'
  ) {
    throw new Error('Remote bound session control payload is invalid.');
  }
  return {
    actorId: (payload as { actorId: string }).actorId,
    boundPacketName: (payload as { boundPacketName: string }).boundPacketName,
    requestSeq: BigInt((payload as { requestSeq: string }).requestSeq),
    metadata: metadataRecordOf((payload as { metadata?: unknown }).metadata),
    compressPayload: (payload as { compressPayload?: unknown }).compressPayload === true,
    actorPacketTarget: (payload as { actorPacketTarget?: unknown }).actorPacketTarget
  };
}

function metadataRecordOf(rawMetadata: unknown): Record<string, string> {
  const metadata: Record<string, string> = {};
  if (typeof rawMetadata === 'object' && rawMetadata !== null) {
    for (const [key, value] of Object.entries(rawMetadata)) {
      if (typeof value === 'string') {
        metadata[key] = value;
      }
    }
  }
  return metadata;
}
