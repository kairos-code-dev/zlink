'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist');

test('routing id accepts 255-byte maximum and rejects overflow', () => {
  const ctx = new zlink.Context();
  const dealer = new zlink.DealerSocket(ctx);
  const router = new zlink.RouterSocket(ctx);
  const stream = new zlink.StreamSocket(ctx);
  const compat = new zlink.Socket(ctx, zlink.SocketType.STREAM);
  const maxRoutingId = Buffer.alloc(255, 0x61);
  const overflowRoutingId = Buffer.alloc(256, 0x62);

  assert.doesNotThrow(() => dealer.setRoutingId(maxRoutingId));

  assert.throws(() => dealer.setRoutingId(overflowRoutingId), /at most 255 bytes/);
  assert.throws(() => router.send(overflowRoutingId, zlink.Message.empty()), /at most 255 bytes/);
  assert.throws(() => router.trySend(overflowRoutingId, zlink.Message.empty()), /at most 255 bytes/);
  assert.throws(() => stream.send(overflowRoutingId, zlink.Message.empty()), /at most 255 bytes/);
  assert.throws(() => compat.streamSend(overflowRoutingId, Buffer.alloc(0)), /255 bytes/);

  compat.close();
  stream.close();
  router.close();
  dealer.close();
  ctx.close();
});

test('fixed-size c-string inputs reject embedded nulls and overflow', () => {
  const ctx = new zlink.Context();
  const pair = new zlink.PairSocket(ctx);
  const registry = new zlink.Registry(ctx);
  const query = new zlink.RegistryQueryClient(ctx);
  const discovery = new zlink.Discovery(ctx, zlink.ServiceType.SPOT, 'svc');
  const node = new zlink.SpotNode(ctx);
  const spot = new zlink.Spot(node);

  assert.throws(() => pair.bind('tcp://127.0.0.1:5555\0bad'), /embedded null/);
  assert.throws(() => pair.unbind('x'.repeat(256)), /at most 255 bytes/);
  assert.throws(() => registry.bind('x'.repeat(256), 'tcp://127.0.0.1:5556'), /255 bytes/);
  assert.throws(() => query.connect('tcp://127.0.0.1:5556\0bad'), /embedded null/);
  assert.throws(() => discovery.connectRegistry('x'.repeat(256)), /255 bytes/);
  assert.throws(() => node.bind('tcp://127.0.0.1:5557\0bad'), /embedded null/);
  assert.throws(() => spot.setSubscription('topic\0bad'), /embedded null/);

  spot.close();
  node.close();
  discovery.close();
  query.close();
  registry.close();
  pair.close();
  ctx.close();
});
