'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist');

test('routing id accepts 255-byte maximum and rejects overflow', () => {
  const ctx = new zlink.Context();
  const dealer = new zlink.DealerSocket(ctx);
  const router = new zlink.RouterSocket(ctx);
  const stream = new zlink.StreamSocket(ctx);
  const maxRoutingId = Buffer.alloc(255, 0x61);
  const overflowRoutingId = Buffer.alloc(256, 0x62);

  assert.doesNotThrow(() => dealer.setRoutingId(maxRoutingId));

  assert.throws(() => dealer.setRoutingId(overflowRoutingId), /at most 255 bytes/);
  assert.throws(() => router.send(overflowRoutingId, Buffer.alloc(0)), /at most 255 bytes/);
  assert.throws(() => router.trySend(overflowRoutingId, Buffer.alloc(0)), /at most 255 bytes/);
  assert.throws(() => stream.send(overflowRoutingId, Buffer.alloc(0)), /at most 255 bytes/);

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
  const maxServiceName = 's'.repeat(255);

  assert.throws(() => pair.bind('tcp://127.0.0.1:5555\0bad'), /embedded null/);
  assert.throws(() => pair.unbind('x'.repeat(256)), /at most 255 bytes/);
  assert.throws(() => registry.bind('x'.repeat(256), 'tcp://127.0.0.1:5556'), /255 bytes/);
  assert.throws(() => query.connect('tcp://127.0.0.1:5556\0bad'), /embedded null/);
  assert.throws(() => discovery.connectRegistry('x'.repeat(256)), /255 bytes/);
  assert.throws(() => node.bind('tcp://127.0.0.1:5557\0bad'), /embedded null/);
  assert.throws(() => spot.setSubscription('topic\0bad'), /embedded null/);
  assert.doesNotThrow(() => new zlink.Discovery(ctx, zlink.ServiceType.SPOT, maxServiceName).close());

  spot.close();
  node.close();
  discovery.close();
  query.close();
  registry.close();
  pair.close();
  ctx.close();
});

test('typed numeric options fail fast on int32 and int64 boundary violations', () => {
  const ctx = new zlink.Context();
  const pair = new zlink.PairSocket(ctx);

  assert.throws(() => {
    pair.options.linger = 2147483648;
  }, /fit in int32/);
  assert.throws(() => {
    pair.options.recvTimeout = -2147483649;
  }, /fit in int32/);
  assert.throws(() => {
    pair.options.connectTimeout = 1.5;
  }, /must be an integer/);
  assert.throws(() => {
    pair.options.maxMsgSize = Number.MAX_SAFE_INTEGER + 1;
  }, /safe integer/);
  assert.throws(() => {
    pair.options.maxMsgSize = 1n << 63n;
  }, /fit in int64/);
  assert.doesNotThrow(() => {
    pair.options.maxMsgSize = (1n << 63n) - 1n;
  });

  pair.close();
  ctx.close();
});
