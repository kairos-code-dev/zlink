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

export class DealerSocket extends MessageSocket {
  readonly options: DealerSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.DEALER); this.options = DealerSocketOptions.create(this); }
  setChannelName(channelName: string): void {
    const normalized = validateCString(channelName, 'channelName', Number.MAX_SAFE_INTEGER);
    configCall('channel name set failed', () => {
      requireNative().socketSetChannelName(
        this.nativeHandle(),
        normalized
      );
    });
  }
  getChannelName(): string {
    return configCall('channel name get failed', () =>
      requireNative().socketGetChannelName(this.nativeHandle()) as string
    );
  }
  setRoutingId(routingId: RoutingId): void {
    const normalizedRoutingId = normalizeRoutingId(routingId);
    configCall('routing id set failed', () => {
      requireNative().socketSetOpt(
        this.nativeHandle(),
        SocketOption.ROUTING_ID | 0,
        normalizedRoutingId
      );
    });
  }
  getRoutingId(): RoutingId {
    return RoutingId.from(
      configCall('routing id get failed', () =>
        requireNative().socketGetOpt(this.nativeHandle(), SocketOption.ROUTING_ID | 0) as Buffer
      )
    );
  }
  attachDiscovery(discovery: { nativeHandle(): unknown }): void {
    configCall('socket discovery attachment failed', () => {
      requireNative().socketAttachDiscovery(this.nativeHandle(), discovery.nativeHandle());
    });
  }
  request(): RequestOp {
    return new RequestOperation((parts, cbOrTimeout, opFlags, opTimeout) =>
      this.requestDirect(parts, cbOrTimeout as any, opFlags as any, opTimeout)
    );
  }
  private requestDirect(
    payloadOrParts: readonly MessageLike[],
    callbackOrTimeout?: RequestCallback | number,
    flagsOrTimeout?: SendFlags | number,
    maybeTimeout?: number,
  ): Promise<Message[]> | boolean {
    const parts = Array.isArray(payloadOrParts)
      ? toMessageParts(payloadOrParts)
      : [normalizeMessageLikePayload(payloadOrParts)];
    const nativeHandle = this.nativeHandle();
    return executeNativeRequest({
      callbackOrTimeout,
      flagsOrTimeout,
      maybeTimeout,
      startProgress: () => startRequestProgress(
        nativeHandle,
        (handle) => { void handle; }
      ),
      invoke: (callback, flags, timeoutMs) => {
        requireNative().dealerRequest(
          nativeHandle,
          parts,
          callback,
          flags | 0,
          timeoutMs | 0
        );
      },
      submitErrorMessage: 'request failed',
      requestErrorMessage: 'request failed'
    });
  }
}
