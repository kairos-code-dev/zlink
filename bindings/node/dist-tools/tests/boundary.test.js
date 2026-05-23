'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('@zlink-systems/zlink');
const AUTO_CONNECT_SPOT_MESH = 5;
test('routing id accepts 255-byte maximum and rejects overflow', () => {
    const ctx = new zlink.Context();
    const dealer = new zlink.DealerSocket(ctx);
    const router = new zlink.RouterSocket(ctx);
    const stream = new zlink.StreamSocket(ctx);
    const maxRoutingId = zlink.RoutingId.fromBytes(Buffer.alloc(255, 0x61));
    const overflowRoutingId = Buffer.alloc(256, 0x62);
    assert.doesNotThrow(() => dealer.setRoutingId(maxRoutingId));
    assert.throws(() => zlink.RoutingId.fromBytes(overflowRoutingId), /1\.\.255 bytes/);
    assert.doesNotThrow(() => zlink.RoutingId.fromBytes(Buffer.alloc(1, 0x63)));
    assert.ok(zlink.RoutingId.fromString('004142').equals(zlink.RoutingId.fromBytes(Buffer.from([0, 0x41, 0x42]))));
    assert.equal(zlink.RoutingId.fromString('a'.repeat(510)).size, 255);
    assert.throws(() => zlink.RoutingId.fromString('not-hex'), /hex string/);
    assert.throws(() => zlink.RoutingId.fromString('a'.repeat(512)), /255 bytes/);
    assert.throws(() => router.send(overflowRoutingId).message(Buffer.alloc(0)).submit(), /RoutingId/);
    assert.throws(() => stream.send(overflowRoutingId).message(Buffer.alloc(0)).submit(), /RoutingId/);
    stream.close();
    router.close();
    dealer.close();
    ctx.close();
});
test('stream attach actor gateway requires routed node', () => {
    const ctx = new zlink.Context();
    const stream = new zlink.StreamSocket(ctx);
    const node = new zlink.SpotNode(ctx, zlink.SpotNodeMode.PubSub);
    assert.throws(() => stream.attachActorGateway(node), /not supported/i);
    node.close();
    stream.close();
    ctx.close();
});
test('fixed-size c-string inputs reject embedded nulls and overflow', () => {
    const ctx = new zlink.Context();
    const pair = new zlink.PairSocket(ctx);
    const registry = new zlink.Registry(ctx);
    const query = new zlink.RegistryQueryClient(ctx);
    const discovery = new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, 'svc');
    const node = new zlink.SpotNode(ctx);
    const spot = node.createSpot();
    const maxChannelName = 's'.repeat(255);
    assert.throws(() => pair.bind('tcp://127.0.0.1:5555\0bad'), /embedded null/);
    assert.throws(() => pair.unbind('x'.repeat(256)), /at most 255 bytes/);
    assert.throws(() => registry.bind('x'.repeat(256), 'tcp://127.0.0.1:5556'), /255 bytes/);
    assert.throws(() => query.connect('tcp://127.0.0.1:5556\0bad'), /embedded null/);
    assert.throws(() => discovery.connectRegistry('x'.repeat(256)), /255 bytes/);
    assert.throws(() => node.setPubBind('tcp://127.0.0.1:5557\0bad'), /embedded null/);
    assert.throws(() => spot.setSubscription('topic\0bad'), /embedded null/);
    assert.doesNotThrow(() => new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, maxChannelName).close());
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
