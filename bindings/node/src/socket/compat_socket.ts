// SPDX-License-Identifier: MPL-2.0

import { requireNative } from '../native';
import { DuplexSocket } from './duplex_socket';
import { SocketOption, StreamDispatchMode } from './constants';
import { normalizeBufferLike } from './socket_support';
import type { BufferLike } from '../buffer_like';

type StreamHandler = (routingId: Buffer, packets: Buffer[]) => number | void;

/**
 * @deprecated Use the concrete socket classes for canonical raw socket usage.
 */
export class Socket extends DuplexSocket {
  constructor(ctx: import('../index').Context, type: number) {
    super(ctx, type);
  }

  subscribe(filter: BufferLike | string): void {
    this.setSockOpt(SocketOption.SUBSCRIBE, normalizeBufferLike(filter, 'filter'));
  }

  unsubscribe(filter: BufferLike | string): void {
    this.setSockOpt(SocketOption.UNSUBSCRIBE, normalizeBufferLike(filter, 'filter'));
  }

  streamAttach(handler: StreamHandler, mode: number = StreamDispatchMode.NONE): void {
    if (typeof handler !== 'function') {
      throw new TypeError('streamAttach handler must be a function');
    }
    requireNative().socketStreamAttach(this.nativeHandle(), handler, mode | 0);
  }

  streamAttachRaw(handler: StreamHandler): void {
    this.streamAttach(handler, StreamDispatchMode.NONE);
  }

  streamAttachLen32be(handler: StreamHandler): void {
    this.streamAttach(handler, StreamDispatchMode.LEN32BE);
  }

  streamDetach(): void {
    this._handle.streamDetach();
  }

  streamPeerRoutingId(index = 0): Buffer | null {
    return requireNative().socketStreamPeerRoutingId(
      this.nativeHandle(),
      index | 0
    ) as Buffer | null;
  }

  streamSend(routingId: BufferLike, payload: BufferLike, flags = 0): number {
    return requireNative().socketStreamSend(
      this.nativeHandle(),
      normalizeBufferLike(routingId, 'routingId'),
      normalizeBufferLike(payload, 'payload'),
      flags | 0
    ) as number;
  }
}
