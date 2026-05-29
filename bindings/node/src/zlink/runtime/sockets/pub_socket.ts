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
  RequestOperation,
  RoutedMessageSocket,
  SendFlags,
  SendOperation,
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
  ReplyOperation,
  RecvResult,
  requestErrorFromResult,
  type ActorBindOp,
  type ActorRef,
  type ActorUnbindOp,
  type BufferLike,
  type Context,
  type Message,
  type MessageLike,
  type MessageSnapshot,
  type RequestCallback,
  type RequestOp,
  type ReplyOp,
  type SendOp,
  type SocketSendReadyHandler,
  type StreamPacketHandler,
} from './socket_operations';

export class PubSocket extends PublisherSocket {
  readonly options: PubSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.PUB); this.options = PubSocketOptions.create(this); }
  setSendReadyHandler(handler: SocketSendReadyHandler): void {
    handlerCall('send-ready handler registration failed', () => {
      requireNative().socketSendReadyHandler(this.nativeHandle(), handler);
    });
  }
  attachDiscovery(discovery: { nativeHandle(): unknown }): void {
    configCall('socket discovery attachment failed', () => {
      requireNative().socketAttachDiscovery(this.nativeHandle(), discovery.nativeHandle());
    });
  }
}
