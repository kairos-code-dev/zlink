import type { ActorRef, RoutingId, ZLinkSessionActor } from '../../contracts';
import { ZLinkSpotKind } from '../../contracts';
import { decodeRoutingId, routingIdWireHex } from '../routing-id';
import type { ZLinkRemoteActorPacketTarget } from './actor-runtime-state';

export const ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET = '__zlink.actor.packet.relay';
export const ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET = 'zlink.framework.actor.session_disconnected';
export const ZLINK_REMOTE_ACTOR_SESSION_BIND_PACKET = 'framework.internal.actor-session-bind';

export function encodeRemoteActorSessionBinding(input: {
  readonly sessionNodeRid: RoutingId;
  readonly sessionRid: RoutingId;
}): Uint8Array {
  return Buffer.from(JSON.stringify({
    sessionNodeRid: String(input.sessionNodeRid),
    sessionNodeRidHex: routingIdWireHex(input.sessionNodeRid),
    sessionRid: String(input.sessionRid),
    sessionRidHex: routingIdWireHex(input.sessionRid)
  }));
}

export function decodeRemoteActorSessionBinding(payload: Uint8Array): {
  readonly sessionNodeRid: RoutingId;
  readonly sessionRid: RoutingId;
} {
  const decoded = JSON.parse(Buffer.from(payload).toString('utf8')) as Record<string, unknown>;
  if (typeof decoded.sessionNodeRid !== 'string' || typeof decoded.sessionRid !== 'string') {
    throw new Error('Remote actor session binding payload is invalid.');
  }
  return {
    sessionNodeRid: decodeRoutingId(decoded.sessionNodeRid, decoded.sessionNodeRidHex),
    sessionRid: decodeRoutingId(decoded.sessionRid, decoded.sessionRidHex)
  };
}

export function encodeRemoteActorPacketRelayPayload(input: {
  readonly actorId: string;
  readonly routerChannelId?: string;
  readonly boundSessionTargetNodeRid?: string;
  readonly boundSessionSpotRid?: string;
  readonly header: Uint8Array;
  readonly payload: Uint8Array;
  readonly bindingActorRef?: ActorRef;
}): Record<string, unknown> {
  return {
    packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
    actorId: input.actorId,
    routerChannelId: input.routerChannelId,
    boundSessionTargetNodeRid: input.boundSessionTargetNodeRid,
    boundSessionSpotRid: input.boundSessionSpotRid,
    bindingActorNodeRid: input.bindingActorRef === undefined ? undefined : String(input.bindingActorRef.nodeRid),
    bindingActorNodeRidHex: input.bindingActorRef === undefined
      ? undefined
      : routingIdWireHex(input.bindingActorRef.nodeRid),
    bindingActorGeneration: input.bindingActorRef?.generation.toString(),
    header: Buffer.from(input.header).toString('base64'),
    payload: Buffer.from(input.payload).toString('base64')
  };
}

export function encodeForwardedRemoteActorPacketRelayPayload(input: {
  readonly actorId: string;
  readonly routerChannelId?: string;
  readonly boundSessionTargetNodeRid?: string;
  readonly boundSessionSpotRid?: string;
  readonly header: string;
  readonly payload: string;
  readonly actorNodeRid: string;
  readonly actorGeneration: string;
  readonly handoffTargetSpotRid: string;
}): Record<string, unknown> {
  return { packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET, ...input };
}

export interface ZLinkRemoteActorPacketTargetWire {
  readonly routerChannelId: string;
  readonly targetNodeRid: string;
  readonly targetNodeRidHex?: string;
  readonly spotRid: string;
  readonly spotRidHex?: string;
  readonly spotKind: ZLinkSpotKind;
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
    targetNodeRidHex: routingIdWireHex(target.targetNodeRid),
    spotRid: String(target.spotRid),
    spotRidHex: routingIdWireHex(target.spotRid),
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
    targetNodeRid: decodeRoutingId(
      (value as { targetNodeRid: string }).targetNodeRid,
      (value as { targetNodeRidHex?: unknown }).targetNodeRidHex
    ),
    spotRid: decodeRoutingId(
      (value as { spotRid: string }).spotRid,
      (value as { spotRidHex?: unknown }).spotRidHex
    ),
    spotKind: (value as { spotKind?: unknown }).spotKind === ZLinkSpotKind.Entry
      ? ZLinkSpotKind.Entry
      : ZLinkSpotKind.User
  };
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
  readonly actorNodeRid?: string;
  readonly actorNodeRidHex?: string;
  readonly actorGeneration?: string;
  readonly handoffTargetSpotRid?: string;
  readonly bindingActorNodeRid?: string;
  readonly bindingActorNodeRidHex?: string;
  readonly bindingActorGeneration?: string;
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
    routerChannelId: optionalString(payload, 'routerChannelId'),
    boundSessionTargetNodeRid: optionalString(payload, 'boundSessionTargetNodeRid'),
    boundSessionSpotRid: optionalString(payload, 'boundSessionSpotRid'),
    header: (payload as { header: string }).header,
    payload: (payload as { payload: string }).payload,
    actorNodeRid: optionalString(payload, 'actorNodeRid'),
    actorNodeRidHex: optionalString(payload, 'actorNodeRidHex'),
    actorGeneration: optionalString(payload, 'actorGeneration'),
    handoffTargetSpotRid: optionalString(payload, 'handoffTargetSpotRid'),
    bindingActorNodeRid: optionalString(payload, 'bindingActorNodeRid'),
    bindingActorNodeRidHex: optionalString(payload, 'bindingActorNodeRidHex'),
    bindingActorGeneration: optionalString(payload, 'bindingActorGeneration')
  };
}

function optionalString(value: object, key: string): string | undefined {
  const field = (value as Record<string, unknown>)[key];
  return typeof field === 'string' ? field : undefined;
}
