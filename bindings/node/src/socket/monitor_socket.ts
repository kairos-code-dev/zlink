// SPDX-License-Identifier: MPL-2.0

import { requireNative } from '../native';
import type {
  MonitorSnapshot,
  SocketMonitorEventValue,
  SocketMonitorHandler
} from '../index';

export class MonitorSocket {
  /** @internal */
  private _native: unknown | null;

  constructor(handle: unknown) {
    this._native = handle;
  }

  recv(): SocketMonitorEventValue {
    return requireNative().monitorRecv(this._native) as SocketMonitorEventValue;
  }

  tryRecv(): SocketMonitorEventValue | null {
    return requireNative().monitorTryRecv(this._native) as SocketMonitorEventValue | null;
  }

  onEvent(handler: SocketMonitorHandler): void {
    if (typeof handler !== 'function') {
      throw new TypeError('handler must be a function');
    }
    requireNative().monitorHandler(this._native, handler);
  }

  snapshot(): MonitorSnapshot {
    return requireNative().monitorSnapshot(this._native) as MonitorSnapshot;
  }

  close(): void {
    if (!this._native) return;
    requireNative().monitorClose(this._native);
    this._native = null;
  }

  [Symbol.dispose](): void {
    this.close();
  }

  async [Symbol.asyncDispose](): Promise<void> {
    this.close();
  }
}
