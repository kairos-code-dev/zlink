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
  ActorRecvInfo,
  ActorRef,
  ReplyHandler
} from '../../../contracts/service';
import {
  actorRecvInfoToRaw,
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

export function invokeSendToActor(
  nodeHandle: unknown,
  actor: ActorRef,
  parts: MessageLike | readonly MessageLike[],
  flags: SendFlags,
): boolean {
  try {
    requireNative().spotNodeSendToActor(
      nodeHandle,
      actorRefToRaw(actor),
      normalizeOperationPayload(parts),
      flags | 0,
      0,
    );
  } catch (error) {
    const submitError = submitNativeError(error, flags, 'sendToActor failed');
    if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
      return false;
    }
    throw submitError;
  }
  return true;
}

export function invokeSendToActorCallback(
  nodeHandle: unknown,
  actor: ActorRef,
  parts: MessageLike | readonly MessageLike[],
  callback: ReplyHandler,
  flags: SendFlags,
  timeoutMs: number,
): boolean {
  try {
    requireNative().spotNodeSendToActor(
      nodeHandle,
      actorRefToRaw(actor),
      normalizeOperationPayload(parts),
      flags | 0,
      timeoutMs | 0,
      (result: number, replyParts: Buffer[] | null) => {
        callback(result as RequestResult, messagesFromNativeBuffers(replyParts));
      },
    );
    return true;
  } catch (error) {
    const submitError = submitNativeError(error, flags, 'sendToActor failed');
    if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
      return false;
    }
    throw submitError;
  }
}

export function invokeRequestToActor(
  nodeHandle: unknown,
  actor: ActorRef,
  parts: MessageLike | readonly MessageLike[],
  callback: ReplyHandler,
  flags: SendFlags,
  timeoutMs: number,
): boolean {
  try {
    requireNative().spotNodeRequestToActor(
      nodeHandle,
      actorRefToRaw(actor),
      normalizeOperationPayload(parts),
      (result: number, replyParts: Buffer[] | null) => {
        callback(result as RequestResult, messagesFromNativeBuffers(replyParts));
      },
      flags | 0,
      timeoutMs | 0,
    );
    return true;
  } catch (error) {
    throw submitNativeError(error, flags, 'requestToActor failed');
  }
}

export function invokeActorReplyNoBind(
  nodeHandle: unknown,
  info: ActorRecvInfo,
  parts: MessageLike | readonly MessageLike[],
  result: RequestResult,
): void {
  try {
    requireNative().spotNodeActorReplyNoBind(
      nodeHandle,
      actorRecvInfoToRaw(info),
      normalizeOperationPayload(parts),
      result | 0,
    );
  } catch (error) {
    throw submitNativeError(error, SendFlags.None, 'actor no-bind reply failed');
  }
}

export function invokeActorBindRemoteSession(
  nodeHandle: unknown,
  actor: ActorRef,
  sourceNodeRid: RoutingId,
  sourceSessionRid: RoutingId,
): void {
  try {
    requireNative().spotNodeActorBindRemoteSession(
      nodeHandle,
      actorRefToRaw(actor),
      normalizeRoutingId(sourceNodeRid, 'sourceNodeRid'),
      normalizeRoutingId(sourceSessionRid, 'sourceSessionRid'),
    );
  } catch (error) {
    throw submitNativeError(error, SendFlags.None, 'actor remote session bind failed');
  }
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
