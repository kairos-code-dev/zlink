// SPDX-License-Identifier: MPL-2.0

'use strict';

function normalizeBufferLike(value, label = 'value') {
  if (Buffer.isBuffer(value)) return value;
  if (value instanceof Uint8Array) {
    return Buffer.from(value.buffer, value.byteOffset, value.byteLength);
  }
  if (typeof value === 'string') return Buffer.from(value);
  throw new TypeError(`${label} must be Buffer, Uint8Array, or string`);
}

module.exports = {
  normalizeBufferLike
};
