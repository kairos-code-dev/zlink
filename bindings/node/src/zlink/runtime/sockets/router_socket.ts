// SPDX-License-Identifier: MPL-2.0

import { RouterSocketOptions } from './socket_options';
import { normalizeMessageLikePayload, toMessageParts } from '../buffers/message_conversion';
import { normalizeRoutingId } from '../core/routing_id';
import {
  NativeSocketType,
  RuntimeRequestOperation,
  RoutedMessageSocket,
  SendFlags,
  RuntimeSendOperation,
  SocketOption,
  SubmitResult,
  RoutingId,
  requireNative,
  configCall,
  submitNativeError,
  normalizeReplyFlags,
  executeNativeRequest,
  startRequestProgress,
  RuntimeReplyOperation,
  type Context,
  type Message,
  type MessageLike,
  type RequestCallback,
  type RequestOperation,
  type ReplyOperation,
  type SendOperation,
} from './socket_operations';

export class RouterSocket extends RoutedMessageSocket {
  readonly options: RouterSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.ROUTER); this.options = RouterSocketOptions.create(this); }
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
  request(peerRid: RoutingId): RequestOperation {
    return new RuntimeRequestOperation((parts, cbOrTimeout, opFlags, opTimeout) =>
      this.requestDirect(peerRid, parts, cbOrTimeout, opFlags, opTimeout)
    );
  }
  private requestDirect(
    peerRid: RoutingId,
    payloadOrParts: readonly MessageLike[],
    callbackOrTimeout?: RequestCallback | number,
    flagsOrTimeout?: SendFlags | number,
    maybeTimeout?: number,
  ): Promise<Message[]> | boolean {
    const parts = Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)];
    const peer = normalizeRoutingId(peerRid, 'peerRid');
    const nativeHandle = this.nativeHandle();
    return executeNativeRequest({
      callbackOrTimeout,
      flagsOrTimeout,
      maybeTimeout,
      startProgress: () => startRequestProgress(nativeHandle),
      invoke: (callback, flags, timeoutMs) => {
        requireNative().routerRequest(
          nativeHandle,
          peer,
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
  sendToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): SendOperation {
    return new RuntimeSendOperation((parts, opFlags) => this.sendToSpotDirect(destNodeRid, destSpotRid, parts, opFlags));
  }
  /** @internal */
  sendToSpotDirect(destNodeRid: RoutingId, destSpotRid: RoutingId, payloadOrParts: readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
    try {
      requireNative().routerSpotSend(
        this.nativeHandle(),
        normalizeRoutingId(destNodeRid),
        normalizeRoutingId(destSpotRid),
        Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)],
        flags | 0
      );
      return true;
    } catch (error) {
      const submitError = submitNativeError(error, flags, 'sendToSpot failed');
      if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
        return false;
      }
      throw submitError;
    }
  }
  protected sendToSpotFromRoutedMessage(destNodeRid: RoutingId, destSpotRid: RoutingId, parts: readonly Message[], flags: SendFlags): boolean {
    return this.sendToSpotDirect(destNodeRid, destSpotRid, parts, flags);
  }
  requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): RequestOperation {
    return new RuntimeRequestOperation((parts, cbOrTimeout, opFlags, opTimeout) =>
      this.requestToSpotDirect(destNodeRid, destSpotRid, parts, cbOrTimeout, opFlags, opTimeout)
    );
  }
  private requestToSpotDirect(destNodeRid: RoutingId, destSpotRid: RoutingId, payloadOrParts: readonly MessageLike[], callbackOrTimeout?: RequestCallback | number, flagsOrTimeout?: SendFlags | number, maybeTimeout?: number): Promise<Message[]> | boolean {
    const parts = Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)];
    const nodeRid = normalizeRoutingId(destNodeRid);
    const spotRid = normalizeRoutingId(destSpotRid);
    const nativeHandle = this.nativeHandle();
    return executeNativeRequest({
      callbackOrTimeout,
      flagsOrTimeout,
      maybeTimeout,
      promiseTimeoutMayUseFlagsOrTimeout: true,
      startProgress: () => startRequestProgress(nativeHandle),
      invoke: (callback, flags, timeoutMs) => {
        requireNative().routerSpotRequest(
          nativeHandle,
          nodeRid,
          spotRid,
          parts,
          callback,
          flags | 0,
          timeoutMs | 0
        );
      },
      submitErrorMessage: 'requestToSpot failed',
      requestErrorMessage: 'requestToSpot failed'
    });
  }
  replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint): ReplyOperation {
    return new RuntimeReplyOperation((parts, opFlags) => this.replyToSpotDirect(destNodeRid, destSpotRid, requestSeq, parts, opFlags));
  }
  private replyToSpotDirect(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint, payloadOrParts: readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
    normalizeReplyFlags(flags);
    const normalizedDestNodeRid = normalizeRoutingId(destNodeRid);
    const normalizedDestSpotRid = normalizeRoutingId(destSpotRid);
    const parts = Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)];
    try {
      requireNative().routerSpotReply(
        this.nativeHandle(),
        normalizedDestNodeRid,
        normalizedDestSpotRid,
        requestSeq,
        parts
      );
    } catch (error) {
      throw submitNativeError(error, flags, 'replyToSpot failed');
    }
  }
  reply(peerRid: RoutingId, requestSeq: bigint): ReplyOperation {
    return new RuntimeReplyOperation((parts, opFlags) => this.replyDirect(peerRid, requestSeq, parts, opFlags));
  }
  private replyDirect(peerRid: RoutingId, requestSeq: bigint, payloadOrParts: readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
    normalizeReplyFlags(flags);
    const normalizedPeerRid = normalizeRoutingId(peerRid, 'peerRid');
    const parts = Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)];
    try {
      requireNative().routerReply(
        this.nativeHandle(),
        normalizedPeerRid,
        requestSeq,
        parts
      );
    } catch (error) {
      throw submitNativeError(error, flags, 'reply failed');
    }
  }
}
