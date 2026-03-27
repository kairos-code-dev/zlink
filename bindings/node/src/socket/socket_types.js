// SPDX-License-Identifier: MPL-2.0

'use strict';

const { SocketType } = require('./constants');
const { SendSocket } = require('./send_socket');
const { DuplexSocket } = require('./duplex_socket');
const { SubscriberSocket } = require('./subscriber_socket');

class PubSocket extends SendSocket {
  constructor(ctx) {
    super(ctx, SocketType.PUB);
  }
}

class XPubSocket extends SendSocket {
  constructor(ctx) {
    super(ctx, SocketType.XPUB);
  }
}

class PairSocket extends DuplexSocket {
  constructor(ctx) {
    super(ctx, SocketType.PAIR);
  }
}

class DealerSocket extends DuplexSocket {
  constructor(ctx) {
    super(ctx, SocketType.DEALER);
  }
}

class RouterSocket extends DuplexSocket {
  constructor(ctx) {
    super(ctx, SocketType.ROUTER);
  }
}

class StreamSocket extends DuplexSocket {
  constructor(ctx) {
    super(ctx, SocketType.STREAM);
  }
}

class SubSocket extends SubscriberSocket {
  constructor(ctx) {
    super(ctx, SocketType.SUB);
  }
}

class XSubSocket extends SubscriberSocket {
  constructor(ctx) {
    super(ctx, SocketType.XSUB);
  }
}

module.exports = {
  PubSocket,
  XPubSocket,
  PairSocket,
  DealerSocket,
  RouterSocket,
  StreamSocket,
  SubSocket,
  XSubSocket
};
