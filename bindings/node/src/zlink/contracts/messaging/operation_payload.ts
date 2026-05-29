// SPDX-License-Identifier: MPL-2.0

export class OperationPayload<TInput, TStored = TInput> {
  private readonly _parts: TStored[] = [];
  private readonly _normalize: (value: TInput) => TStored;
  private _submitted = false;

  constructor(normalize: (value: TInput) => TStored) {
    this._normalize = normalize;
  }

  append(message: TInput): void {
    this.ensureOpen();
    this._parts.push(this._normalize(message));
  }

  ensureOpen(): void {
    if (this._submitted) {
      throw new TypeError('operation has already been submitted');
    }
  }

  consume(): readonly TStored[] {
    this.ensureOpen();
    if (this._parts.length === 0) {
      throw new TypeError('operation requires at least one message');
    }
    this._submitted = true;
    return this._parts;
  }
}
