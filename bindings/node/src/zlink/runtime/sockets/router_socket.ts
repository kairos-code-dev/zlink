// SPDX-License-Identifier: MPL-2.0

import { RouterSocketOptions } from './socket_options';
import { normalizeOperationPayload } from '../buffers/message_conversion';
import { normalizeRoutingId } from '../core/routing_id';
import {
  RuntimeReplyOperation,
  RuntimeRequestOperation,
  RuntimeSendOperation,
  RoutedMessageSocket,
} from './socket_operations';
import { configureSocketChannelName } from './socket_base';
import { normalizeReplyFlags } from './socket_submit_errors';
import type { RuntimeContext as Context } from '../core/context';
import { configCall, submitNativeError } from '../errors/native_errors';
import { executeNativeRequest } from '../messaging/request_executor';
import { startRequestProgress } from '../messaging/request_progress';
import { requireNative } from '../native/native';
import { RoutingId, type Message, type MessageLike } from '../../contracts';
import { SubmitResult } from '../../contracts/errors/errors';
import { SendFlags, SocketType as NativeSocketType } from '../../contracts/sockets/socket_constants';
import type {
  ReplyOperation,
  RequestCallback,
  RequestOperation,
  SendOperation,
} from '../../contracts/service';

const native = requireNative();

export class RouterSocket extends RoutedMessageSocket {
  readonly options: RouterSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.ROUTER); this.options = RouterSocketOptions.create(this); }
  setChannelName(channelName: string): void {
    configureSocketChannelName(this, channelName);
  }
  setRoutingId(routingId: RoutingId): void {
    const normalizedRoutingId = normalizeRoutingId(routingId);
    configCall('routing id set failed', () => {
      native.handleSetRoutingId(this.nativeHandle(), normalizedRoutingId);
    });
  }
  getRoutingId(): RoutingId {
    return RoutingId.from(
      configCall('routing id get failed', () =>
        native.handleGetRoutingId(this.nativeHandle()) as Buffer
      )
    );
  }
  attachDiscovery(discovery: { nativeHandle(): unknown }): void {
    configCall('socket discovery attachment failed', () => {
      native.socketAttachDiscovery(this.nativeHandle(), discovery.nativeHandle());
    });
  }
  request(peerRid: RoutingId): RequestOperation {
    return new RuntimeRequestOperation((parts, cbOrTimeout, opFlags, opTimeout) =>
      this.requestDirect(peerRid, parts, cbOrTimeout, opFlags, opTimeout)
    );
  }
  private requestDirect(
    peerRid: RoutingId,
    payloadOrParts: MessageLike | readonly MessageLike[],
    callbackOrTimeout?: RequestCallback | number,
    flagsOrTimeout?: SendFlags | number,
    maybeTimeout?: number,
  ): Promise<Message[]> | boolean {
    const parts = normalizeOperationPayload(payloadOrParts);
    const peer = normalizeRoutingId(peerRid, 'peerRid');
    const nativeHandle = this.nativeHandle();
    return executeNativeRequest({
      callbackOrTimeout,
      flagsOrTimeout,
      maybeTimeout,
      startProgress: () => startRequestProgress(nativeHandle),
      invoke: (callback, flags, timeoutMs) => {
        native.routerRequest(
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
  sendToSpotDirect(destNodeRid: RoutingId, destSpotRid: RoutingId, payloadOrParts: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
    try {
      native.routerSpotSend(
        this.nativeHandle(),
        normalizeRoutingId(destNodeRid),
        normalizeRoutingId(destSpotRid),
        normalizeOperationPayload(payloadOrParts),
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
  private requestToSpotDirect(destNodeRid: RoutingId, destSpotRid: RoutingId, payloadOrParts: MessageLike | readonly MessageLike[], callbackOrTimeout?: RequestCallback | number, flagsOrTimeout?: SendFlags | number, maybeTimeout?: number): Promise<Message[]> | boolean {
    const parts = normalizeOperationPayload(payloadOrParts);
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
        native.routerSpotRequest(
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
  private replyToSpotDirect(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint, payloadOrParts: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
    normalizeReplyFlags(flags);
    const normalizedDestNodeRid = normalizeRoutingId(destNodeRid);
    const normalizedDestSpotRid = normalizeRoutingId(destSpotRid);
    const parts = normalizeOperationPayload(payloadOrParts);
    try {
      native.routerSpotReply(
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
  private replyDirect(peerRid: RoutingId, requestSeq: bigint, payloadOrParts: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
    normalizeReplyFlags(flags);
    const normalizedPeerRid = normalizeRoutingId(peerRid, 'peerRid');
    const parts = normalizeOperationPayload(payloadOrParts);
    try {
      native.routerReply(
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
