// SPDX-License-Identifier: MPL-2.0

import type { PollEventFlagValue } from '../../contracts/sockets/socket_constants';
import { closeCall } from '../errors/native_errors';
import { requireNative } from '../native/native';

export interface PollEvent {
  readonly sourceKind: number;
  readonly slot: number;
  readonly revents: number;
  readonly fd: number;
}

export class PollEvents {
  private _native: unknown | null;
  private _readyCount = 0;
  readonly capacity: number;

  constructor(capacity: number) {
    if (!Number.isInteger(capacity) || capacity <= 0) {
      throw new RangeError('capacity must be a positive integer');
    }
    this.capacity = capacity;
    this._native = requireNative().pollEventsNew(capacity);
  }

  get readyCount(): number { return this._readyCount; }

  sourceKind(index: number): number {
    this.checkReadyIndex(index);
    return requireNative().pollEventsSourceKind(this._native, index | 0) as number;
  }

  slot(index: number): number {
    this.checkReadyIndex(index);
    return requireNative().pollEventsSlot(this._native, index | 0) as number;
  }

  revents(index: number): number {
    this.checkReadyIndex(index);
    return requireNative().pollEventsRevents(this._native, index | 0) as number;
  }

  fd(index: number): number {
    this.checkReadyIndex(index);
    return Number(requireNative().pollEventsFd(this._native, index | 0));
  }

  hasEvent(index: number, event: PollEventFlagValue): boolean {
    return (this.revents(index) & (event as number)) !== 0;
  }

  /** @internal */
  nativeHandle(): unknown {
    if (!this._native) throw new Error('PollEvents is closed');
    return this._native;
  }

  /** @internal */
  markReadyCount(count: number): void {
    if (count < 0 || count > this.capacity) {
      throw new RangeError('ready count out of range');
    }
    this._readyCount = count;
  }

  close(): void {
    if (!this._native) return;
    closeCall('poll events close failed', () => {
      requireNative().pollEventsDestroy(this._native);
    });
    this._native = null;
    this._readyCount = 0;
  }

  private checkReadyIndex(index: number): void {
    if (!Number.isInteger(index) || index < 0 || index >= this._readyCount) {
      throw new RangeError(`ready index ${index}`);
    }
  }
}

export { PollEvents as RuntimePollEvents };
