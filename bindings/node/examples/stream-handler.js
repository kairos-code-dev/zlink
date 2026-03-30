// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../dist');

const ctx = new zlink.Context();
const stream = new zlink.Socket(ctx, zlink.SocketType.STREAM);

try {
  stream.streamAttach(() => 0, zlink.StreamDispatchMode.LEN32BE);
} catch (err) {
  console.log(String(err));
}

stream.close();
ctx.close();
