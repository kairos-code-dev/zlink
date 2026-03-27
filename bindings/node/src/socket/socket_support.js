// SPDX-License-Identifier: MPL-2.0

'use strict';

const { normalizeBufferLike } = require('../buffer_like');
const { Message, Received } = require('../message');
const { requireNative } = require('../native');
const { ReceiveFlag } = require('./constants');

function normalizeMessagePayload(message, label = 'message') {
  return message instanceof Message
    ? message._buffer
    : normalizeBufferLike(message, label);
}

function normalizeMultipart(parts) {
  if (!Array.isArray(parts)) throw new TypeError('parts must be an array');
  return parts.map((part, index) => normalizeMessagePayload(part, `parts[${index}]`));
}

function recvMessage(socket, arg0 = 0, arg1 = undefined) {
  if (typeof arg1 === 'number') {
    return requireNative().socketRecv(socket._native, arg0 | 0, arg1 | 0);
  }
  if (typeof arg0 === 'number' && arg0 > ReceiveFlag.DONTWAIT) {
    return requireNative().socketRecv(socket._native, arg0 | 0, 0);
  }
  const raw = requireNative().socketRecvMessage(socket._native, arg0 | 0);
  return new Received(raw.parts, raw.routingId ?? null, raw.hasMore === true);
}

function recvInto(socket, buffer, flags = 0) {
  return requireNative().socketRecvInto(
    socket._native,
    normalizeBufferLike(buffer, 'buffer'),
    flags | 0
  );
}

function recvMsgInto(socket, buffer, flags = 0) {
  return requireNative().socketRecvMsgInto(
    socket._native,
    normalizeBufferLike(buffer, 'buffer'),
    flags | 0
  );
}

module.exports = {
  normalizeBufferLike,
  normalizeMessagePayload,
  normalizeMultipart,
  recvMessage,
  recvInto,
  recvMsgInto
};
