// SPDX-License-Identifier: MPL-2.0

import { normalizeOperationPayload } from '../../buffers/message_conversion';
import { normalizeRoutingId } from '../../core/routing_id';
import { submitNativeError } from '../../errors/native_errors';
import { messagesFromNativeBuffers } from '../../messaging/request_executor';
import { requireNative } from '../../native/native';
import { RoutingId, type MessageLike } from '../../../contracts';
import { RequestResult, SubmitResult } from '../../../contracts/errors/errors';
import { SendFlags } from '../../../contracts/sockets/socket_constants';
import type {
  ActorJoinEntrySpotHandler,
  ActorJoinHandler,
  ActorLookupHandler,
  ActorRef,
  ReplyHandler
} from '../../../contracts/service';
import {
  actorJoinEntrySpotResultFromRaw,
  actorJoinResultFromRaw,
  actorLookupResultFromRaw,
  actorRefToRaw,
  type ActorJoinEntrySpotResultRaw,
  type ActorJoinResultRaw,
  type ActorLookupResultRaw
} from './actor_models';

export function invokeActorJoin(
  nodeHandle: unknown,
  actor: ActorRef,
  destNodeRid: RoutingId,
  destSpotRid: RoutingId,
  spotHandle: unknown | null,
  parts: MessageLike | readonly MessageLike[],
  callback: ActorJoinHandler,
  flags: SendFlags,
  timeoutMs: number,
): boolean {
  void spotHandle;
  try {
    requireNative().spotNodeActorJoinSpot(
      nodeHandle,
      actorRefToRaw(actor),
      normalizeRoutingId(destNodeRid, 'destNodeRid'),
      normalizeRoutingId(destSpotRid, 'destSpotRid'),
      normalizeOperationPayload(parts),
      (rawResult: ActorJoinResultRaw | null, replyParts: Buffer[] | null) => {
        callback(actorJoinResultFromRaw(rawResult), messagesFromNativeBuffers(replyParts));
      },
      flags | 0,
      timeoutMs | 0,
    );
    return true;
  } catch (error) {
    const submitError = submitNativeError(error, flags, 'actor join failed');
    if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
      return false;
    }
    throw submitError;
  }
}

export function invokeActorJoinEntrySpot(
  nodeHandle: unknown,
  actor: ActorRef,
  destNodeRid: RoutingId,
  parts: MessageLike | readonly MessageLike[],
  callback: ActorJoinEntrySpotHandler,
  flags: SendFlags,
  timeoutMs: number,
): boolean {
  try {
    requireNative().spotNodeActorJoinEntrySpot(
      nodeHandle,
      actorRefToRaw(actor),
      normalizeRoutingId(destNodeRid, 'destNodeRid'),
      normalizeOperationPayload(parts),
      (rawResult: ActorJoinEntrySpotResultRaw | null, rawReplyParts: Buffer[] | null) => {
        callback(actorJoinEntrySpotResultFromRaw(rawResult), messagesFromNativeBuffers(rawReplyParts));
      },
      flags | 0,
      timeoutMs | 0,
    );
    return true;
  } catch (error) {
    throw submitNativeError(error, flags, 'actor entry spot join failed');
  }
}

export function invokeActorLeave(
  nodeHandle: unknown,
  actor: ActorRef,
  currentSpotRid: RoutingId,
  callback: ReplyHandler,
  timeoutMs: number,
): boolean {
  try {
    requireNative().spotNodeActorLeaveSpot(
      nodeHandle,
      actorRefToRaw(actor),
      normalizeRoutingId(currentSpotRid, 'currentSpotRid'),
      (result: number, replyParts: Buffer[] | null) => {
        callback(result as RequestResult, messagesFromNativeBuffers(replyParts));
      },
      timeoutMs | 0,
    );
    return true;
  } catch (error) {
    throw submitNativeError(error, SendFlags.None, 'actor leave failed');
  }
}

export function invokeActorDestroy(
  nodeHandle: unknown,
  actorRaw: ReturnType<typeof actorRefToRaw>,
  callback: ReplyHandler,
  timeoutMs: number,
): boolean {
  try {
    requireNative().spotNodeActorDestroy(
      nodeHandle,
      actorRaw,
      (result: number, replyParts: Buffer[] | null) => {
        callback(result as RequestResult, messagesFromNativeBuffers(replyParts));
      },
      timeoutMs | 0,
    );
    return true;
  } catch (error) {
    throw submitNativeError(error, SendFlags.None, 'actor destroy failed');
  }
}

export function invokeRemoteActorGetRef(
  nodeHandle: unknown,
  targetNodeRid: Buffer,
  actorId: string,
  callback: ActorLookupHandler,
  timeoutMs: number,
): boolean {
  try {
    requireNative().remoteActorGetRef(
      nodeHandle,
      targetNodeRid,
      actorId,
      (raw: ActorLookupResultRaw) => callback(actorLookupResultFromRaw(raw)),
      timeoutMs | 0,
    );
    return true;
  } catch (error) {
    throw submitNativeError(error, SendFlags.None, 'remote actor lookup failed');
  }
}

export function invokeActorSendBoundSession(
  nodeHandle: unknown,
  actor: ActorRef,
  parts: MessageLike | readonly MessageLike[],
  flags: SendFlags,
): boolean {
  try {
    requireNative().spotNodeActorSendBoundSessionMsg(
      nodeHandle,
      actorRefToRaw(actor),
      normalizeOperationPayload(parts),
      flags | 0,
    );
  } catch (error) {
    const submitError = submitNativeError(error, flags, 'actor bound session send failed');
    if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
      return false;
    }
    throw submitError;
  }
  return true;
}

export function invokeStreamBindActor(
  streamHandle: unknown,
  sessionRid: Buffer,
  actorRaw: ReturnType<typeof actorRefToRaw>,
  callback: ReplyHandler,
  timeoutMs: number,
): boolean {
  try {
    requireNative().streamBindActor(
      streamHandle,
      sessionRid,
      actorRaw,
      (result: number, replyParts: Buffer[] | null) => {
        callback(result as RequestResult, messagesFromNativeBuffers(replyParts));
      },
      timeoutMs | 0,
    );
    return true;
  } catch (error) {
    throw submitNativeError(error, SendFlags.None, 'stream actor bind failed');
  }
}

export function invokeStreamUnbindActor(
  streamHandle: unknown,
  sessionRid: Buffer,
  actorId: string,
  callback: ReplyHandler,
  timeoutMs: number,
): boolean {
  try {
    requireNative().streamUnbindActor(
      streamHandle,
      sessionRid,
      actorId,
      (result: number, replyParts: Buffer[] | null) => {
        callback(result as RequestResult, messagesFromNativeBuffers(replyParts));
      },
      timeoutMs | 0,
    );
    return true;
  } catch (error) {
    throw submitNativeError(error, SendFlags.None, 'stream actor unbind failed');
  }
}

export function invokeStreamSendBoundActor(
  streamHandle: unknown,
  sessionRid: Buffer,
  actorId: string,
  parts: MessageLike | readonly MessageLike[],
  flags: SendFlags,
): boolean {
  try {
    requireNative().streamSendBoundActorPart(
      streamHandle,
      sessionRid,
      actorId,
      normalizeOperationPayload(parts),
      flags | 0,
    );
  } catch (error) {
    const submitError = submitNativeError(error, flags, 'sendBoundActor failed');
    if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
      return false;
    }
    throw submitError;
  }
  return true;
}
