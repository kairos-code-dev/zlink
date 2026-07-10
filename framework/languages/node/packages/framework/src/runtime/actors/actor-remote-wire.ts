import type { RoutingId } from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { RoutingId as BindingRoutingId } from '@zlink-systems/zlink';
import type { ZLinkBackendActorRef } from '../backend/contracts';
import type { ZLinkRemoteBoundSessionTarget } from './actor-runtime-state';

export const ZLINK_REMOTE_ACTOR_JOIN_PACKET = '__zlink.actor.join_spot.request';
export const REMOTE_ACTOR_JOIN_PACKET = ZLINK_REMOTE_ACTOR_JOIN_PACKET;
export const REMOTE_ACTOR_JOIN_ADMISSION = 'admission';
export const REMOTE_ACTOR_JOIN_COMMIT = 'commit';
export type ZLinkRemoteActorJoinPhase =
  | typeof REMOTE_ACTOR_JOIN_ADMISSION
  | typeof REMOTE_ACTOR_JOIN_COMMIT;
export const ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET = '__zlink.actor.bound_session.send';
export const ZLINK_REMOTE_BOUND_SESSION_RESPONSE_PACKET = '__zlink.actor.bound_session.response';
export const ZLINK_REMOTE_BOUND_SESSION_ERROR_PACKET = '__zlink.actor.bound_session.error';
export const ZLINK_REMOTE_BOUND_SESSION_OWNERSHIP_PACKET = '__zlink.actor.bound_session.ownership';
export const ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET = 'zlink.framework.actor.session_disconnected';
export const ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET = '__zlink.actor.packet.relay';

export interface ZLinkRemoteActorJoinWirePayload {
  readonly packetName?: unknown;
  readonly spotRid?: unknown;
  readonly actorId?: unknown;
  readonly actorType?: unknown;
  readonly actorNodeRid?: unknown;
  readonly actorNodeRidHex?: unknown;
  readonly actorGeneration?: unknown;
  readonly actorCreateRequest?: unknown;
  readonly phase?: unknown;
  readonly transferId?: unknown;
  readonly transferAdapterKey?: unknown;
  readonly transferState?: unknown;
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

export interface ZLinkRemoteActorJoinRequest {
  readonly packetName: typeof REMOTE_ACTOR_JOIN_PACKET;
  readonly actorId: string;
  readonly actorType: string;
  readonly actorNodeRid: string;
  readonly actorNodeRidHex?: string;
  readonly actorGeneration: string;
  readonly actorCreateRequest?: string;
  readonly phase?: ZLinkRemoteActorJoinPhase;
  readonly transferId?: string;
  readonly transferAdapterKey?: string;
  readonly transferState?: string;
  readonly routerChannelId?: string;
  readonly sourceSpotRid?: string;
  readonly sourceSpotRidHex?: string;
  readonly boundSessionRouterChannelId?: string;
  readonly boundSessionTargetNodeRid?: string;
  readonly boundSessionTargetNodeRidHex?: string;
  readonly boundSessionSpotRid?: string;
  readonly boundSessionSpotRidHex?: string;
}

export interface ZLinkRemoteActorJoinRequestPayload {
  readonly packetName: typeof REMOTE_ACTOR_JOIN_PACKET;
  readonly spotRid?: string;
  readonly actorId?: string;
  readonly actorType: string;
  readonly actorNodeRid?: string;
  readonly actorNodeRidHex?: string;
  readonly actorGeneration?: string;
  readonly actorCreateRequest?: string;
  readonly phase?: ZLinkRemoteActorJoinPhase;
  readonly transferId?: string;
  readonly transferAdapterKey?: string;
  readonly transferState?: string;
  readonly sourceSpotRid?: string;
  readonly sourceSpotRidHex?: string;
  readonly routerChannelId?: string;
  readonly boundSessionRouterChannelId?: string;
  readonly boundSessionTargetNodeRid?: string;
  readonly boundSessionTargetNodeRidHex?: string;
  readonly boundSessionSpotRid?: string;
  readonly boundSessionSpotRidHex?: string;
  readonly request?: string;
}

interface ZLinkRemoteActorJoinRequestPayloadOptions {
  readonly actorId?: string;
  readonly actorType: string;
  readonly actorRef?: ZLinkBackendActorRef;
  readonly actorCreateRequest?: Buffer;
  readonly request?: Message;
  readonly targetSpotRid?: RoutingId;
  readonly routerChannelId?: string;
  readonly sourceSpotRid?: RoutingId;
  readonly boundSessionTarget?: ZLinkRemoteBoundSessionTarget;
  readonly phase?: ZLinkRemoteActorJoinPhase;
  readonly transferId?: string;
  readonly transferAdapterKey?: string;
  readonly transferState?: Buffer;
}

export interface ZLinkRemoteActorJoinReply {
  readonly accepted: boolean;
  readonly actorNodeRid: string;
  readonly actorNodeRidHex?: string;
  readonly actorId: string;
  readonly actorGeneration: string;
}

export function buildRemoteActorJoinRequestPayload(
  options: ZLinkRemoteActorJoinRequestPayloadOptions
): ZLinkRemoteActorJoinRequestPayload {
  const actorRef = options.actorRef;
  const sourceSpotRid = options.sourceSpotRid;
  const boundSessionTarget = options.boundSessionTarget;
  return {
    packetName: REMOTE_ACTOR_JOIN_PACKET,
    spotRid: options.targetSpotRid === undefined ? undefined : String(options.targetSpotRid),
    actorId: options.actorId,
    actorType: options.actorType,
    actorNodeRid: actorRef === undefined ? undefined : String(actorRef.nodeRid),
    actorNodeRidHex: actorRef === undefined ? undefined : encodeRoutingIdHex(actorRef.nodeRid),
    actorGeneration: actorRef === undefined ? undefined : actorRef.generation.toString(),
    actorCreateRequest: options.actorCreateRequest?.toString('base64'),
    phase: options.phase,
    transferId: options.transferId,
    transferAdapterKey: options.transferAdapterKey,
    transferState: options.transferState?.toString('base64'),
    sourceSpotRid: sourceSpotRid === undefined ? undefined : String(sourceSpotRid),
    sourceSpotRidHex: sourceSpotRid === undefined ? undefined : encodeRoutingIdHex(sourceSpotRid),
    routerChannelId: options.routerChannelId,
    boundSessionRouterChannelId: boundSessionTarget?.routerChannelId,
    boundSessionTargetNodeRid: boundSessionTarget === undefined ? undefined : String(boundSessionTarget.targetNodeRid),
    boundSessionTargetNodeRidHex: boundSessionTarget === undefined ? undefined : encodeRoutingIdHex(boundSessionTarget.targetNodeRid),
    boundSessionSpotRid: boundSessionTarget === undefined ? undefined : String(boundSessionTarget.spotRid),
    boundSessionSpotRidHex: boundSessionTarget === undefined ? undefined : encodeRoutingIdHex(boundSessionTarget.spotRid),
    request: options.request === undefined ? undefined : options.request.data().toString('base64')
  };
}

function encodeRoutingIdHex(routingId: RoutingId): string | undefined {
  const toHex = (routingId as unknown as { toHex?: () => string }).toHex;
  return typeof toHex === 'function' ? toHex.call(routingId) : undefined;
}

export function decodeWireRoutingId(text: string, hex: string | undefined): RoutingId {
  return hex === undefined
    ? BindingRoutingId.from(text) as unknown as RoutingId
    : BindingRoutingId.fromHex(hex) as unknown as RoutingId;
}
