// SPDX-License-Identifier: MPL-2.0

import { requireNative } from '../native';
import { DuplexSocket } from './duplex_socket';
import { SocketOption } from './constants';
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

  connect(endpoint: string): void {
    requireNative().socketConnect(this.nativeHandle(), endpoint);
  }

  disconnect(endpoint: string): void {
    requireNative().socketDisconnect(this.nativeHandle(), endpoint);
  }

  disconnectRid(peerRid: BufferLike): void {
    requireNative().socketDisconnectRid(
      this.nativeHandle(),
      normalizeBufferLike(peerRid, 'peerRid')
    );
  }

  setSockOpt(option: number, value: BufferLike | string): void {
    this.setSockOptRaw(option, value);
  }

  getSockOpt(option: number): Buffer {
    return this.getSockOptRaw(option);
  }

  setOption(option: number, value: BufferLike | string): void {
    this.setSockOpt(option, value);
  }

  getOption(option: number): Buffer {
    return this.getSockOpt(option);
  }

  setRoutingId(routingId: BufferLike): void {
    this.setSockOpt(SocketOption.ROUTING_ID, routingId);
  }

  getRoutingId(): Buffer {
    return this.getSockOpt(SocketOption.ROUTING_ID);
  }

  subscribe(filter: BufferLike | string): void {
    this.setSockOpt(SocketOption.SUBSCRIBE, normalizeBufferLike(filter, 'filter'));
  }

  unsubscribe(filter: BufferLike | string): void {
    this.setSockOpt(SocketOption.UNSUBSCRIBE, normalizeBufferLike(filter, 'filter'));
  }

  streamAttach(handler: StreamHandler): void {
    if (typeof handler !== 'function') {
      throw new TypeError('streamAttach handler must be a function');
    }
    requireNative().socketStreamAttach(this.nativeHandle(), handler, 0);
  }

  streamAttachRaw(handler: StreamHandler): void {
    this.streamAttach(handler);
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
