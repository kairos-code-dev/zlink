'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('@zlink-systems/zlink');

test('routing id accepts 255-byte maximum and rejects overflow', () => {
  const ctx = zlink.createContext();
  const dealer = zlink.createDealerSocket(ctx);
  const router = zlink.createRouterSocket(ctx);
  const stream = zlink.createStreamSocket(ctx);
  const maxRoutingId = zlink.RoutingId.from(Buffer.alloc(255, 0x61));
  const overflowRoutingId = Buffer.alloc(256, 0x62);

  assert.doesNotThrow(() => dealer.setRoutingId(maxRoutingId));

  assert.throws(() => zlink.RoutingId.from(overflowRoutingId), /1\.\.255 bytes/);
  assert.doesNotThrow(() => zlink.RoutingId.from(Buffer.alloc(1, 0x63)));
  assert.ok(zlink.RoutingId.fromHex('004142').equals(zlink.RoutingId.from(Buffer.from([0, 0x41, 0x42]))));
  assert.equal(zlink.RoutingId.fromHex('a'.repeat(510)).size, 255);
  assert.throws(() => zlink.RoutingId.fromHex('not-hex'), /hex string/);
  assert.throws(() => zlink.RoutingId.fromHex('a'.repeat(512)), /255 bytes/);
  assert.equal(zlink.RoutingId.from('dealer-1').toString(), 'dealer-1');
  assert.equal(zlink.RoutingId.from(23).toString(), '23');
  assert.equal(zlink.RoutingId.fromHex('004142').toString(), 'hex:004142');
  assert.throws(() => router.send(overflowRoutingId).message(Buffer.alloc(0)).submit(), /RoutingId/);
  assert.throws(() => stream.send(overflowRoutingId).message(Buffer.alloc(0)).submit(), /RoutingId/);

  stream.close();
  router.close();
  dealer.close();
  ctx.close();
});

test('fixed-size c-string inputs reject embedded nulls and overflow', () => {
  const ctx = zlink.createContext();
  const pair = zlink.createPairSocket(ctx);
  const node = zlink.createSpotNode(ctx);
  const spot = node.createSpot();

  assert.throws(() => pair.bind('tcp://127.0.0.1:5555\0bad'), /embedded null/);
  assert.throws(() => pair.unbind('x'.repeat(256)), /at most 255 bytes/);
  assert.throws(() => node.setPubBind('tcp://127.0.0.1:5557\0bad'), /embedded null/);
  assert.throws(() => spot.setSubscription('topic\0bad'), /embedded null/);

  spot.close();
  node.close();
  pair.close();
  ctx.close();
});

test('typed numeric options fail fast on int32 and int64 boundary violations', () => {
  const ctx = zlink.createContext();
  const pair = zlink.createPairSocket(ctx);

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
    Reflect.set(pair.options, 'maxMsgSize', Number.MAX_SAFE_INTEGER + 1);
  }, /bigint/);
  assert.throws(() => {
    pair.options.maxMsgSize = 1n << 63n;
  }, /fit in int64/);
  assert.doesNotThrow(() => {
    pair.options.maxMsgSize = (1n << 63n) - 1n;
  });

  pair.close();
  ctx.close();
});
