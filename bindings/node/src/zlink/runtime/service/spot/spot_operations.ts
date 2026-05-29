// SPDX-License-Identifier: MPL-2.0

import { requireNative } from '../../native/native';
import { submitNativeError } from '../../errors/native_errors';
import { validateCString } from '../../options/validation';
import { messageFromNativeBuffer, toMessageParts } from '../../buffers/message_conversion';
import { normalizeRoutingId } from '../../core/routing_id';
import { messagesFromNativeBuffers, requestErrorFromResult } from '../../messaging/request_executor';
import { Message, RoutingId, type MessageLike } from '../../../contracts';
import { RequestResult, SubmitResult } from '../../../contracts/errors/errors';
import { SendFlags } from '../../../contracts/sockets/socket_constants';
import { type ActorBindOperation, type ActorDestroyOperation, type ActorJoinCallbackSubmitOperation, type ActorJoinEntrySpotHandler, type ActorJoinEntrySpotOperation, type ActorJoinEntrySpotResult, type ActorJoinHandler, type ActorJoinInfo, type ActorJoinOperation, type ActorJoinReplyOperation, type ActorJoinResult, type ActorJoinSubmitOperation, type ActorLeaveOperation, type ActorLookupHandler, type ActorLookupOperation, type ActorLookupResult, type ActorPart, type ActorRecvInfo, type ActorRef, type ActorRoute, type ActorUnbindOperation, type ReplyHandler, type SpotActorLifecycleInfo, type SpotKindValue, type SpotNodeActorEntry, type SpotNodeSpotEntry } from '../../../contracts/service';
import { wrapRoutingId } from '../../../contracts/service/spot/spot_models';
import { OperationPayload } from '../../sockets/socket_operations';


export function actorRefFromRaw(raw: { nodeRid: Buffer; actorId: string; generation: bigint | number }): ActorRef {
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

export function actorRecvInfoFromRaw(raw: {
  actor: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  sourceNodeRid: Buffer;
  sourceSessionRid: Buffer;
  flags: number;
}): ActorRecvInfo {
  return {
    actor: actorRefFromRaw(raw.actor),
    sourceNodeRid: RoutingId.from(raw.sourceNodeRid),
    sourceSessionRid: RoutingId.from(raw.sourceSessionRid),
    flags: raw.flags
  };
}

export function actorPartFromRaw(raw: {
  info: {
    actor: { nodeRid: Buffer; actorId: string; generation: bigint | number };
    sourceNodeRid: Buffer;
    sourceSessionRid: Buffer;
    flags: number;
  };
  message: Buffer;
  more: boolean;
}): ActorPart {
  return {
    info: actorRecvInfoFromRaw(raw.info),
    message: messageFromNativeBuffer(raw.message),
    more: Boolean(raw.more)
  };
}

export const actorJoinRequestHandles = new WeakMap<ActorJoinInfo, bigint>();

export function actorJoinInfoFromRaw(raw: {
  actor?: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  sourceActor?: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  targetActor?: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  sourceNodeRid: Buffer;
  sourceSpotRid?: Buffer | null;
  targetNodeRid?: Buffer | null;
  targetSpotRid?: Buffer | null;
  joinEpoch?: bigint | number;
  flags: number;
  requestHandle: bigint;
}): ActorJoinInfo {
  const sourceActor = actorRefFromRaw(raw.sourceActor ?? raw.actor!);
  const targetActor = actorRefFromRaw(raw.targetActor ?? raw.actor!);
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

export function actorRouteFromRaw(raw: {
  actor: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  currentSpotRid: Buffer;
  currentSpotKind: number;
}): ActorRoute {
  return {
    actor: actorRefFromRaw(raw.actor),
    currentSpotRid: RoutingId.from(raw.currentSpotRid),
    currentSpotKind: raw.currentSpotKind as SpotKindValue
  };
}

export interface ActorJoinResultRaw {
  result: number;
  joinResultCode?: number;
  actor: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  joinedSpotRid?: Buffer | null;
  joinEpoch?: bigint | number;
  flags: number;
}

export interface ActorJoinEntrySpotResultRaw {
  result: number;
  actor: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  targetNodeRid?: Buffer | null;
  joinEpoch?: bigint | number;
  flags: number;
}

export interface ActorLookupResultRaw {
  result: number;
  actor: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  flags: number;
}

export interface SpotActorLifecycleInfoRaw {
  previousActor: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  currentActor: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  previousSpotRid?: Buffer | null;
  currentSpotRid?: Buffer | null;
  joinEpoch?: bigint | number;
  flags: number;
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
    actor: failedActorRef(),
    targetNodeRid: emptyRoutingId(),
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
    actor: actorRefFromRaw(raw.actor),
    targetNodeRid: (wrapRoutingId(raw.targetNodeRid ?? null) as RoutingId) ?? emptyRoutingId(),
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

export function invokeActorJoin(
  nodeHandle: unknown,
  actor: ActorRef,
  destNodeRid: RoutingId,
  destSpotRid: RoutingId,
  spotHandle: unknown | null,
  parts: readonly MessageLike[],
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
      toMessageParts(parts),
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
  callback: ActorJoinEntrySpotHandler,
  timeoutMs: number,
): boolean {
  try {
    requireNative().spotNodeActorJoinEntrySpot(
      nodeHandle,
      actorRefToRaw(actor),
      normalizeRoutingId(destNodeRid, 'destNodeRid'),
      (rawResult: ActorJoinEntrySpotResultRaw | null) => {
        callback(actorJoinEntrySpotResultFromRaw(rawResult));
      },
      timeoutMs | 0,
    );
    return true;
  } catch (error) {
    throw submitNativeError(error, SendFlags.None, 'actor entry spot join failed');
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
  parts: readonly MessageLike[],
  flags: SendFlags,
): boolean {
  try {
    requireNative().spotNodeActorSendBoundSessionMsg(
      nodeHandle,
      actorRefToRaw(actor),
      toMessageParts(parts),
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
  parts: readonly MessageLike[],
  flags: SendFlags,
): boolean {
  try {
    requireNative().streamSendBoundActorPart(
      streamHandle,
      sessionRid,
      actorId,
      toMessageParts(parts),
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

export function spotNodeSpotEntryFromRaw(raw: {
  spotRid: Buffer;
  spotKind: number;
  dispatchHandlerAttached: boolean;
  joinedActorCount: number;
  pendingActorJoinCount: number;
  routeSynced: boolean;
  lastChangedMs: bigint | number;
}): SpotNodeSpotEntry {
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

export function spotNodeActorEntryFromRaw(raw: {
  actor: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  currentSpotRid: Buffer;
  currentSpotKind: number;
  routeSynced: boolean;
  pendingMessageCount: number;
  lastChangedMs: bigint | number;
}): SpotNodeActorEntry {
  return {
    actor: actorRefFromRaw(raw.actor),
    currentSpotRid: RoutingId.from(raw.currentSpotRid),
    currentSpotKind: raw.currentSpotKind as SpotKindValue,
    routeSynced: Boolean(raw.routeSynced),
    pendingMessageCount: raw.pendingMessageCount,
    lastChangedMs: BigInt(raw.lastChangedMs)
  };
}

type ActorJoinInvoker = (
  parts: readonly MessageLike[],
  callback: ActorJoinHandler,
  flags: SendFlags,
  timeoutMs: number,
) => boolean;

export class RuntimeActorJoinOperation implements ActorJoinOperation, ActorJoinSubmitOperation, ActorJoinCallbackSubmitOperation {
  private readonly _invoke: ActorJoinInvoker;
  private readonly _payload = new OperationPayload();
  private _flags: SendFlags = SendFlags.None;
  private _timeoutMs = 0;
  private _callbackMode = false;

  constructor(invoke: ActorJoinInvoker) {
    this._invoke = invoke;
  }

  message(message: MessageLike): this {
    this._payload.append(message);
    return this;
  }

  timeout(timeoutMs: number): this {
    this._payload.ensureOpen();
    this._timeoutMs = timeoutMs | 0;
    return this;
  }

  flags(flags: SendFlags): ActorJoinCallbackSubmitOperation {
    this._payload.ensureOpen();
    this._flags = flags;
    this._callbackMode = true;
    return this;
  }

  submitAsync(): Promise<{ result: ActorJoinResult; parts: Message[] }> {
    const parts = this._payload.consume();
    return new Promise((resolve, reject) => {
      this._invoke(parts, (result, replyParts) => {
        if (result.result !== RequestResult.Ok) {
          reject(requestErrorFromResult(result.result, 'actor join failed'));
          return;
        }
        resolve({ result, parts: replyParts });
      }, SendFlags.None, this._timeoutMs);
    });
  }

  submit(callback: ActorJoinHandler): boolean {
    const flags = this._callbackMode ? this._flags : SendFlags.None;
    return this._invoke(this._payload.consume(), callback, flags, this._timeoutMs);
  }
}

type ActorJoinEntrySpotInvoker = (
  callback: ActorJoinEntrySpotHandler,
  timeoutMs: number,
) => boolean;

export class RuntimeActorJoinReplyOperation implements ActorJoinReplyOperation {
  private readonly _invoke: (parts: readonly MessageLike[]) => void;
  private readonly _payload = new OperationPayload();

  constructor(invoke: (parts: readonly MessageLike[]) => void) {
    this._invoke = invoke;
  }

  message(message: MessageLike): ActorJoinReplyOperation {
    this._payload.append(message);
    return this;
  }

  submit(): void {
    this._invoke(this._payload.consume());
  }
}

type ReplyOpInvoker = (callback: ReplyHandler, timeoutMs: number) => boolean;

export class ReplyHandlerOperation {
  protected readonly _invoke: ReplyOpInvoker;
  protected _timeoutMs = 0;
  protected _submitted = false;

  constructor(invoke: ReplyOpInvoker) {
    this._invoke = invoke;
  }

  protected ensureOpen(): void {
    if (this._submitted) {
      throw new TypeError('operation has already been submitted');
    }
  }

  protected markSubmitted(): void {
    this._submitted = true;
  }

  timeout(timeoutMs: number): this {
    this.ensureOpen();
    this._timeoutMs = timeoutMs | 0;
    return this;
  }

  submitAsync(): Promise<Message[]> {
    this.ensureOpen();
    return new Promise((resolve, reject) => {
      this.markSubmitted();
      this._invoke((result, parts) => {
        if (result !== RequestResult.Ok) {
          reject(requestErrorFromResult(result, 'actor operation failed'));
          return;
        }
        resolve(parts);
      }, this._timeoutMs);
    });
  }

  submit(callback: ReplyHandler): boolean {
    this.ensureOpen();
    this.markSubmitted();
    return this._invoke(callback, this._timeoutMs);
  }
}

export class RuntimeActorLeaveOperation extends ReplyHandlerOperation implements ActorLeaveOperation {}
export class RuntimeActorDestroyOperation extends ReplyHandlerOperation implements ActorDestroyOperation {}
export class RuntimeActorBindOperation extends ReplyHandlerOperation implements ActorBindOperation {}
export class RuntimeActorUnbindOperation extends ReplyHandlerOperation implements ActorUnbindOperation {}

type ResultHandlerInvoker<TResult extends { result: RequestResult }> = (
  callback: (result: TResult) => void,
  timeoutMs: number,
) => boolean;

class ResultHandlerOperation<TResult extends { result: RequestResult }> {
  private readonly _invoke: ResultHandlerInvoker<TResult>;
  private readonly _errorMessage: string;
  private _timeoutMs = 0;
  private _submitted = false;

  constructor(invoke: ResultHandlerInvoker<TResult>, errorMessage: string) {
    this._invoke = invoke;
    this._errorMessage = errorMessage;
  }

  protected ensureOpen(): void {
    if (this._submitted) {
      throw new TypeError('operation has already been submitted');
    }
  }

  timeout(timeoutMs: number): this {
    this.ensureOpen();
    this._timeoutMs = timeoutMs | 0;
    return this;
  }

  submitAsync(): Promise<TResult> {
    this.ensureOpen();
    this._submitted = true;
    return new Promise((resolve, reject) => {
      this._invoke((result) => {
        if (result.result !== RequestResult.Ok) {
          reject(requestErrorFromResult(result.result, this._errorMessage));
          return;
        }
        resolve(result);
      }, this._timeoutMs);
    });
  }

  submit(callback: (result: TResult) => void): boolean {
    this.ensureOpen();
    this._submitted = true;
    return this._invoke(callback, this._timeoutMs);
  }
}

export class RuntimeActorJoinEntrySpotOperation
  extends ResultHandlerOperation<ActorJoinEntrySpotResult>
  implements ActorJoinEntrySpotOperation {
  constructor(invoke: ActorJoinEntrySpotInvoker) {
    super(invoke, 'actor entry spot join failed');
  }
}

type ActorLookupInvoker = (callback: ActorLookupHandler, timeoutMs: number) => boolean;

export class RuntimeActorLookupOperation
  extends ResultHandlerOperation<ActorLookupResult>
  implements ActorLookupOperation {
  constructor(invoke: ActorLookupInvoker) {
    super(invoke, 'actor lookup failed');
  }
}
