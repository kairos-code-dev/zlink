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
  assert.ok(received.parts[0] instanceof zlink.Message);
  assert.equal(received.parts[0].toBuffer().toString(), 'ping');
  assert.equal(received.routingId, null);

  receiver.close();
  sender.close();
  ctx.close();
});

test('tryReceive returns null when no message is available', () => {
  const ctx = new zlink.Context();
  const pair = new zlink.PairSocket(ctx);

  assert.equal(pair.tryRecv(), null);

  pair.close();
  ctx.close();
});

test('recvHandler delivers multipart Message instances', async () => {
  const ctx = new zlink.Context();
  const sender = new zlink.PairSocket(ctx);
  const receiver = new zlink.PairSocket(ctx);

  sender.bind('inproc://pair-handler-contract');
  receiver.connect('inproc://pair-handler-contract');

  const received = await new Promise((resolve, reject) => {
    try {
      receiver.onReceive((routingId, parts) => {
        resolve({ routingId, parts });
      });
    } catch (err) {
      reject(err);
      return;
    }
    sender.send([zlink.Message.copyOf('left'), zlink.Message.copyOf('right')]);
  });

  assert.equal(received.routingId, null);
  assert.deepEqual(
    received.parts.map((part) => part.toBuffer().toString()),
    ['left', 'right']
  );

  receiver.close();
  sender.close();
  ctx.close();
});
