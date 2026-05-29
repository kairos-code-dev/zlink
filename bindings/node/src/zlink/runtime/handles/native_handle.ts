// SPDX-License-Identifier: MPL-2.0

export class NativeHandle {
  /** @internal */
  protected _native: unknown | null;

  protected constructor(native: unknown) {
    this._native = native;
  }

  /** @internal */
  nativeHandle(): unknown {
    return this._native;
  }

  close(): void {
    this._native = null;
  }
}
