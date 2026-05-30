// SPDX-License-Identifier: MPL-2.0

/** A value accepted where bytes are expected: a `Buffer`, `Uint8Array`, or `string` (encoded as UTF-8). */
export type BufferLike = Buffer | Uint8Array | string;

/** Normalize a {@link BufferLike} `value` to a `Buffer`; throws {@link TypeError} for other types. */
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
