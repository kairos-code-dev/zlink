// SPDX-License-Identifier: MPL-2.0

import { RouterSocketOptions } from './socket_options';
import { normalizeOperationPayload } from '../buffers/message_conversion';
import { normalizeRoutingId } from '../core/routing_id';
import {
  RuntimeReplyOperation,
  RuntimeRequestOperation,
  RoutedMessageSocket,
} from './socket_operations';
import { configureSocketChannelName } from './socket_base';
import { normalizeReplyFlags } from './socket_submit_errors';
import type { RuntimeContext as Context } from '../core/context';
import { configCall, submitNativeError } from '../errors/native_errors';
import { getNativeHandle } from '../handles/native_handle';
import { executeNativeRequest } from '../messaging/request_executor';
import { startRequestProgress } from '../messaging/request_progress';
import { requireNative } from '../native/native';
import { RoutingId, type Message, type MessageLike } from '../../contracts';
import { SendFlags, SocketType as NativeSocketType } from '../../contracts/sockets/socket_constants';
import type {
  ReplyOperation,
  RequestCallback,
  RequestOperation,
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
      native.handleSetRoutingId(getNativeHandle(this), normalizedRoutingId);
    });
  }
  getRoutingId(): RoutingId {
    return RoutingId.from(
      configCall('routing id get failed', () =>
        native.handleGetRoutingId(getNativeHandle(this)) as Buffer
      )
    );
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
    const nativeHandle = getNativeHandle(this);
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
  reply(peerRid: RoutingId, requestSeq: bigint): ReplyOperation {
    return new RuntimeReplyOperation((parts, opFlags) => this.replyDirect(peerRid, requestSeq, parts, opFlags));
  }
  protected replyToRoutedMessage(
    sourceRid: RoutingId,
    requestSeq: bigint,
    parts: readonly Message[],
    flags: SendFlags,
  ): void {
    this.replyDirect(sourceRid, requestSeq, parts, flags);
  }
  private replyDirect(peerRid: RoutingId, requestSeq: bigint, payloadOrParts: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
    normalizeReplyFlags(flags);
    const normalizedPeerRid = normalizeRoutingId(peerRid, 'peerRid');
    const parts = normalizeOperationPayload(payloadOrParts);
    try {
      native.routerReply(
        getNativeHandle(this),
        normalizedPeerRid,
        requestSeq,
        parts
      );
    } catch (error) {
      throw submitNativeError(error, flags, 'reply failed');
    }
  }
}
