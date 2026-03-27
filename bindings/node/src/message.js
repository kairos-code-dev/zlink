// SPDX-License-Identifier: MPL-2.0

'use strict';

const { normalizeBufferLike } = require('./buffer_like');

class Message {
  constructor(buffer, borrowed) {
    this._buffer = buffer;
    this._borrowed = borrowed === true;
    Object.freeze(this);
  }

  static copyOf(data, encoding = 'utf8') {
    if (typeof data === 'string') return new Message(Buffer.from(data, encoding), false);
    return new Message(Buffer.from(normalizeBufferLike(data, 'data')), false);
  }

  static wrap(buffer) {
    return new Message(normalizeBufferLike(buffer, 'buffer'), true);
  }

  static empty() {
    return new Message(Buffer.alloc(0), false);
  }

  toBuffer() {
    return this._borrowed ? this._buffer : Buffer.from(this._buffer);
  }

  byteLength() {
    return this._buffer.length;
  }
}

class Received {
  constructor(parts, routingId = null, hasMore = false) {
    this.parts = Object.freeze(parts.slice());
    this.routingId = routingId;
    this.hasMore = hasMore === true;
  }

  close() {}
}

module.exports = {
  Message,
  Received
};
