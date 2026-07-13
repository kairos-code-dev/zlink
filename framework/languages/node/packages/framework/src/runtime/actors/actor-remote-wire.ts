import type { RoutingId } from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type { ZLinkBackendActorRef } from '../backend/contracts';
import type { ZLinkRemoteBoundSessionTarget } from './actor-runtime-state';
import type { ZLinkActorHandoffPacket, ZLinkActorHandoffResult } from './actor-handoff';
import { decodeRoutingId, routingIdWireHex } from '../routing-id';

export const ZLINK_REMOTE_ACTOR_JOIN_PACKET = '__zlink.actor.join_spot.request';
export const REMOTE_ACTOR_JOIN_PACKET = ZLINK_REMOTE_ACTOR_JOIN_PACKET;
export const REMOTE_ACTOR_JOIN_ADMISSION = 'admission';
export const REMOTE_ACTOR_JOIN_COMMIT = 'commit';
export type ZLinkRemoteActorJoinPhase =
  | typeof REMOTE_ACTOR_JOIN_ADMISSION
  | typeof REMOTE_ACTOR_JOIN_COMMIT;
export interface ZLinkRemoteActorJoinWirePayload {
  readonly packetName?: unknown;
  readonly spotRid?: unknown;
  readonly spotRidHex?: unknown;
  readonly actorId?: unknown;
  readonly actorType?: unknown;
  readonly actorNodeRid?: unknown;
  readonly actorNodeRidHex?: unknown;
  readonly actorGeneration?: unknown;
  readonly actorEntryNodeRid?: unknown;
  readonly actorEntryNodeRidHex?: unknown;
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
  readonly handoffBacklog?: unknown;
}

export interface ZLinkRemoteActorJoinRequest {
  readonly packetName: typeof REMOTE_ACTOR_JOIN_PACKET;
  readonly actorId: string;
  readonly actorType: string;
  readonly actorNodeRid: string;
  readonly actorNodeRidHex?: string;
  readonly actorGeneration: string;
  readonly actorEntryNodeRid?: string;
  readonly actorEntryNodeRidHex?: string;
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
  readonly handoffBacklog?: readonly ZLinkActorHandoffPacket[];
}

export interface ZLinkRemoteActorJoinRequestPayload {
  readonly packetName: typeof REMOTE_ACTOR_JOIN_PACKET;
  readonly spotRid?: string;
  readonly spotRidHex?: string;
  readonly actorId?: string;
  readonly actorType: string;
  readonly actorNodeRid?: string;
  readonly actorNodeRidHex?: string;
  readonly actorGeneration?: string;
  readonly actorEntryNodeRid?: string;
  readonly actorEntryNodeRidHex?: string;
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
  readonly handoffBacklog?: readonly ZLinkActorHandoffPacket[];
}

interface ZLinkRemoteActorJoinRequestPayloadOptions {
  readonly actorId?: string;
  readonly actorType: string;
  readonly actorRef?: ZLinkBackendActorRef;
  readonly actorEntryNodeRid?: RoutingId;
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
  readonly handoffBacklog?: readonly ZLinkActorHandoffPacket[];
}

export interface ZLinkRemoteActorJoinReply {
  readonly accepted: boolean;
  readonly actorNodeRid: string;
  readonly actorNodeRidHex?: string;
  readonly actorId: string;
  readonly actorGeneration: string;
  readonly handoffResults?: readonly ZLinkActorHandoffResult[];
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
    spotRidHex: options.targetSpotRid === undefined ? undefined : encodeRoutingIdHex(options.targetSpotRid),
    actorId: options.actorId,
    actorType: options.actorType,
    actorNodeRid: actorRef === undefined ? undefined : String(actorRef.nodeRid),
    actorNodeRidHex: actorRef === undefined ? undefined : encodeRoutingIdHex(actorRef.nodeRid),
    actorGeneration: actorRef === undefined ? undefined : actorRef.generation.toString(),
    actorEntryNodeRid: options.actorEntryNodeRid === undefined ? undefined : String(options.actorEntryNodeRid),
    actorEntryNodeRidHex: options.actorEntryNodeRid === undefined ? undefined : encodeRoutingIdHex(options.actorEntryNodeRid),
    actorCreateRequest: options.actorCreateRequest?.toString('base64'),
    phase: options.phase,
    transferId: options.transferId,
    transferAdapterKey: options.transferAdapterKey,
    transferState: options.transferState?.toString('base64'),
    handoffBacklog: options.handoffBacklog,
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

export function decodeRemoteActorJoinPayload(payload: unknown): {
  readonly spotRid: RoutingId;
  readonly actorId: string;
  readonly actorType: string;
  readonly actorNodeRid: RoutingId;
  readonly actorGeneration: string;
  readonly sourceSpotRid?: RoutingId;
  readonly routerChannelId?: string;
  readonly boundSessionRouterChannelId?: string;
  readonly boundSessionTargetNodeRid?: RoutingId;
  readonly boundSessionSpotRid?: RoutingId;
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
    spotRid: decodeWireRoutingId(
      (payload as { spotRid: string }).spotRid,
      optionalString(payload, 'spotRidHex')
    ),
    actorId: (payload as { actorId: string }).actorId,
    actorType: (payload as { actorType: string }).actorType,
    actorNodeRid: decodeWireRoutingId(
      (payload as { actorNodeRid: string }).actorNodeRid,
      optionalString(payload, 'actorNodeRidHex')
    ),
    actorGeneration: (payload as { actorGeneration: string }).actorGeneration,
    sourceSpotRid: optionalRoutingId(payload, 'sourceSpotRid', 'sourceSpotRidHex'),
    routerChannelId: optionalString(payload, 'routerChannelId'),
    boundSessionRouterChannelId: optionalString(payload, 'boundSessionRouterChannelId'),
    boundSessionTargetNodeRid: optionalRoutingId(
      payload,
      'boundSessionTargetNodeRid',
      'boundSessionTargetNodeRidHex'
    ),
    boundSessionSpotRid: optionalRoutingId(payload, 'boundSessionSpotRid', 'boundSessionSpotRidHex'),
    request: (payload as { request: string }).request
  };
}

function encodeRoutingIdHex(routingId: RoutingId): string | undefined {
  return routingIdWireHex(routingId);
}

export function decodeWireRoutingId(text: string, hex: string | undefined): RoutingId {
  return decodeRoutingId(text, hex);
}

function optionalString(value: object, key: string): string | undefined {
  const field = (value as Record<string, unknown>)[key];
  return typeof field === 'string' ? field : undefined;
}

function optionalRoutingId(value: object, textKey: string, hexKey: string): RoutingId | undefined {
  const text = optionalString(value, textKey);
  return text === undefined ? undefined : decodeWireRoutingId(text, optionalString(value, hexKey));
}
