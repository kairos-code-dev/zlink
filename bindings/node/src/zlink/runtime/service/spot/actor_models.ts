// SPDX-License-Identifier: MPL-2.0

import { messageFromNativeBuffer } from '../../buffers/message_conversion';
import { normalizeRoutingId } from '../../core/routing_id';
import { validateCString } from '../../options/validation';
import { RoutingId } from '../../../contracts';
import { RequestResult } from '../../../contracts/errors/errors';
import type {
  ActorJoinEntrySpotResult,
  ActorJoinInfo,
  ActorJoinResult,
  ActorLookupResult,
  ActorPart,
  ActorRecvInfo,
  ActorRef,
  ActorRoute,
  SpotActorLifecycleInfo,
  SpotKindValue,
  SpotNodeActorEntry,
  SpotNodeSpotEntry
} from '../../../contracts/service';
import { wrapRoutingId } from '../../core/routing_id_conversion';

export interface ActorRefRaw {
  nodeRid: Buffer;
  actorId: string;
  generation: bigint | number;
}

export interface ActorRecvInfoRaw {
  actor: ActorRefRaw;
  sourceNodeRid: Buffer;
  sourceSessionRid: Buffer;
  flags: number;
}

export interface ActorPartRaw {
  info: ActorRecvInfoRaw;
  message: Buffer;
  more: boolean;
}

export interface ActorJoinInfoRaw {
  actor?: ActorRefRaw;
  sourceActor?: ActorRefRaw;
  targetActor?: ActorRefRaw;
  sourceNodeRid: Buffer;
  sourceSpotRid?: Buffer | null;
  targetNodeRid?: Buffer | null;
  targetSpotRid?: Buffer | null;
  joinEpoch?: bigint | number;
  flags: number;
  requestHandle: bigint;
}

export interface ActorRouteRaw {
  actor: ActorRefRaw;
  currentSpotRid: Buffer;
  currentSpotKind: number;
}

export interface ActorJoinResultRaw {
  result: number;
  joinResultCode?: number;
  actor: ActorRefRaw;
  joinedSpotRid?: Buffer | null;
  joinEpoch?: bigint | number;
  flags: number;
}

export interface ActorJoinEntrySpotResultRaw {
  result: number;
  joinResultCode?: number;
  actor: ActorRefRaw;
  targetNodeRid?: Buffer | null;
  joinedSpotRid?: Buffer | null;
  joinEpoch?: bigint | number;
  flags: number;
}

export interface ActorLookupResultRaw {
  result: number;
  actor: ActorRefRaw;
  flags: number;
}

export interface SpotActorLifecycleInfoRaw {
  previousActor: ActorRefRaw;
  currentActor: ActorRefRaw;
  previousSpotRid?: Buffer | null;
  currentSpotRid?: Buffer | null;
  joinEpoch?: bigint | number;
  flags: number;
}

export interface SpotNodeSpotEntryRaw {
  spotRid: Buffer;
  spotKind: number;
  dispatchHandlerAttached: boolean;
  joinedActorCount: number;
  pendingActorJoinCount: number;
  routeSynced: boolean;
  lastChangedMs: bigint | number;
}

export interface SpotNodeActorEntryRaw {
  actor: ActorRefRaw;
  currentSpotRid: Buffer;
  currentSpotKind: number;
  routeSynced: boolean;
  pendingMessageCount: number;
  lastChangedMs: bigint | number;
}

export const actorJoinRequestHandles = new WeakMap<ActorJoinInfo, bigint>();

export function actorRefFromRaw(raw: ActorRefRaw): ActorRef {
  const generation = BigInt(raw.generation);
  return Object.freeze({
    nodeRid: RoutingId.from(raw.nodeRid),
    actorId: raw.actorId,
    generation
  });
}

export function actorRefToRaw(actor: ActorRef): { nodeRid: Buffer; actorId: string; generation: bigint } {
  return {
    nodeRid: normalizeRoutingId(actor.nodeRid, 'actor.nodeRid'),
    actorId: validateCString(actor.actorId, 'actor.actorId', 255),
    generation: BigInt(actor.generation)
  };
}

export function actorRecvInfoFromRaw(raw: ActorRecvInfoRaw): ActorRecvInfo {
  return {
    actor: actorRefFromRaw(raw.actor),
    sourceNodeRid: RoutingId.from(raw.sourceNodeRid),
    sourceSessionRid: RoutingId.from(raw.sourceSessionRid),
    flags: raw.flags
  };
}

export function actorPartFromRaw(raw: ActorPartRaw): ActorPart {
  return {
    info: actorRecvInfoFromRaw(raw.info),
    message: messageFromNativeBuffer(raw.message),
    more: Boolean(raw.more)
  };
}

function actorJoinRefFromRaw(
  raw: Pick<ActorJoinInfoRaw, 'actor' | 'sourceActor' | 'targetActor'>,
  field: 'sourceActor' | 'targetActor'
): ActorRef {
  const actor = raw[field] ?? raw.actor;
  if (!actor) {
    throw new TypeError(`actor join info missing ${field}`);
  }
  return actorRefFromRaw(actor);
}

export function actorJoinInfoFromRaw(raw: ActorJoinInfoRaw): ActorJoinInfo {
  const sourceActor = actorJoinRefFromRaw(raw, 'sourceActor');
  const targetActor = actorJoinRefFromRaw(raw, 'targetActor');
  const info = {
    sourceActor,
    targetActor,
    sourceNodeRid: RoutingId.from(raw.sourceNodeRid),
    sourceSpotRid: wrapRoutingId(raw.sourceSpotRid ?? null) as RoutingId,
    targetNodeRid: wrapRoutingId(raw.targetNodeRid ?? null) as RoutingId,
    targetSpotRid: wrapRoutingId(raw.targetSpotRid ?? null) as RoutingId,
    joinEpoch: BigInt(raw.joinEpoch ?? 0),
    flags: raw.flags
  };
  actorJoinRequestHandles.set(info, BigInt(raw.requestHandle));
  return info;
}

export function actorJoinInfoToRaw(info: ActorJoinInfo): Record<string, unknown> {
  const requestHandle = actorJoinRequestHandles.get(info);
  if (typeof requestHandle !== 'bigint') {
    throw new TypeError('join info must come from recvActorJoin()');
  }
  return {
    actor: actorRefToRaw(info.targetActor),
    sourceActor: actorRefToRaw(info.sourceActor),
    targetActor: actorRefToRaw(info.targetActor),
    sourceNodeRid: normalizeRoutingId(info.sourceNodeRid, 'info.sourceNodeRid'),
    sourceSpotRid: normalizeRoutingId(info.sourceSpotRid, 'info.sourceSpotRid'),
    targetNodeRid: normalizeRoutingId(info.targetNodeRid, 'info.targetNodeRid'),
    targetSpotRid: normalizeRoutingId(info.targetSpotRid, 'info.targetSpotRid'),
    joinEpoch: BigInt(info.joinEpoch ?? 0),
    flags: info.flags | 0,
    requestHandle
  };
}

export function actorRouteFromRaw(raw: ActorRouteRaw): ActorRoute {
  return {
    actor: actorRefFromRaw(raw.actor),
    currentSpotRid: RoutingId.from(raw.currentSpotRid),
    currentSpotKind: raw.currentSpotKind as SpotKindValue
  };
}

function emptyRoutingId(): RoutingId {
  return RoutingId.from(Buffer.alloc(1));
}

function failedActorRef(): ActorRef {
  return {
    nodeRid: emptyRoutingId(),
    actorId: '',
    generation: 0n,
  };
}

function failedActorJoinResult(): ActorJoinResult {
  return {
    result: RequestResult.InternalError,
    joinResultCode: 0,
    actor: failedActorRef(),
    joinedSpotRid: emptyRoutingId(),
    joinEpoch: 0n,
    flags: 0,
  };
}

function failedActorJoinEntrySpotResult(): ActorJoinEntrySpotResult {
  return {
    result: RequestResult.InternalError,
    joinResultCode: 0,
    actor: failedActorRef(),
    targetNodeRid: emptyRoutingId(),
    joinedSpotRid: emptyRoutingId(),
    joinEpoch: 0n,
    flags: 0,
  };
}

export function actorJoinResultFromRaw(raw: ActorJoinResultRaw | null): ActorJoinResult {
  if (!raw) {
    return failedActorJoinResult();
  }
  return {
    result: raw.result as RequestResult,
    joinResultCode: raw.joinResultCode ?? 0,
    actor: actorRefFromRaw(raw.actor),
    joinedSpotRid: (wrapRoutingId(raw.joinedSpotRid ?? null) as RoutingId) ?? emptyRoutingId(),
    joinEpoch: BigInt(raw.joinEpoch ?? 0),
    flags: raw.flags | 0,
  };
}

export function actorJoinEntrySpotResultFromRaw(raw: ActorJoinEntrySpotResultRaw | null): ActorJoinEntrySpotResult {
  if (!raw) {
    return failedActorJoinEntrySpotResult();
  }
  return {
    result: raw.result as RequestResult,
    joinResultCode: raw.joinResultCode ?? 0,
    actor: actorRefFromRaw(raw.actor),
    targetNodeRid: (wrapRoutingId(raw.targetNodeRid ?? null) as RoutingId) ?? emptyRoutingId(),
    joinedSpotRid: (wrapRoutingId(raw.joinedSpotRid ?? null) as RoutingId) ?? emptyRoutingId(),
    joinEpoch: BigInt(raw.joinEpoch ?? 0),
    flags: raw.flags | 0,
  };
}

export function actorLookupResultFromRaw(raw: ActorLookupResultRaw): ActorLookupResult {
  return {
    result: raw.result as RequestResult,
    actor: actorRefFromRaw(raw.actor),
    flags: raw.flags | 0,
  };
}

export function spotActorLifecycleInfoFromRaw(raw: SpotActorLifecycleInfoRaw): SpotActorLifecycleInfo {
  return {
    previousActor: actorRefFromRaw(raw.previousActor),
    currentActor: actorRefFromRaw(raw.currentActor),
    previousSpotRid: wrapRoutingId(raw.previousSpotRid ?? null),
    currentSpotRid: wrapRoutingId(raw.currentSpotRid ?? null),
    joinEpoch: BigInt(raw.joinEpoch ?? 0),
    flags: raw.flags | 0,
  };
}

export function spotNodeSpotEntryFromRaw(raw: SpotNodeSpotEntryRaw): SpotNodeSpotEntry {
  return {
    spotRid: RoutingId.from(raw.spotRid),
    spotKind: raw.spotKind as SpotKindValue,
    dispatchHandlerAttached: Boolean(raw.dispatchHandlerAttached),
    joinedActorCount: raw.joinedActorCount,
    pendingActorJoinCount: raw.pendingActorJoinCount,
    routeSynced: Boolean(raw.routeSynced),
    lastChangedMs: BigInt(raw.lastChangedMs)
  };
}

export function spotNodeActorEntryFromRaw(raw: SpotNodeActorEntryRaw): SpotNodeActorEntry {
  return {
    actor: actorRefFromRaw(raw.actor),
    currentSpotRid: RoutingId.from(raw.currentSpotRid),
    currentSpotKind: raw.currentSpotKind as SpotKindValue,
    routeSynced: Boolean(raw.routeSynced),
    pendingMessageCount: raw.pendingMessageCount,
    lastChangedMs: BigInt(raw.lastChangedMs)
  };
}
