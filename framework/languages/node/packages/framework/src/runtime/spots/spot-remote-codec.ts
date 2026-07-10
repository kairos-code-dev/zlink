import type { ZLinkBackendActorRef } from '../backend/contracts';
import type { ZLinkRemoteBoundSessionTarget } from '../actors';
import type { ZLinkActor } from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { Message as BindingMessage, Received as BindingReceived } from '@zlink-systems/zlink';
import { decodeChannelEnvelope } from '../channels/channel-envelope';
import { decodeWireRoutingId } from './route-wire-codec';

export const REMOTE_ACTOR_JOIN_PACKET = '__zlink.actor.join_spot.request';
export const REMOTE_BOUND_SESSION_BIND_PACKET = 'zlink.framework.actor.bound_session.bind';

export interface ZLinkRemoteActorJoinActor {
  readonly actor: ZLinkActor;
  readonly actorRef: ZLinkBackendActorRef;
}

export interface ZLinkDecodedRemoteActorJoinRequest {
  readonly envelope?: ReturnType<typeof decodeChannelEnvelope>;
  readonly raw: boolean;
  readonly actorId: string;
  readonly actorType: string;
  readonly actorRef?: ZLinkBackendActorRef;
  readonly remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget;
  readonly actorCreateRequest?: Message;
  readonly request: Message;
}

export interface ZLinkRemoteActorJoinWirePayload {
  readonly packetName?: unknown;
  readonly actorId?: unknown;
  readonly actorType?: unknown;
  readonly actorNodeRid?: unknown;
  readonly actorNodeRidHex?: unknown;
  readonly actorGeneration?: unknown;
  readonly actorCreateRequest?: unknown;
  readonly sourceSpotRid?: unknown;
  readonly sourceSpotRidHex?: unknown;
  readonly routerChannelId?: unknown;
  readonly boundSessionRouterChannelId?: unknown;
  readonly boundSessionTargetNodeRid?: unknown;
  readonly boundSessionTargetNodeRidHex?: unknown;
  readonly boundSessionSpotRid?: unknown;
  readonly boundSessionSpotRidHex?: unknown;
  readonly request?: unknown;
}

export function hasRemoteActorJoinIdentity(
  value: unknown
): value is ZLinkRemoteActorJoinWirePayload & {
  readonly actorId: string;
  readonly actorType: string;
} {
  return (
    typeof value === 'object' &&
    value !== null &&
    typeof (value as { actorId?: unknown }).actorId === 'string' &&
    typeof (value as { actorType?: unknown }).actorType === 'string'
  );
}

export function isRemoteActorJoinPayload(
  value: unknown
): value is ZLinkRemoteActorJoinWirePayload & {
  readonly actorId: string;
  readonly actorType: string;
  readonly request: string;
} {
  return (
    hasRemoteActorJoinIdentity(value) &&
    typeof value.request === 'string'
  );
}

export function decodeRemoteActorJoinPayload(
  payload: ZLinkRemoteActorJoinWirePayload & {
    readonly actorId: string;
    readonly actorType: string;
  },
  request: Message,
  received: BindingReceived,
  raw: boolean,
  envelope?: ReturnType<typeof decodeChannelEnvelope>
): ZLinkDecodedRemoteActorJoinRequest {
  return {
    envelope,
    raw,
    actorId: payload.actorId,
    actorType: payload.actorType,
    actorRef: decodeRemoteActorRef(
      payload.actorNodeRid,
      payload.actorNodeRidHex,
      payload.actorId,
      payload.actorGeneration
    ),
    actorCreateRequest: typeof payload.actorCreateRequest === 'string'
      ? BindingMessage.from(Buffer.from(payload.actorCreateRequest, 'base64'))
      : undefined,
    remoteBoundSessionTarget: decodeRemoteBoundSessionTarget(
      payload.boundSessionRouterChannelId ?? payload.routerChannelId,
      payload.boundSessionTargetNodeRid ?? (
        received.routingId === null
          ? payload.actorNodeRid
          : String(received.routingId)
      ),
      payload.boundSessionTargetNodeRidHex,
      payload.boundSessionSpotRid ?? payload.sourceSpotRid ?? received.spotRid ?? undefined,
      payload.boundSessionSpotRidHex ?? payload.sourceSpotRidHex
    ),
    request
  };
}

export function decodeRemoteActorRef(
  nodeRid: unknown,
  nodeRidHex: unknown,
  actorId: string,
  generation: unknown
): ZLinkBackendActorRef | undefined {
  if (typeof nodeRid !== 'string') {
    return undefined;
  }
  return {
    nodeRid: decodeWireRoutingId(nodeRid, nodeRidHex),
    actorId,
    generation: typeof generation === 'string' ? BigInt(generation) : 0n
  } as ZLinkBackendActorRef;
}

function decodeRemoteBoundSessionTarget(
  routerChannelId: unknown,
  targetNodeRid: unknown,
  targetNodeRidHex: unknown,
  spotRid: unknown,
  spotRidHex: unknown
): ZLinkRemoteBoundSessionTarget | undefined {
  if (
    typeof routerChannelId !== 'string' ||
    typeof targetNodeRid !== 'string' ||
    spotRid === undefined ||
    spotRid === null
  ) {
    return undefined;
  }
  return {
    routerChannelId,
    targetNodeRid: decodeWireRoutingId(targetNodeRid, targetNodeRidHex),
    spotRid: decodeWireRoutingId(String(spotRid), spotRidHex)
  };
}
