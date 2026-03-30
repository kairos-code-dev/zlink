// SPDX-License-Identifier: MPL-2.0

import { requireNative } from '../native';
import type { MonitorSnapshot, SocketMonitorEventValue } from '../index';

export class MonitorSocket {
  /** @internal */
  private _native: unknown | null;

  constructor(handle: unknown) {
    this._native = handle;
  }

  recv(): SocketMonitorEventValue {
    return requireNative().monitorRecv(this._native) as SocketMonitorEventValue;
  }

  snapshot(): MonitorSnapshot {
    return requireNative().monitorSnapshot(this._native) as MonitorSnapshot;
  }

  close(): void {
    if (!this._native) return;
    requireNative().monitorClose(this._native);
    this._native = null;
  }
}
