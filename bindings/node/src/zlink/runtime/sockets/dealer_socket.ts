// SPDX-License-Identifier: MPL-2.0

import { DealerSocketOptions } from './socket_options';
import { normalizeMessageLikePayload, toMessageParts } from '../buffers/message_conversion';
import { normalizeRoutingId } from '../core/routing_id';
import {
  MessageSocket,
  NativeSocketType,
  RuntimeRequestOperation,
  SendFlags,
  SocketOption,
  RoutingId,
  validateCString,
  requireNative,
  configCall,
  executeNativeRequest,
  startRequestProgress,
  type Context,
  type Message,
  type MessageLike,
  type RequestCallback,
  type RequestOperation,
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
  request(): RequestOperation {
    return new RuntimeRequestOperation((parts, cbOrTimeout, opFlags, opTimeout) =>
      this.requestDirect(parts, cbOrTimeout, opFlags, opTimeout)
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
      startProgress: () => startRequestProgress(nativeHandle),
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
