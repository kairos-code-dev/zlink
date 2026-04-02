'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist');
test('socket and service constants match aligned header values', () => {
    assert.equal(zlink.SocketType.PAIR, 0x1001);
    assert.equal(zlink.SocketType.STREAM, 0x1008);
    assert.equal(zlink.ServiceType.SPOT, 0x3002);
    assert.equal(zlink.ServiceType.SOCKET, 0x3003);
    assert.equal(zlink.ContextOption.BLOCKY, 10);
    assert.equal(zlink.SocketOption.LINGER, 0x300A);
    assert.equal(zlink.SocketOption.TLS_PASSWORD, 0x302F);
    assert.equal(zlink.SocketOption.XPUB_VERBOSE, 0x3301);
    assert.equal(zlink.SocketOption.ROUTER_MANDATORY, 0x3101);
});
test('monitor and topology constants stay frozen', () => {
    assert.ok(Object.isFrozen(zlink.MonitorEvent));
    assert.ok(Object.isFrozen(zlink.ServiceMonitorEvent));
    assert.ok(Object.isFrozen(zlink.MonitorSnapshotDetail));
    assert.ok(Object.isFrozen(zlink.SpotNodeState));
    assert.ok(Object.isFrozen(zlink.SpotPeerSource));
    assert.ok(Object.isFrozen(zlink.SpotPeerState));
    assert.ok(Object.isFrozen(zlink.RegistryState));
    assert.ok(Object.isFrozen(zlink.TopologySource));
    assert.ok(Object.isFrozen(zlink.TopologyState));
});
