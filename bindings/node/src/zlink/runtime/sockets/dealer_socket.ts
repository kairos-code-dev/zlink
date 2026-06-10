// SPDX-License-Identifier: MPL-2.0

import { DealerSocketOptions } from './socket_options';
import { normalizeOperationPayload } from '../buffers/message_conversion';
import { normalizeRoutingId } from '../core/routing_id';
import {
  MessageSocket,
  RuntimeRequestOperation,
} from './socket_operations';
import { configureSocketChannelName } from './socket_base';
import type { RuntimeContext as Context } from '../core/context';
import { configCall } from '../errors/native_errors';
import { executeNativeRequest } from '../messaging/request_executor';
import { startRequestProgress } from '../messaging/request_progress';
import { requireNative } from '../native/native';
import { RoutingId, type Message, type MessageLike } from '../../contracts';
import { SendFlags, SocketType as NativeSocketType } from '../../contracts/sockets/socket_constants';
import type { RequestCallback, RequestOperation } from '../../contracts/service';

export class DealerSocket extends MessageSocket {
  readonly options: DealerSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.DEALER); this.options = DealerSocketOptions.create(this); }
  getChannelName(): string {
    return configCall('channel name get failed', () =>
      requireNative().socketGetChannelName(this.nativeHandle()) as string
    );
  }
  setChannelName(channelName: string): void {
    configureSocketChannelName(this, channelName);
  }
  setRoutingId(routingId: RoutingId): void {
    const normalizedRoutingId = normalizeRoutingId(routingId);
    configCall('routing id set failed', () => {
      requireNative().handleSetRoutingId(this.nativeHandle(), normalizedRoutingId);
    });
  }
  getRoutingId(): RoutingId {
    return RoutingId.from(
      configCall('routing id get failed', () =>
        requireNative().handleGetRoutingId(this.nativeHandle()) as Buffer
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
    payloadOrParts: MessageLike | readonly MessageLike[],
    callbackOrTimeout?: RequestCallback | number,
    flagsOrTimeout?: SendFlags | number,
    maybeTimeout?: number,
  ): Promise<Message[]> | boolean {
    const parts = normalizeOperationPayload(payloadOrParts);
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
