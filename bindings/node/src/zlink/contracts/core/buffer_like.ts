// SPDX-License-Identifier: MPL-2.0

export type BufferLike = Buffer | Uint8Array | string;

export function normalizeBufferLike(
  value: BufferLike | string,
  label = 'value'
): Buffer {
  if (Buffer.isBuffer(value)) return value;
  if (value instanceof Uint8Array) {
    return Buffer.from(value.buffer, value.byteOffset, value.byteLength);
  }
  if (typeof value === 'string') return Buffer.from(value);
  throw new TypeError(`${label} must be Buffer, Uint8Array, or string`);
}
