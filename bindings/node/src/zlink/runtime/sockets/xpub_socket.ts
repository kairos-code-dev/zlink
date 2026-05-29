// SPDX-License-Identifier: MPL-2.0

import {
  CommonSocketOptions,
  DealerSocketOptions,
  PubSocketOptions,
  RouterSocketOptions,
  StreamSocketOptions,
  SubSocketOptions,
} from './socket_options';
import { SocketBase } from './socket_base';
import { messageFromNativeBuffer, normalizeMessageLikePayload, toMessageParts } from '../buffers/message_conversion';
import { normalizeRoutingId } from '../core/routing_id';
import {
  MessageSocket,
  NativeSocketType,
  PublisherSocket,
  RecvFlags,
  Received,
  DefaultRequestOperation,
  RoutedMessageSocket,
  SendFlags,
  DefaultSendOperation,
  SocketOption,
  SubscriberSocket,
  SubscriptionEvent,
  SubmitResult,
  TopicMessage,
  RoutingId,
  wrapRoutingId,
  validateCString,
  requireNative,
  configCall,
  handlerCall,
  recvNativeError,
  submitNativeError,
  submitErrorFromResult,
  normalizeBufferLike,
  normalizeReplyFlags,
  materializeReceived,
  materializeReceivedInto,
  materializeTopicMessage,
  adoptTopicMessage,
  executeNativeRequest,
  startRequestProgress,
  DefaultReplyOperation,
  RecvResult,
  requestErrorFromResult,
  type ActorBindOperation,
  type ActorRef,
  type ActorUnbindOperation,
  type BufferLike,
  type Context,
  type Message,
  type MessageLike,
  type MessageSnapshot,
  type RequestCallback,
  type RequestOperation,
  type ReplyOperation,
  type SendOperation,
  type SocketSendReadyHandler,
  type StreamPacketHandler,
} from './socket_operations';

export class XPubSocket extends PublisherSocket {
  readonly options: PubSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.XPUB); this.options = PubSocketOptions.create(this); }
  receiveSubscriptionEvent(result: SubscriptionEvent, flags?: RecvFlags): boolean;
  receiveSubscriptionEvent(resultOrFlags: SubscriptionEvent | RecvFlags = RecvFlags.None,
                           maybeFlags: RecvFlags = RecvFlags.None): SubscriptionEvent | null | boolean {
    const hasResult = resultOrFlags instanceof SubscriptionEvent;
    const flags = hasResult ? maybeFlags : resultOrFlags as RecvFlags;
    let raw;
    try {
      raw = ((flags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().socketTrySubscriptionEvent(this.nativeHandle()) as { routingId?: Buffer | null; topic: string; subscribed: boolean } | null
        : requireNative().socketSubscriptionEvent(this.nativeHandle(), flags | 0) as { routingId?: Buffer | null; topic: string; subscribed: boolean } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'subscription event recv failed');
    }
    if (!raw) {
      return hasResult ? false : null;
    }
    const event = SubscriptionEvent.create(raw.topic, raw.subscribed, wrapRoutingId(raw.routingId ?? null));
    if (hasResult) {
      resultOrFlags.adoptFrom(event);
      return true;
    }
    return event;
  }
  setSendReadyHandler(handler: SocketSendReadyHandler): void {
    handlerCall('send-ready handler registration failed', () => {
      requireNative().socketSendReadyHandler(this.nativeHandle(), handler);
    });
  }
}
