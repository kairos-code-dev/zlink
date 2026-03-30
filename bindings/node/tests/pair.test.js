'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist');

test('pair messaging uses Message and Received by default', () => {
  const ctx = new zlink.Context();
  const sender = new zlink.PairSocket(ctx);
  const receiver = new zlink.PairSocket(ctx);

  sender.bind('inproc://pair-contract');
  receiver.connect('inproc://pair-contract');
  sender.send(zlink.Message.copyOf('ping'));

  const received = receiver.recv();
  assert.equal(received.parts.length, 1);
  assert.equal(received.parts[0].toString(), 'ping');
  assert.equal(received.routingId, null);

  receiver.close();
  sender.close();
  ctx.close();
});
