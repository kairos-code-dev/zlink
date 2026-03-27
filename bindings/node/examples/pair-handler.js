// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../src');

const ctx = new zlink.Context();
const left = new zlink.PairSocket(ctx);
const right = new zlink.PairSocket(ctx);

left.bind('inproc://example-pair-handler');
right.connect('inproc://example-pair-handler');
right.sendParts([zlink.Message.copyOf('alpha'), zlink.Message.copyOf('beta')]);

const received = left.recv();
for (const part of received.parts) {
  console.log(part.toString());
}

right.close();
left.close();
ctx.close();
