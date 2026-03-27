// SPDX-License-Identifier: MPL-2.0

'use strict';

const { requireNative } = require('../native');
const { BaseSocket } = require('./base_socket');
const { normalizeBufferLike, normalizeMessagePayload, normalizeMultipart } = require('./socket_support');

class SendSocket extends BaseSocket {
  send(message, flags = 0) {
    return requireNative().socketSend(
      this._native,
      normalizeMessagePayload(message),
      flags | 0
    );
  }

  sendParts(parts, flags = 0) {
    return requireNative().socketSendParts(this._native, normalizeMultipart(parts), flags | 0);
  }

  sendFrom(buffer, length, flags = 0) {
    return requireNative().socketSendFrom(
      this._native,
      normalizeBufferLike(buffer, 'buffer'),
      length | 0,
      flags | 0
    );
  }
}

module.exports = {
  SendSocket
};
