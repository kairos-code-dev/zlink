import { Message as BindingMessage } from '@zlink-systems/zlink';
import type { ActorRef } from '../../contracts';
import {
  decodeChannelEnvelope,
  decodeChannelPayload,
  type ZLinkChannelEnvelopeCodecRegistry
} from '../channels/channel-envelope';
import {
  ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_ERROR_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_OWNERSHIP_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_RESPONSE_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET
} from '../actors';
import { decodeWireRoutingId } from '../actors/actor-remote-wire';

export interface ZLinkRemoteBoundSessionSend {
  readonly actorId: string;
  readonly message: unknown;
  readonly packetName?: string;
  readonly metadata: ReadonlyMap<string, string>;
  readonly actorRef?: ActorRef;
  readonly envelope?: ReturnType<typeof decodeChannelEnvelope>;
}

export interface ZLinkRemoteBoundSessionResponse {
  readonly actorId: string;
  readonly message: unknown;
  readonly packetName: string;
  readonly requestSeq: bigint;
  readonly metadata: ReadonlyMap<string, string>;
  readonly compressPayload: boolean;
  readonly actorPacketTarget?: unknown;
}

export interface ZLinkRemoteBoundSessionError {
  readonly actorId: string;
  readonly error: unknown;
  readonly packetName: string;
  readonly requestSeq: bigint;
  readonly metadata: ReadonlyMap<string, string>;
  readonly actorPacketTarget?: unknown;
}

export interface ZLinkRemoteBoundSessionOwnership {
  readonly actorId: string;
  readonly actorNodeRid: string;
  readonly actorNodeRidHex?: string;
  readonly actorGeneration: string;
  readonly actorOwnershipGeneration: string;
}

export function decodeRemoteBoundSessionOwnership(
  parts: readonly BindingMessage[],
  codecs?: ZLinkChannelEnvelopeCodecRegistry
): ZLinkRemoteBoundSessionOwnership | undefined {
  try {
    const envelope = decodeChannelEnvelope(parts);
    if (envelope.packetName !== ZLINK_REMOTE_BOUND_SESSION_OWNERSHIP_PACKET) return undefined;
    const payload = decodeChannelPayload(envelope, codecs) as Record<string, unknown>;
    if (
      typeof payload.actorId !== 'string' ||
      typeof payload.actorNodeRid !== 'string' ||
      typeof payload.actorGeneration !== 'string' ||
      typeof payload.actorOwnershipGeneration !== 'string'
    ) {
      return undefined;
    }
    return {
      actorId: payload.actorId,
      actorNodeRid: payload.actorNodeRid,
      actorNodeRidHex: typeof payload.actorNodeRidHex === 'string' ? payload.actorNodeRidHex : undefined,
      actorGeneration: payload.actorGeneration,
      actorOwnershipGeneration: payload.actorOwnershipGeneration
    };
  } catch {
    return undefined;
  }
}

export interface ZLinkRemoteActorPacketRelay {
  readonly actorId: string;
  readonly routerChannelId?: string;
  readonly boundSessionTargetNodeRid?: string;
  readonly boundSessionSpotRid?: string;
  readonly header: string;
  readonly payload: string;
  readonly actorRef?: ActorRef;
  readonly envelope?: ReturnType<typeof decodeChannelEnvelope>;
}

export function decodeRemoteBoundSessionSend(
  parts: readonly BindingMessage[],
  codecs?: ZLinkChannelEnvelopeCodecRegistry
): ZLinkRemoteBoundSessionSend | undefined {
  try {
    if (parts.length === 1) {
      const payload = JSON.parse(parts[0].data().toString()) as {
        readonly packetName?: unknown;
        readonly boundPacketName?: unknown;
        readonly actorId?: unknown;
        readonly actorNodeRid?: unknown;
        readonly actorNodeRidHex?: unknown;
        readonly actorGeneration?: unknown;
        readonly handoffTargetSpotRid?: unknown;
        readonly actorOwnershipGeneration?: unknown;
        readonly message?: unknown;
        readonly metadata?: unknown;
      };
      if (
        payload.packetName !== ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET ||
        typeof payload.actorId !== 'string'
      ) {
        return undefined;
      }
      return {
        actorId: payload.actorId,
        message: payload.message,
        packetName: typeof payload.boundPacketName === 'string' ? payload.boundPacketName : undefined,
        metadata: metadataOf(payload.metadata),
        actorRef: decodeActorRef(payload)
      };
    }
    const envelope = decodeChannelEnvelope(parts);
    if (envelope.packetName !== ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET) {
      return undefined;
    }
    const payload = decodeChannelPayload(envelope, codecs) as {
      readonly actorId?: unknown;
      readonly actorNodeRid?: unknown;
      readonly actorNodeRidHex?: unknown;
      readonly actorGeneration?: unknown;
      readonly actorOwnershipGeneration?: unknown;
      readonly message?: unknown;
      readonly packetName?: unknown;
      readonly boundPacketName?: unknown;
      readonly metadata?: unknown;
    };
    if (typeof payload.actorId !== 'string') {
      return undefined;
    }
    return {
      actorId: payload.actorId,
      message: payload.message,
      packetName: typeof payload.boundPacketName === 'string' ? payload.boundPacketName : undefined,
      metadata: metadataOf(payload.metadata),
      actorRef: decodeActorRef(payload),
      envelope
    };
  } catch {
    return undefined;
  }
}

function decodeActorRef(payload: {
  readonly actorId?: unknown;
  readonly actorNodeRid?: unknown;
  readonly actorNodeRidHex?: unknown;
  readonly actorGeneration?: unknown;
  readonly actorOwnershipGeneration?: unknown;
}): ActorRef | undefined {
  if (
    typeof payload.actorId !== 'string'
    || typeof payload.actorNodeRid !== 'string'
    || typeof payload.actorGeneration !== 'string'
  ) {
    return undefined;
  }
  const actorRef = {
    actorId: payload.actorId,
    nodeRid: decodeWireRoutingId(
      payload.actorNodeRid,
      typeof payload.actorNodeRidHex === 'string' ? payload.actorNodeRidHex : undefined
    ),
    generation: BigInt(payload.actorGeneration)
  } as ActorRef & { ownershipGeneration?: bigint };
  if (typeof payload.actorOwnershipGeneration === 'string') {
    actorRef.ownershipGeneration = BigInt(payload.actorOwnershipGeneration);
  }
  return actorRef;
}

export function decodeRemoteBoundSessionResponse(
  parts: readonly BindingMessage[],
  codecs?: ZLinkChannelEnvelopeCodecRegistry
): ZLinkRemoteBoundSessionResponse | undefined {
  const decoded = decodeRemoteBoundSessionControl(parts, ZLINK_REMOTE_BOUND_SESSION_RESPONSE_PACKET, codecs);
  if (decoded === undefined) {
    return undefined;
  }
  return {
    actorId: decoded.actorId,
    message: decoded.payload.message,
    packetName: decoded.packetName,
    requestSeq: decoded.requestSeq,
    metadata: decoded.metadata,
    compressPayload: decoded.payload.compressPayload === true,
    actorPacketTarget: decoded.actorPacketTarget
  };
}

export function decodeRemoteBoundSessionError(
  parts: readonly BindingMessage[],
  codecs?: ZLinkChannelEnvelopeCodecRegistry
): ZLinkRemoteBoundSessionError | undefined {
  const decoded = decodeRemoteBoundSessionControl(parts, ZLINK_REMOTE_BOUND_SESSION_ERROR_PACKET, codecs);
  if (decoded === undefined) {
    return undefined;
  }
  return {
    actorId: decoded.actorId,
    error: decoded.payload.error,
    packetName: decoded.packetName,
    requestSeq: decoded.requestSeq,
    metadata: decoded.metadata,
    actorPacketTarget: decoded.actorPacketTarget
  };
}

export function decodeRemoteActorPacketRelay(
  parts: readonly BindingMessage[],
  codecs?: ZLinkChannelEnvelopeCodecRegistry
): ZLinkRemoteActorPacketRelay | undefined {
  try {
    if (parts.length >= 2 && parts[0].data().length > 0) {
      const envelope = decodeChannelEnvelope(parts);
      if (envelope.packetName !== ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET) {
        return undefined;
      }
      const payload = decodeChannelPayload(envelope, codecs) as {
        readonly packetName?: unknown;
        readonly actorId?: unknown;
        readonly routerChannelId?: unknown;
        readonly boundSessionTargetNodeRid?: unknown;
        readonly boundSessionSpotRid?: unknown;
        readonly header?: unknown;
        readonly payload?: unknown;
        readonly actorNodeRid?: unknown;
        readonly actorNodeRidHex?: unknown;
        readonly actorGeneration?: unknown;
        readonly handoffTargetSpotRid?: unknown;
      };
      if (
        payload.packetName !== ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET ||
        typeof payload.actorId !== 'string' ||
        typeof payload.header !== 'string' ||
        typeof payload.payload !== 'string'
      ) {
        return undefined;
      }
      return {
        actorId: payload.actorId,
        routerChannelId: stringOrUndefined(payload.routerChannelId),
        boundSessionTargetNodeRid: stringOrUndefined(payload.boundSessionTargetNodeRid),
        boundSessionSpotRid: stringOrUndefined(payload.boundSessionSpotRid),
        header: payload.header,
        payload: payload.payload,
        actorRef: decodeForwardedActorRef({
          actorId: payload.actorId,
          actorNodeRid: payload.actorNodeRid,
          actorNodeRidHex: payload.actorNodeRidHex,
          actorGeneration: payload.actorGeneration
        }, payload.handoffTargetSpotRid),
        envelope
      };
    }
    if (parts.length !== 1) {
      return undefined;
    }
    const payload = JSON.parse(parts[0].data().toString()) as {
      readonly packetName?: unknown;
      readonly actorId?: unknown;
      readonly routerChannelId?: unknown;
      readonly boundSessionTargetNodeRid?: unknown;
      readonly boundSessionSpotRid?: unknown;
      readonly header?: unknown;
      readonly payload?: unknown;
      readonly actorNodeRid?: unknown;
      readonly actorNodeRidHex?: unknown;
      readonly actorGeneration?: unknown;
      readonly handoffTargetSpotRid?: unknown;
    };
    if (
      payload.packetName !== ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET ||
      typeof payload.actorId !== 'string' ||
      typeof payload.header !== 'string' ||
      typeof payload.payload !== 'string'
    ) {
      return undefined;
    }
    return {
      actorId: payload.actorId,
      routerChannelId: stringOrUndefined(payload.routerChannelId),
      boundSessionTargetNodeRid: stringOrUndefined(payload.boundSessionTargetNodeRid),
      boundSessionSpotRid: stringOrUndefined(payload.boundSessionSpotRid),
      header: payload.header,
      payload: payload.payload,
      actorRef: decodeForwardedActorRef({
        actorId: payload.actorId,
        actorNodeRid: payload.actorNodeRid,
        actorNodeRidHex: payload.actorNodeRidHex,
        actorGeneration: payload.actorGeneration
      }, payload.handoffTargetSpotRid)
    };
  } catch {
    return undefined;
  }
}

function decodeForwardedActorRef(payload: {
  readonly actorId?: unknown;
  readonly actorNodeRid?: unknown;
  readonly actorNodeRidHex?: unknown;
  readonly actorGeneration?: unknown;
}, targetSpotRid: unknown): ActorRef | undefined {
  const actorRef = decodeActorRef(payload);
  if (actorRef !== undefined) {
    (actorRef as ActorRef & { handoffForwarded?: boolean }).handoffForwarded = true;
    if (typeof targetSpotRid === 'string') {
      (actorRef as ActorRef & { handoffTargetSpotRid?: string }).handoffTargetSpotRid = targetSpotRid;
    }
  }
  return actorRef;
}

function decodeRemoteBoundSessionControl(
  parts: readonly BindingMessage[],
  packetName: string,
  codecs?: ZLinkChannelEnvelopeCodecRegistry
): {
  readonly actorId: string;
  readonly packetName: string;
  readonly requestSeq: bigint;
  readonly metadata: ReadonlyMap<string, string>;
  readonly payload: {
    readonly message?: unknown;
    readonly error?: unknown;
    readonly compressPayload?: unknown;
  };
  readonly actorPacketTarget?: unknown;
} | undefined {
  try {
    const payload = parts.length === 1
      ? JSON.parse(parts[0].data().toString()) as {
          readonly packetName?: unknown;
          readonly boundPacketName?: unknown;
          readonly actorId?: unknown;
          readonly requestSeq?: unknown;
          readonly message?: unknown;
          readonly error?: unknown;
          readonly compressPayload?: unknown;
          readonly metadata?: unknown;
          readonly actorPacketTarget?: unknown;
        }
      : decodeChannelPayload(decodeChannelEnvelope(parts), codecs) as {
          readonly packetName?: unknown;
          readonly boundPacketName?: unknown;
          readonly actorId?: unknown;
          readonly requestSeq?: unknown;
          readonly message?: unknown;
          readonly error?: unknown;
          readonly compressPayload?: unknown;
          readonly metadata?: unknown;
          readonly actorPacketTarget?: unknown;
        };
    if (
      payload.packetName !== packetName ||
      typeof payload.actorId !== 'string' ||
      typeof payload.boundPacketName !== 'string' ||
      typeof payload.requestSeq !== 'string'
    ) {
      return undefined;
    }
    return {
      actorId: payload.actorId,
      packetName: payload.boundPacketName,
      requestSeq: BigInt(payload.requestSeq),
      metadata: metadataOf(payload.metadata),
      payload,
      actorPacketTarget: payload.actorPacketTarget
    };
  } catch {
    return undefined;
  }
}

function metadataOf(value: unknown): ReadonlyMap<string, string> {
  return new Map(Object.entries(
    typeof value === 'object' && value !== null
      ? value as Record<string, string>
      : {}
  ));
}

function stringOrUndefined(value: unknown): string | undefined {
  return typeof value === 'string' ? value : undefined;
}
