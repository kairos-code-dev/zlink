// SPDX-License-Identifier: MPL-2.0

'use strict';

const { SendSocket } = require('./send_socket');
const { recvMessage, recvInto, recvMsgInto } = require('./socket_support');

class DuplexSocket extends SendSocket {
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
  DuplexSocket
};
