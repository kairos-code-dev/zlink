'use strict';

const { once } = require('node:events');
const net = require('node:net');
const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist');

test('version matches core', () => {
  assert.deepEqual(zlink.version(), [5, 0, 4]);
});

test('legacy recv(size, flags) remains as compatibility path', () => {
  const ctx = new zlink.Context();
  const sender = new zlink.Socket(ctx, zlink.SocketType.PAIR);
  const receiver = new zlink.Socket(ctx, zlink.SocketType.PAIR);

  sender.bind('inproc://legacy-recv-contract');
  receiver.connect('inproc://legacy-recv-contract');
  sender.send('ping');

  const out = receiver.recv(16, 0);
  assert.equal(out.toString(), 'ping');

  receiver.close();
  sender.close();
  ctx.close();
});

test('recvInto reuses caller-provided buffer', () => {
  const ctx = new zlink.Context();
  const sender = new zlink.PairSocket(ctx);
  const receiver = new zlink.PairSocket(ctx);

  sender.bind('inproc://recv-into-contract');
  receiver.connect('inproc://recv-into-contract');
  sender.send(zlink.Message.copyOf('pong'));

  const target = Buffer.alloc(16);
  const received = receiver.recvInto(target);
  assert.equal(received, 4);
  assert.equal(target.subarray(0, received).toString(), 'pong');

  receiver.close();
  sender.close();
  ctx.close();
});

test('dedicated option helpers cover routing id and generic option access', () => {
  const ctx = new zlink.Context();
  const dealer = new zlink.DealerSocket(ctx);
  const routingId = Buffer.from('dealer-1');

  dealer.setRoutingId(routingId);
  assert.equal(dealer.getRoutingId().subarray(0, routingId.length).toString(), 'dealer-1');

  dealer.close();
  ctx.close();
});

test('stream helpers remain on compat socket and stay off canonical stream surface', () => {
  const ctx = new zlink.Context();
  const stream = new zlink.StreamSocket(ctx);
  const compat = new zlink.Socket(ctx, zlink.SocketType.STREAM);

  assert.equal(stream.streamAttach, undefined);
  assert.equal(stream.streamDetach, undefined);
  assert.equal(typeof compat.streamAttach, 'function');
  assert.equal(typeof compat.streamDetach, 'function');
  assert.doesNotThrow(() => compat.streamDetach());

  compat.close();
  stream.close();
  ctx.close();
});

test('compat stream helpers attach len32be dispatch and framed reply', async () => {
  const ctx = new zlink.Context();
  const compat = new zlink.Socket(ctx, zlink.SocketType.STREAM);
  let client;

  try {
    compat.bind('tcp://127.0.0.1:*');
    const endpoint = compat.getSockOpt(zlink.SocketOption.LAST_ENDPOINT)
      .toString('utf8')
      .replace(/\0.*$/, '');
    const port = Number(endpoint.split(':').pop());

    const packetsPromise = new Promise((resolve, reject) => {
      try {
        compat.streamAttach((routingId, packets) => {
          resolve({
            routingId: Buffer.from(routingId),
            packets: packets.map((packet) => Buffer.from(packet))
          });
          return 1;
        }, zlink.StreamDispatchMode.LEN32BE);
      } catch (err) {
        reject(err);
      }
    });

    client = net.createConnection({ host: '127.0.0.1', port });
    await once(client, 'connect');

    const payload = Buffer.from('ping');
    const framedPayload = Buffer.alloc(payload.length + 4);
    framedPayload.writeUInt32BE(payload.length, 0);
    payload.copy(framedPayload, 4);
    client.write(framedPayload);

    const received = await packetsPromise;
    assert.deepEqual(received.packets.map((packet) => packet.toString()), ['ping']);

    const peerRoutingId = compat.streamPeerRoutingId();
    assert.ok(Buffer.isBuffer(peerRoutingId));
    assert.deepEqual(peerRoutingId, received.routingId);

    const replyPromise = once(client, 'data');
    assert.equal(compat.streamSend(received.routingId, Buffer.from('pong')), 4);
    const [reply] = await replyPromise;
    assert.equal(reply.readUInt32BE(0), 4);
    assert.equal(reply.subarray(4, 8).toString(), 'pong');
  } finally {
    if (client) client.destroy();
    compat.streamDetach();
    compat.close();
    ctx.close();
  }
});
