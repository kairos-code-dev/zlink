// SPDX-License-Identifier: MPL-2.0

import { requireNative } from '../native';

export class NativeSocketHandle {
  private _native: unknown | null;
  private readonly _own: boolean;

  constructor(nativeHandle: unknown, own = true) {
    this._native = nativeHandle;
    this._own = own === true;
  }

  value(): unknown {
    return this._native;
  }

  streamDetach(): void {
    if (!this._native) return;
    requireNative().socketStreamDetach(this._native);
  }

  close(): void {
    if (!this._native) return;
    try {
      this.streamDetach();
    } catch (_) {}
    if (this._own) requireNative().socketClose(this._native);
    this._native = null;
  }
}
