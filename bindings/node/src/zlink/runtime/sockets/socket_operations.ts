// SPDX-License-Identifier: MPL-2.0

import { DefaultContext as Context } from '../core/context';

// SPDX-License-Identifier: MPL-2.0

import { randomBytes } from 'node:crypto';
import { requireNative } from '../native/native';
import {
  bindCall,
  closeCall,
  configCall,
  connectCall,
  handlerCall,
  lastError,
  nativeErrorMessage,
  readErrno,
  recvNativeError,
  submitNativeError
} from '../errors/native_errors';
import {
  acquireExternalRequestProgress,
  releaseExternalRequestProgress,
  startRequestProgress
} from '../messaging/request_progress';
import {
  executeNativeRequest,
  messagesFromNativeBuffers,
  normalizeCallbackFlagsAndTimeout,
  requestErrorFromResult
} from '../messaging/request_executor';
import {
  adoptTopicMessage,
  materializeReceived,
  materializeReceivedInto,
  materializeTopicMessage
} from '../messaging/message_materializer';
import {
  messageFromNativeBuffer,
  normalizeMessageLikePayload,
  normalizeOperationPayload,
  toMessageParts
} from '../buffers/message_conversion';
import { normalizeRoutingId } from '../core/routing_id';
import { ConnectableSocket } from './socket_base';
export { Thread } from '../eventing/thread';
import { normalizeBufferLike, type BufferLike } from '../../contracts/core/buffer_like';
import {
  Message,
  Received,
  RoutingId,
  TopicMessage,
  SubscriptionEvent,
  type MessageLike,
  type MessageSnapshot
} from '../../contracts';
import { validateCString } from '../options/validation';
import {
  SocketType as NativeSocketType, SendFlags, RecvFlags, RidDuplicatePolicy,
  PollEventFlag, type PollEventFlagValue, type RidDuplicatePolicy as RidDuplicatePolicyValue
} from '../../contracts/sockets/socket_constants';
import { SocketOption } from '../options/option_mapping';
import {
  BindError,
  BindResult,
  CloseError,
  CloseResult,
  ConfigError,
  ConfigResult,
  ConnectError,
  ConnectResult,
  HandlerError,
  HandlerResult,
  RecvError,
  RecvResult,
  RequestError,
  RequestResult,
  SubmitError,
  SubmitResult,
  ZlinkError,
  createError
} from '../../contracts/errors/errors';

import {
  AutoHwmProfile,
  type AutoHwmProfileValue,
} from '../../contracts/core';

import {
  SpotDispatchEvent,
  SpotDispatchSubjectKind,
  AutoConnectType,
  ServiceRole,
  ServiceKind,
  SpotRole,
  SpotPeerSource,
  SpotPeerKind,
  SpotPeerState,
  SpotNodeState,
  SpotNodeMode,
  SpotNodeSocketOwner,
  RegistryState,
  TopologySource,
  TopologyState,
  wrapRoutingId,
  type SpotDispatchSubjectKind as SpotDispatchSubjectKindValue,
  type ServiceRoleValue,
  type ServiceKindValue,
  type SpotRoleValue,
  type SpotPeerSourceValue,
  type SpotPeerKindValue,
  type SpotPeerStateValue,
  type SpotKindValue,
  type SpotNodeStateValue,
  type SpotNodeSocketOwnerValue,
  type RegistryStateValue,
  type TopologySourceValue,
  type TopologyStateValue,
  type SpotNodeModeValue,
  type MemberPeerEntry,
  type RegistryTopologyEntry,
  type RegistryServiceSummaryEntry,
  type RegistryStatus,
  type SpotNodeStatus,
  type SpotNodePeerEntry,
  type SpotNodeSubjectEntry,
  type SpotNodeSocketFilter,
  type SpotNodeSocketEntry,
  type RegistryServiceSummaryFilter,
  type RegistryTopologyFilter,
  type SpotNodePeerFilter,
  type SpotNodeSubjectFilter,
  type SubscriptionEntry,
  type SocketSendReadyHandler,
  type StreamPacketHandler,
  type SocketMonitorHandler,
  type SpotSendReadyHandler,
  type ActorRef,
  type ActorRoute,
  type SpotRoute,
  type ActorRecvInfo,
  type ActorJoinInfo,
  type ActorPart,
  type ActorJoinRequest,
  type ActorJoinResult,
  type ActorJoinEntrySpotResult,
  type ActorLookupResult,
  type SpotActorLifecycleInfo,
  type SpotActorLifecycleEvent,
  type ActorJoinHandler,
  type ActorJoinEntrySpotHandler,
  type ActorLookupHandler,
  type ReplyHandler,
  type SpotNodeSpotEntry,
  type SpotNodeActorEntry,
  type SpotDispatchInfo,
  type SpotDispatchEventHandler,
  type RequestCallback,
  type SendOp,
  type SendSubmitOp,
  type RequestOp,
  type RequestSubmitOp,
  type RequestCallbackSubmitOp,
  type ReplyOp,
  type ReplySubmitOp,
  type ActorJoinOp,
  type ActorJoinSubmitOp,
  type ActorJoinCallbackSubmitOp,
  type ActorJoinEntrySpotOp,
  type ActorJoinReplyOp,
  type ActorLeaveOp,
  type ActorDestroyOp,
  type ActorLookupOp,
  type ActorBindOp,
  type ActorUnbindOp,
} from '../../contracts/service';
import {
  MonitorSourceKind,
  MonitorEventType,
  MonitorEvent,
  type MonitorSourceKindValue,
  type MonitorStatus,
  type MonitorStatusRaw,
  type MonitorEventValueRaw,
  type TimerHandler,
} from '../../contracts/eventing';

export type { BufferLike, MessageLike };
export type { PollEventFlagValue, RidDuplicatePolicy as RidDuplicatePolicyValue, SocketTypeValue } from '../../contracts/sockets/socket_constants';
export {
  AutoHwmProfile,
} from '../../contracts/core';
export {
  SpotDispatchEvent,
  SpotDispatchSubjectKind,
  AutoConnectType,
  ServiceRole,
  ServiceKind,
  SpotRole,
  SpotPeerSource,
  SpotPeerKind,
  SpotPeerState,
  SpotNodeState,
  SpotNodeMode,
  SpotNodeSocketOwner,
  RegistryState,
  TopologySource,
  TopologyState,
} from '../../contracts/service';
export { NativeSocketType as SocketType };
export {
  MonitorSourceKind,
  MonitorEventType,
  MonitorEvent,
} from '../../contracts/eventing';
export type {
  ServiceRoleValue,
  ServiceKindValue,
  SpotRoleValue,
  SpotPeerSourceValue,
  SpotPeerKindValue,
  SpotPeerStateValue,
  SpotKindValue,
  SpotNodeStateValue,
  SpotNodeSocketOwnerValue,
  RegistryStateValue,
  TopologySourceValue,
  TopologyStateValue,
  SpotNodeModeValue,
  MemberPeerEntry,
  RegistryTopologyEntry,
  RegistryServiceSummaryEntry,
  RegistryStatus,
  SpotNodeStatus,
  SpotNodePeerEntry,
  SpotNodeSubjectEntry,
  SpotNodeSocketFilter,
  SpotNodeSocketEntry,
  RegistryServiceSummaryFilter,
  RegistryTopologyFilter,
  SpotNodePeerFilter,
  SpotNodeSubjectFilter,
  SubscriptionEntry,
  SocketSendReadyHandler,
  StreamPacketHandler,
  SocketMonitorHandler,
  SpotSendReadyHandler,
  ActorRef,
  ActorRoute,
  SpotRoute,
  ActorRecvInfo,
  ActorJoinInfo,
  ActorPart,
  ActorJoinRequest,
  ActorJoinResult,
  ActorJoinEntrySpotResult,
  ActorLookupResult,
  SpotActorLifecycleInfo,
  SpotActorLifecycleEvent,
  ActorJoinHandler,
  ActorJoinEntrySpotHandler,
  ActorLookupHandler,
  ReplyHandler,
  SpotNodeSpotEntry,
  SpotNodeActorEntry,
  SpotDispatchInfo,
  SpotDispatchEventHandler,
  RequestCallback,
  SendOp,
  SendSubmitOp,
  RequestOp,
  RequestSubmitOp,
  RequestCallbackSubmitOp,
  ReplyOp,
  ReplySubmitOp,
  ActorJoinOp,
  ActorJoinSubmitOp,
  ActorJoinCallbackSubmitOp,
  ActorJoinEntrySpotOp,
  ActorJoinReplyOp,
  ActorLeaveOp,
  ActorDestroyOp,
  ActorLookupOp,
  ActorBindOp,
  ActorUnbindOp,
} from '../../contracts/service';
export type {
  MonitorSourceKindValue,
  TimerHandler,
} from '../../contracts/eventing';
export {
  Message,
  Received,
  RoutingId,
  TopicMessage,
  SubscriptionEvent,
  SendFlags,
  RecvFlags,
  PollEventFlag,
  RidDuplicatePolicy,
  SubmitResult,
  RequestResult,
  RecvResult,
  HandlerResult,
  CloseResult,
  BindResult,
  ConnectResult,
  ConfigResult,
  ZlinkError,
  SubmitError,
  RequestError,
  RecvError,
  HandlerError,
  CloseError,
  BindError,
  ConnectError,
  ConfigError
};

export function submitErrorFromResult(result: SubmitResult, message: string): SubmitError {
  return new SubmitError(result, 0, message);
}

export function normalizeReplyFlags(flags: SendFlags = SendFlags.None): SendFlags {
  const normalized = flags | 0;
  if (normalized !== SendFlags.None) {
    throw submitErrorFromResult(
      SubmitResult.NotSupported,
      'reply flags are not supported by the current core library'
    );
  }
  return normalized as SendFlags;
}

export type SendInvoker = (parts: readonly MessageLike[], flags: SendFlags) => boolean;
export type RequestInvoker = (
  parts: readonly MessageLike[],
  callbackOrTimeout?: RequestCallback | number,
  flagsOrTimeout?: SendFlags | number,
  maybeTimeout?: number
) => Promise<Message[]> | boolean;
export type ReplyInvoker = (parts: readonly MessageLike[], flags: SendFlags) => void;

export class OperationPayload {
  private readonly _parts: MessageLike[] = [];
  private _submitted = false;

  append(message: MessageLike): void {
    this.ensureOpen();
    this._parts.push(message);
  }

  ensureOpen(): void {
    if (this._submitted) {
      throw new TypeError('operation has already been submitted');
    }
  }

  consume(): readonly MessageLike[] {
    this.ensureOpen();
    if (this._parts.length === 0) {
      throw new TypeError('operation requires at least one message');
    }
    this._submitted = true;
    return this._parts;
  }
}

export class SendOperation implements SendOp, SendSubmitOp {
  private readonly _invoke: SendInvoker;
  private readonly _payload = new OperationPayload();
  private _flags: SendFlags = SendFlags.None;

  constructor(invoke: SendInvoker) {
    this._invoke = invoke;
  }

  message(message: MessageLike): SendSubmitOp {
    this._payload.append(message);
    return this;
  }

  flags(flags: SendFlags): SendSubmitOp {
    this._payload.ensureOpen();
    this._flags = flags;
    return this;
  }

  submit(): boolean {
    return this._invoke(this._payload.consume(), this._flags);
  }
}

export class PublishOperation implements SendOp, SendSubmitOp {
  private readonly _socket: PublisherSocket;
  private readonly _topic: string;
  private _single: MessageLike | null = null;
  private _parts: MessageLike[] | null = null;
  private _submitted = false;
  private _flags: SendFlags = SendFlags.None;

  constructor(socket: PublisherSocket, topic: string) {
    this._socket = socket;
    this._topic = topic;
  }

  message(message: MessageLike): SendSubmitOp {
    this.ensureOpen();
    if (this._parts) {
      this._parts.push(message);
    } else if (this._single) {
      this._parts = [this._single, message];
      this._single = null;
    } else {
      this._single = message;
    }
    return this;
  }

  flags(flags: SendFlags): SendSubmitOp {
    this.ensureOpen();
    this._flags = flags;
    return this;
  }

  submit(): boolean {
    this.ensureOpen();
    const payload = this._parts ?? this._single;
    if (!payload) {
      throw new TypeError('operation requires at least one message');
    }
    this._submitted = true;
    return this._socket.publishDirect(this._topic, payload, this._flags);
  }

  private ensureOpen(): void {
    if (this._submitted) {
      throw new TypeError('operation has already been submitted');
    }
  }
}

export class RequestOperation implements RequestOp, RequestSubmitOp, RequestCallbackSubmitOp {
  private readonly _invoke: RequestInvoker;
  private readonly _payload = new OperationPayload();
  private _timeoutMs = 0;
  private _flags: SendFlags = SendFlags.None;
  private _callbackMode = false;

  constructor(invoke: RequestInvoker) {
    this._invoke = invoke;
  }

  message(message: MessageLike): RequestSubmitOp {
    this._payload.append(message);
    return this;
  }

  timeout(timeoutMs: number): RequestSubmitOp {
    this._payload.ensureOpen();
    this._timeoutMs = timeoutMs | 0;
    return this;
  }

  flags(flags: SendFlags): RequestCallbackSubmitOp {
    this._payload.ensureOpen();
    this._flags = flags;
    this._callbackMode = true;
    return this;
  }

  submitAsync(): Promise<Message[]> {
    return this._invoke(this._payload.consume(), this._timeoutMs) as Promise<Message[]>;
  }

  submit(callback: RequestCallback): boolean {
    const flags = this._callbackMode ? this._flags : SendFlags.None;
    return this._invoke(this._payload.consume(), callback, flags, this._timeoutMs) as boolean;
  }
}

export class ReplyOperation implements ReplyOp, ReplySubmitOp {
  private readonly _invoke: ReplyInvoker;
  private readonly _payload = new OperationPayload();
  private _flags: SendFlags = SendFlags.None;

  constructor(invoke: ReplyInvoker) {
    this._invoke = invoke;
  }

  message(message: MessageLike): ReplySubmitOp {
    this._payload.append(message);
    return this;
  }

  flags(flags: SendFlags): ReplySubmitOp {
    this._payload.ensureOpen();
    this._flags = flags;
    return this;
  }

  submit(): void {
    this._invoke(this._payload.consume(), this._flags);
  }
}

export class SendSocket extends ConnectableSocket {
  send(): SendOp {
    return new SendOperation((parts, flags) => this.sendDirect(parts, flags));
  }
  protected sendDirect(payloadOrParts: readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
    const payload = normalizeMessageLikePayload(payloadOrParts);
    if ((flags | 0) & (SendFlags.DontWait | 0)) {
      let result;
      try {
        result = Array.isArray(payload)
          ? requireNative().socketSendNoWaitResultParts(this.nativeHandle(), payload) as number
          : requireNative().socketSendNoWaitResult(this.nativeHandle(), payload) as number;
      } catch (error) {
        throw submitNativeError(error, flags, 'send failed');
      }
      if (result === SubmitResult.Ok) return true;
      if (result === SubmitResult.Backpressured) return false;
      throw submitErrorFromResult(result as SubmitResult, 'send failed');
    }
    try {
      if (Array.isArray(payload)) {
        requireNative().socketSendParts(this.nativeHandle(), payload, flags | 0);
      } else {
        requireNative().socketSend(this.nativeHandle(), payload, flags | 0);
      }
      return true;
    } catch (error) {
      throw submitNativeError(error, flags, 'send failed');
    }
  }
}

export class PublisherSocket extends ConnectableSocket {
  publish(topic: string): SendOp {
    return new PublishOperation(
      this,
      validateCString(topic, 'topic', Number.MAX_SAFE_INTEGER)
    );
  }
  /** @internal */
  publishDirect(topic: string, payload: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
    const normalized = normalizeOperationPayload(payload);
    if ((flags | 0) & (SendFlags.DontWait | 0)) {
      let result;
      try {
        result = requireNative().socketTryPublish(
          this.nativeHandle(),
          topic,
          normalized
        ) as number;
      } catch (error) {
        throw submitNativeError(error, flags, 'publish failed');
      }
      if (result === SubmitResult.Ok) return true;
      if (result === SubmitResult.Backpressured) return false;
      throw submitErrorFromResult(result as SubmitResult, 'publish failed');
    }
    try {
      requireNative().socketPublish(this.nativeHandle(), topic, normalized, flags | 0);
      return true;
    } catch (error) {
      const submitError = submitNativeError(error, flags, 'publish failed');
      if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
        return false;
      }
      throw submitError;
    }
  }
}

export class MessageSocket extends SendSocket {
  /**
   * Canonical caller-provided storage recv. Pass a long-lived {@link Received}
   * and the binding refills its internal state in place each successful call.
   * Returns true on success, false when DontWait finds no data. See
   * doc/spec/bindings/README.md "Canonical Recv: Caller-Provided Storage".
   */
  recv(result: Received, flags: RecvFlags = RecvFlags.None): boolean {
    let raw;
    try {
      raw = ((flags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().socketRecvMessageNoWait(this.nativeHandle()) as { parts: MessageSnapshot[]; routingId?: Buffer | null; requestSeq?: bigint | null } | null
        : requireNative().socketRecvMessage(this.nativeHandle(), flags | 0) as { parts: MessageSnapshot[]; routingId?: Buffer | null; requestSeq?: bigint | null } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'recv failed');
    }
    if (raw == null) return false;
    materializeReceivedInto(result, raw);
    return true;
  }
  setSendReadyHandler(handler: SocketSendReadyHandler): void {
    handlerCall('send-ready handler registration failed', () => {
      requireNative().socketSendReadyHandler(this.nativeHandle(), handler);
    });
  }
}

export class SubscriberSocket extends ConnectableSocket {
  setSubscription(topicOrPattern: string): void {
    const topic = Buffer.from(validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER));
    configCall('subscription set failed', () => {
      requireNative().socketSetOpt(
        this.nativeHandle(),
        SocketOption.SUBSCRIBE | 0,
        topic
      );
    });
  }
  unsetSubscription(topicOrPattern: string): void {
    const topic = Buffer.from(validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER));
    configCall('subscription unset failed', () => {
      requireNative().socketSetOpt(
        this.nativeHandle(),
        SocketOption.UNSUBSCRIBE | 0,
        topic
      );
    });
  }
  subscriptionAt(index: number): SubscriptionEntry | null {
    return configCall('subscription lookup failed', () =>
      requireNative().subscriptionAt(this.nativeHandle(), index >>> 0) as SubscriptionEntry | null
    );
  }
  subscribe(result: TopicMessage, flags?: RecvFlags): boolean;
  subscribe(resultOrFlags: TopicMessage | RecvFlags = RecvFlags.None,
            maybeFlags: RecvFlags = RecvFlags.None): TopicMessage | null | boolean {
    const hasResult = resultOrFlags instanceof TopicMessage;
    const flags = hasResult ? maybeFlags : resultOrFlags as RecvFlags;
    let raw;
    try {
      raw = ((flags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().socketTrySubscribeMessage(this.nativeHandle()) as { topic: string; parts: MessageSnapshot[]; routingId?: Buffer | null } | null
        : requireNative().socketSubscribeMessage(this.nativeHandle(), flags | 0) as { topic: string; parts: MessageSnapshot[]; routingId?: Buffer | null } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'subscribe failed');
    }
    if (!raw) {
      return hasResult ? false : null;
    }
    if (hasResult) {
      adoptTopicMessage(resultOrFlags, raw);
      return true;
    }
    return materializeTopicMessage(raw);
  }
}

export type RouterSocket = { sendToSpotDirect(destNodeRid: RoutingId, destSpotRid: RoutingId, payloadOrParts: readonly MessageLike[], flags?: SendFlags): boolean };
export class RoutedMessageSocket extends ConnectableSocket {
  send(routingId: RoutingId): SendOp {
    return new SendOperation((parts, flags) => this.sendDirect(routingId, parts, flags));
  }
  protected sendDirect(routingId: RoutingId, payload: readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
    const normalizedRoutingId = normalizeRoutingId(routingId);
    return this.sendDirectRaw(normalizedRoutingId, payload, flags);
  }
  protected sendDirectRaw(routingId: Buffer, payload: readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
    const normalized = normalizeMessageLikePayload(payload);
    if ((flags | 0) & (SendFlags.DontWait | 0)) {
      let result;
      try {
        result = Array.isArray(normalized)
          ? requireNative().socketSendRoutingNoWaitResultParts(this.nativeHandle(), routingId, normalized) as number
          : requireNative().socketSendRoutingNoWaitResult(this.nativeHandle(), routingId, normalized) as number;
      } catch (error) {
        throw submitNativeError(error, flags, 'send failed');
      }
      if (result === SubmitResult.Ok) return true;
      if (result === SubmitResult.Backpressured) return false;
      throw submitErrorFromResult(result as SubmitResult, 'send failed');
    }
    if (!Array.isArray(normalized)) {
      try {
        requireNative().socketSendRouting(this.nativeHandle(), routingId, normalized, flags | 0);
      } catch (error) {
        throw submitNativeError(error, flags, 'send failed');
      }
      return true;
    }
    const parts = [routingId, ...normalized];
    try {
      requireNative().socketSendParts(this.nativeHandle(), parts, flags | 0);
    } catch (error) {
      throw submitNativeError(error, flags, 'send failed');
    }
    return true;
  }
  /**
   * Canonical caller-provided storage recv. See {@link MessageSocket.recv}.
   */
  recv(result: Received, flags: RecvFlags = RecvFlags.None): boolean {
    let raw;
    try {
      raw = ((flags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().routerRecvMessageNoWait(this.nativeHandle()) as { parts: MessageSnapshot[]; routingId?: Buffer | null; spotRid?: Buffer | null; requestSeq?: bigint | null } | null
        : requireNative().routerRecvMessage(this.nativeHandle(), flags | 0) as { parts: MessageSnapshot[]; routingId?: Buffer | null; spotRid?: Buffer | null; requestSeq?: bigint | null } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'recv failed');
    }
    if (raw == null) return false;
    const send = (parts: readonly Message[], sendFlags: SendFlags) => {
        if (!raw.routingId) {
          throw submitErrorFromResult(SubmitResult.InvalidState, 'missing routed send target');
        }
        if (raw.spotRid) {
          return (this as unknown as RouterSocket).sendToSpotDirect(
            RoutingId.from(raw.routingId),
            RoutingId.from(raw.spotRid),
            parts,
            sendFlags
          );
        }
        return this.sendDirectRaw(raw.routingId, parts, sendFlags);
      };
    materializeReceivedInto(result, raw, undefined, send);
    return true;
  }
  setSendReadyHandler(handler: SocketSendReadyHandler): void {
    handlerCall('send-ready handler registration failed', () => {
      requireNative().socketSendReadyHandler(this.nativeHandle(), handler);
    });
  }
}

export {
  Context,
  NativeSocketType,
  SocketOption,
  requireNative,
  validateCString,
  bindCall,
  closeCall,
  configCall,
  connectCall,
  handlerCall,
  lastError,
  nativeErrorMessage,
  readErrno,
  recvNativeError,
  submitNativeError,
  executeNativeRequest,
  messagesFromNativeBuffers,
  normalizeCallbackFlagsAndTimeout,
  requestErrorFromResult,
  startRequestProgress,
  adoptTopicMessage,
  materializeReceived,
  materializeReceivedInto,
  materializeTopicMessage,
  normalizeBufferLike,
  wrapRoutingId,
};
export type { MessageSnapshot };
