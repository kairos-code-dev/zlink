// SPDX-License-Identifier: MPL-2.0

import { requireNative } from '../native';
import { NativeSocketHandle } from './native_socket_handle';
import { MonitorSocket } from './monitor_socket';
import { SocketOption } from './constants';
import { normalizeBufferLike } from './socket_support';
import type { BufferLike } from '../buffer_like';
import type { Context } from '../index';

export class BaseSocket {
  /** @internal */
  protected readonly _handle: NativeSocketHandle;
  /** @internal */
  protected readonly _socketType: number;

  protected constructor(ctx: Context, type: number) {
    this._handle = new NativeSocketHandle(requireNative().socketNew(ctx.nativeHandle(), type));
    this._socketType = type;
  }

  /** @internal */
  nativeHandle(): unknown {
    return this._handle.value();
  }

  bind(endpoint: string): void {
    requireNative().socketBind(this.nativeHandle(), endpoint);
  }

  connect(endpoint: string): void {
    requireNative().socketConnect(this.nativeHandle(), endpoint);
  }

  setSockOpt(option: number, value: BufferLike | string): void {
    requireNative().socketSetOpt(
      this.nativeHandle(),
      option | 0,
      normalizeBufferLike(value, 'value')
    );
  }

  getSockOpt(option: number): Buffer {
    return requireNative().socketGetOpt(this.nativeHandle(), option | 0) as Buffer;
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

  monitorOpen(events: number): MonitorSocket {
    return new MonitorSocket(requireNative().monitorOpen(this.nativeHandle(), events | 0));
  }

  close(): void {
    this._handle.close();
  }
}
