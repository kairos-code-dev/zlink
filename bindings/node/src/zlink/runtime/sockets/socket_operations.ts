// SPDX-License-Identifier: MPL-2.0

import { requireNative } from '../native/native';
import {
  configCall,
  handlerCall,
  recvNativeError,
  submitNativeError
} from '../errors/native_errors';
import { executeNativeRequest } from '../messaging/request_executor';
import { startRequestProgress } from '../messaging/request_progress';
import {
  adoptTopicMessage,
  materializeReceivedInto,
  materializeTopicMessage
} from '../messaging/message_materializer';
export { materializeReceived } from '../messaging/message_materializer';
import {
  normalizeMessageLikePayload,
  normalizeOperationPayload,
} from '../buffers/message_conversion';
import { normalizeRoutingId } from '../core/routing_id';
import { ConnectableSocket } from './socket_base';
export { Thread } from '../eventing/thread';
import { normalizeBufferLike } from '../../contracts/core/buffer_like';
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
  PollEventFlag
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
} from '../../contracts/errors/errors';

const PREBUILD_SUBSCRIBE_OPTION = 6;
const PREBUILD_UNSUBSCRIBE_OPTION = 7;

import type {
  SubscriptionEntry,
  SocketSendReadyHandler,
  SendOperation,
} from '../../contracts/service';
import {
  PublishOperation,
  RuntimeSendOperation,
} from './socket_operation_builders';
export {
  OperationPayload,
  PublishOperation,
  RuntimeReplyOperation,
  RuntimeRequestOperation,
  RuntimeSendOperation,
} from './socket_operation_builders';
import { wrapRoutingId } from '../../contracts/service/spot/spot_models';
export type { RuntimeContext as Context } from '../core/context';
export type { BufferLike } from '../../contracts/core/buffer_like';
export type { MessageLike };
export type {
  RequestCallback,
  RequestOperation,
  ReplyOperation,
  SendOperation,
  SocketSendReadyHandler,
  StreamPacketHandler,
  ActorBindOperation,
  ActorRef,
  ActorUnbindOperation,
} from '../../contracts/service';
export { NativeSocketType as SocketType };
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

export class SendSocket extends ConnectableSocket {
  send(): SendOperation {
    return new RuntimeSendOperation((parts, flags) => this.sendDirect(parts, flags));
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
  publish(topic: string): SendOperation {
    return new PublishOperation(
      (normalizedTopic, payload, flags) => this.publishDirect(normalizedTopic, payload, flags),
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
    const topic = validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER);
    configCall('subscription set failed', () => {
      const native = requireNative();
      if (native.socketSetSubscription) {
        native.socketSetSubscription(this.nativeHandle(), topic);
        return;
      }
      native.socketSetOpt(
        this.nativeHandle(),
        PREBUILD_SUBSCRIBE_OPTION,
        Buffer.from(topic)
      );
    });
  }
  unsetSubscription(topicOrPattern: string): void {
    const topic = validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER);
    configCall('subscription unset failed', () => {
      const native = requireNative();
      if (native.socketUnsetSubscription) {
        native.socketUnsetSubscription(this.nativeHandle(), topic);
        return;
      }
      native.socketSetOpt(
        this.nativeHandle(),
        PREBUILD_UNSUBSCRIBE_OPTION,
        Buffer.from(topic)
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

export class RoutedMessageSocket extends ConnectableSocket {
  send(routingId: RoutingId): SendOperation {
    return new RuntimeSendOperation((parts, flags) => this.sendDirect(routingId, parts, flags));
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
  protected sendToSpotFromRoutedMessage(_destNodeRid: RoutingId, _destSpotRid: RoutingId, _parts: readonly Message[], _flags: SendFlags): boolean {
    throw submitErrorFromResult(SubmitResult.InvalidState, 'spot-routed send is only supported by RouterSocket');
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
          return this.sendToSpotFromRoutedMessage(
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
  NativeSocketType,
  SocketOption,
  requireNative,
  validateCString,
  configCall,
  handlerCall,
  recvNativeError,
  submitNativeError,
  executeNativeRequest,
  startRequestProgress,
  adoptTopicMessage,
  materializeReceivedInto,
  materializeTopicMessage,
  normalizeBufferLike,
  wrapRoutingId,
};
export type { MessageSnapshot };
