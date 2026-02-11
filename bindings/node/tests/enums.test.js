'use strict';

const test = require('node:test');
const assert = require('node:assert');
const zlink = require('../src/index');

test('SocketType values match C defines', () => {
  assert.strictEqual(zlink.SocketType.PAIR, 0);
  assert.strictEqual(zlink.SocketType.PUB, 1);
  assert.strictEqual(zlink.SocketType.SUB, 2);
  assert.strictEqual(zlink.SocketType.DEALER, 5);
  assert.strictEqual(zlink.SocketType.ROUTER, 6);
  assert.strictEqual(zlink.SocketType.XPUB, 9);
  assert.strictEqual(zlink.SocketType.XSUB, 10);
  assert.strictEqual(zlink.SocketType.STREAM, 11);
});

test('ContextOption values match C defines', () => {
  assert.strictEqual(zlink.ContextOption.IO_THREADS, 1);
  assert.strictEqual(zlink.ContextOption.MAX_SOCKETS, 2);
  assert.strictEqual(zlink.ContextOption.THREAD_NAME_PREFIX, 9);
});

test('SocketOption values match C defines', () => {
  assert.strictEqual(zlink.SocketOption.LINGER, 17);
  assert.strictEqual(zlink.SocketOption.SNDHWM, 23);
  assert.strictEqual(zlink.SocketOption.RCVHWM, 24);
  assert.strictEqual(zlink.SocketOption.TLS_CERT, 95);
  assert.strictEqual(zlink.SocketOption.TLS_PASSWORD, 102);
  assert.strictEqual(zlink.SocketOption.ZMP_METADATA, 117);
});

test('SendFlag values match C defines', () => {
  assert.strictEqual(zlink.SendFlag.NONE, 0);
  assert.strictEqual(zlink.SendFlag.DONTWAIT, 1);
  assert.strictEqual(zlink.SendFlag.SNDMORE, 2);
});

test('ReceiveFlag values match C defines', () => {
  assert.strictEqual(zlink.ReceiveFlag.NONE, 0);
  assert.strictEqual(zlink.ReceiveFlag.DONTWAIT, 1);
});

test('MonitorEvent values match C defines', () => {
  assert.strictEqual(zlink.MonitorEvent.CONNECTED, 0x0001);
  assert.strictEqual(zlink.MonitorEvent.DISCONNECTED, 0x0200);
  assert.strictEqual(zlink.MonitorEvent.ALL, 0xFFFF);
});

test('DisconnectReason values match C defines', () => {
  assert.strictEqual(zlink.DisconnectReason.UNKNOWN, 0);
  assert.strictEqual(zlink.DisconnectReason.CTX_TERM, 5);
});

test('PollEvent values match C defines', () => {
  assert.strictEqual(zlink.PollEvent.POLLIN, 1);
  assert.strictEqual(zlink.PollEvent.POLLOUT, 2);
  assert.strictEqual(zlink.PollEvent.POLLERR, 4);
  assert.strictEqual(zlink.PollEvent.POLLPRI, 8);
});

test('ServiceType values match C defines', () => {
  assert.strictEqual(zlink.ServiceType.GATEWAY, 1);
  assert.strictEqual(zlink.ServiceType.SPOT, 2);
});

test('GatewayLbStrategy values match C defines', () => {
  assert.strictEqual(zlink.GatewayLbStrategy.ROUND_ROBIN, 0);
  assert.strictEqual(zlink.GatewayLbStrategy.WEIGHTED, 1);
});

test('socket role values match C defines', () => {
  assert.strictEqual(zlink.RegistrySocketRole.PUB, 1);
  assert.strictEqual(zlink.RegistrySocketRole.ROUTER, 2);
  assert.strictEqual(zlink.RegistrySocketRole.PEER_SUB, 3);
  assert.strictEqual(zlink.DiscoverySocketRole.SUB, 1);
  assert.strictEqual(zlink.GatewaySocketRole.ROUTER, 1);
  assert.strictEqual(zlink.ReceiverSocketRole.ROUTER, 1);
  assert.strictEqual(zlink.ReceiverSocketRole.DEALER, 2);
  assert.strictEqual(zlink.SpotNodeSocketRole.PUB, 1);
  assert.strictEqual(zlink.SpotNodeSocketRole.SUB, 2);
  assert.strictEqual(zlink.SpotNodeSocketRole.DEALER, 3);
  assert.strictEqual(zlink.SpotSocketRole.PUB, 1);
  assert.strictEqual(zlink.SpotSocketRole.SUB, 2);
});

test('constant objects are frozen', () => {
  assert.ok(Object.isFrozen(zlink.SocketType));
  assert.ok(Object.isFrozen(zlink.SocketOption));
  assert.ok(Object.isFrozen(zlink.SendFlag));
  assert.ok(Object.isFrozen(zlink.MonitorEvent));
  assert.ok(Object.isFrozen(zlink.PollEvent));
  assert.ok(Object.isFrozen(zlink.ServiceType));
  assert.ok(Object.isFrozen(zlink.GatewayLbStrategy));
  assert.ok(Object.isFrozen(zlink.RegistrySocketRole));
  assert.ok(Object.isFrozen(zlink.DisconnectReason));
  assert.ok(Object.isFrozen(zlink.ContextOption));
  assert.ok(Object.isFrozen(zlink.ReceiveFlag));
  assert.ok(Object.isFrozen(zlink.DiscoverySocketRole));
  assert.ok(Object.isFrozen(zlink.GatewaySocketRole));
  assert.ok(Object.isFrozen(zlink.ReceiverSocketRole));
  assert.ok(Object.isFrozen(zlink.SpotNodeSocketRole));
  assert.ok(Object.isFrozen(zlink.SpotSocketRole));
});

test('flag bitwise OR works', () => {
  const flags = zlink.SendFlag.DONTWAIT | zlink.SendFlag.SNDMORE;
  assert.strictEqual(flags, 3);

  const events = zlink.MonitorEvent.CONNECTED | zlink.MonitorEvent.DISCONNECTED;
  assert.strictEqual(events, 0x0201);

  const poll = zlink.PollEvent.POLLIN | zlink.PollEvent.POLLOUT;
  assert.strictEqual(poll, 3);
});
