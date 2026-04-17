'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist/canonical');
const SERVICE_TYPE_SPOT = 0x3002;
test('routing id accepts 255-byte maximum and rejects overflow', () => {
    const ctx = new zlink.Context();
    const dealer = new zlink.DealerSocket(ctx);
    const router = new zlink.RouterSocket(ctx);
    const stream = new zlink.StreamSocket(ctx);
    const maxRoutingId = zlink.RoutingId.fromBytes(Buffer.alloc(255, 0x61));
    const overflowRoutingId = Buffer.alloc(256, 0x62);
    assert.doesNotThrow(() => dealer.setRoutingId(maxRoutingId));
    assert.throws(() => zlink.RoutingId.fromBytes(overflowRoutingId), /0\.\.255 bytes/);
    assert.doesNotThrow(() => zlink.RoutingId.fromBytes(Buffer.alloc(1, 0x63)));
    assert.throws(() => router.send(overflowRoutingId, Buffer.alloc(0)), /RoutingId/);
    assert.throws(() => stream.send(overflowRoutingId, Buffer.alloc(0)), /RoutingId/);
    stream.close();
    router.close();
    dealer.close();
    ctx.close();
});
test('routing id helper conversions keep bytes canonical', () => {
    const streamRid = zlink.RoutingId.fromU32(0x01020304);
    assert.deepEqual(streamRid.toBytes(), Buffer.from([0x01, 0x02, 0x03, 0x04]));
    assert.equal(streamRid.toU32(), 0x01020304);
    const textRid = zlink.RoutingId.fromText('router-a');
    assert.equal(textRid.toText(), 'router-a');
    assert.equal(textRid.toHex(), '726f757465722d61');
    assert.throws(() => zlink.RoutingId.fromBytes(Buffer.from([0xc3, 0x28])).toText(), /encoded data/);
});
test('fixed-size c-string inputs reject embedded nulls and overflow', () => {
    const ctx = new zlink.Context();
    const pair = new zlink.PairSocket(ctx);
    const registry = new zlink.Registry(ctx);
    const query = new zlink.RegistryQueryClient(ctx);
    const discovery = new zlink.Discovery(ctx, SERVICE_TYPE_SPOT, 'svc');
    const node = new zlink.SpotNode(ctx);
    const spot = node.createSpot();
    const maxServiceName = 's'.repeat(255);
    assert.throws(() => pair.bind('tcp://127.0.0.1:5555\0bad'), /embedded null/);
    assert.throws(() => pair.unbind('x'.repeat(256)), /at most 255 bytes/);
    assert.throws(() => registry.bind('x'.repeat(256), 'tcp://127.0.0.1:5556'), /255 bytes/);
    assert.throws(() => query.connect('tcp://127.0.0.1:5556\0bad'), /embedded null/);
    assert.throws(() => discovery.connectRegistry('x'.repeat(256)), /255 bytes/);
    assert.throws(() => node.bind('tcp://127.0.0.1:5557\0bad'), /embedded null/);
    assert.throws(() => spot.setSubscription('topic\0bad'), /embedded null/);
    assert.doesNotThrow(() => new zlink.Discovery(ctx, SERVICE_TYPE_SPOT, maxServiceName).close());
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
