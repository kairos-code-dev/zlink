// SPDX-License-Identifier: MPL-2.0

import { requireNative } from '../native';
import { NativeSocketHandle } from './native_socket_handle';
import { MonitorSocket } from './monitor_socket';
import { normalizeBufferLike } from './socket_support';
import { SOCKET_MONITOR_EVENT_ALL } from './constants';
import { validateCString } from '../validation';
import type { BufferLike } from '../buffer_like';
import type { Context } from '../index';
import type { MonitorEventMask } from './constants';

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
    requireNative().socketBind(this.nativeHandle(), validateCString(endpoint, 'endpoint'));
  }

  unbind(endpoint: string): void {
    requireNative().socketUnbind(this.nativeHandle(), validateCString(endpoint, 'endpoint'));
  }

  setTlsServer(cert: string, key: string, requireClient = 0): void {
    requireNative().socketSetTlsServer(
      this.nativeHandle(),
      validateCString(cert, 'cert', Number.MAX_SAFE_INTEGER),
      validateCString(key, 'key', Number.MAX_SAFE_INTEGER),
      requireClient | 0
    );
  }

  setTlsClient(ca: string, host: string, trust = 0): void {
    requireNative().socketSetTlsClient(
      this.nativeHandle(),
      validateCString(ca, 'ca', Number.MAX_SAFE_INTEGER),
      validateCString(host, 'host', Number.MAX_SAFE_INTEGER),
      trust | 0
    );
  }

  protected setSockOptRaw(option: number, value: BufferLike | string): void {
    requireNative().socketSetOpt(
      this.nativeHandle(),
      option | 0,
      normalizeBufferLike(value, 'value')
    );
  }

  protected getSockOptRaw(option: number): Buffer {
    return requireNative().socketGetOpt(this.nativeHandle(), option | 0) as Buffer;
  }

  /** @internal */
  setOptionRawInternal(option: number, value: BufferLike | string): void {
    this.setSockOptRaw(option, value);
  }

  /** @internal */
  getOptionRawInternal(option: number): Buffer {
    return this.getSockOptRaw(option);
  }

  monitorOpen(events: MonitorEventMask = SOCKET_MONITOR_EVENT_ALL): MonitorSocket {
    return new MonitorSocket(requireNative().monitorOpen(this.nativeHandle(), events | 0));
  }

  close(): void {
    this._handle.close();
  }

  [Symbol.dispose](): void {
    this.close();
  }

  async [Symbol.asyncDispose](): Promise<void> {
    this.close();
  }
}
