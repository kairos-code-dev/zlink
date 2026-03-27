// SPDX-License-Identifier: MPL-2.0

'use strict';

const { requireNative } = require('../native');
const { DuplexSocket } = require('./duplex_socket');
const { SocketOption } = require('./constants');
const { StreamDispatchMode } = require('./constants');
const { normalizeBufferLike } = require('./socket_support');

class Socket extends DuplexSocket {
  constructor(ctx, type) {
    super(ctx, type);
  }

  subscribe(filter) {
    this.setSockOpt(SocketOption.SUBSCRIBE, normalizeBufferLike(filter, 'filter'));
  }

  unsubscribe(filter) {
    this.setSockOpt(SocketOption.UNSUBSCRIBE, normalizeBufferLike(filter, 'filter'));
  }

  streamAttach(handler, mode = StreamDispatchMode.NONE) {
    if (typeof handler !== 'function') throw new TypeError('streamAttach handler must be a function');
    requireNative().socketStreamAttach(this._native, handler, mode | 0);
  }

  streamAttachRaw(handler) {
    this.streamAttach(handler, StreamDispatchMode.NONE);
  }

  streamAttachLen32be(handler) {
    this.streamAttach(handler, StreamDispatchMode.LEN32BE);
  }

  streamDetach() {
    this._handle.streamDetach();
  }

  streamPeerRoutingId(index = 0) {
    return requireNative().socketStreamPeerRoutingId(this._native, index | 0);
  }

  streamSend(routingId, payload, flags = 0) {
    return requireNative().socketStreamSend(
      this._native,
      normalizeBufferLike(routingId, 'routingId'),
      normalizeBufferLike(payload, 'payload'),
      flags | 0
    );
  }
}

module.exports = {
  Socket
};
