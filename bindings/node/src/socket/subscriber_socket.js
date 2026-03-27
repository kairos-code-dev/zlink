// SPDX-License-Identifier: MPL-2.0

'use strict';

const { BaseSocket } = require('./base_socket');
const { SocketOption } = require('./constants');
const { normalizeBufferLike, recvMessage, recvInto, recvMsgInto } = require('./socket_support');

class SubscriberSocket extends BaseSocket {
  subscribe(filter) {
    this.setSockOpt(SocketOption.SUBSCRIBE, normalizeBufferLike(filter, 'filter'));
  }

  unsubscribe(filter) {
    this.setSockOpt(SocketOption.UNSUBSCRIBE, normalizeBufferLike(filter, 'filter'));
  }

  recv(arg0 = 0, arg1 = undefined) {
    return recvMessage(this, arg0, arg1);
  }

  recvInto(buffer, flags = 0) {
    return recvInto(this, buffer, flags);
  }

  recvMsgInto(buffer, flags = 0) {
    return recvMsgInto(this, buffer, flags);
  }
}

module.exports = {
  SubscriberSocket
};
