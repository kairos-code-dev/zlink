'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const test = require('node:test');
const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const { once } = require('node:events');
const net = require('node:net');
const path = require('node:path');
const zlink = require('@zlink-systems/zlink');
async function reservePort() {
    const server = net.createServer();
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    const { port } = server.address();
    await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
    return port;
}
async function setPubBindOnReservedPort(node) {
    let lastError = null;
    for (let attempt = 0; attempt < 8; attempt += 1) {
        const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
        try {
            node.setPubBind(endpoint);
            return endpoint;
        }
        catch (error) {
            if (!/Address already in use/i.test(String(error?.message ?? error))) {
                throw error;
            }
            lastError = error;
        }
    }
    throw lastError;
}
async function waitFor(deadlineMs, read) {
    const deadline = Date.now() + deadlineMs;
    while (Date.now() < deadline) {
        const value = read();
        if (value) {
            return value;
        }
        await new Promise((resolve) => setImmediate(resolve));
    }
    return null;
}
test('socket monitor exposes recv and snapshot surface', () => {
    const ctx = zlink.createContext();
    const socket = zlink.createPairSocket(ctx);
    const monitor = socket.monitorOpen();
    assert.equal(typeof monitor.recv, 'function');
    assert.equal(monitor.snapshot, undefined);
    assert.equal(typeof monitor.status, 'function');
    const snapshot = monitor.status();
    assert.equal(typeof snapshot.autoHwmProfile, 'number');
    assert.equal(typeof snapshot.autoHwmPolicyClass, 'number');
    assert.equal(typeof snapshot.autoHwmUnitBudgetBytes, 'bigint');
    assert.equal(typeof snapshot.autoHwmSizeCap, 'number');
    assert.equal(typeof snapshot.autoHwmSocketMessageSlots, 'bigint');
    assert.equal(typeof snapshot.autoHwmConnectionBucketCount, 'number');
    assert.equal(typeof snapshot.autoHwmConnectionBucketHwm4K, 'number');
    monitor.close();
    socket.close();
    ctx.close();
});
test('socket monitor receives bind state events', async () => {
    const port = await reservePort();
    const endpoint = `tcp://127.0.0.1:${port}`;
    const ctx = zlink.createContext();
    const socket = zlink.createPairSocket(ctx);
    const monitor = socket.monitorOpen();
    let client;
    try {
        socket.bind(endpoint);
        client = net.createConnection({ host: '127.0.0.1', port });
        await once(client, 'connect');
        const event = monitor.recv();
        assert.equal(event.event, zlink.MonitorEventType.Listening);
    }
    finally {
        if (client) {
            client.destroy();
        }
        monitor.close();
        socket.close();
        ctx.close();
    }
});
test('socket monitor onEvent receives bind state events', async () => {
    const port = await reservePort();
    const endpoint = `tcp://127.0.0.1:${port}`;
    const ctx = zlink.createContext();
    const socket = zlink.createPairSocket(ctx);
    const monitor = socket.monitorOpen();
    let client;
    try {
        const eventPromise = new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new Error('socket monitor onEvent timeout'));
            }, 5000);
            monitor.onEvent((event) => {
                clearTimeout(timeout);
                resolve(event);
            });
        });
        socket.bind(endpoint);
        client = net.createConnection({ host: '127.0.0.1', port });
        await once(client, 'connect');
        const event = await eventPromise;
        assert.equal(event.event, zlink.MonitorEventType.Listening);
        assert.throws(() => monitor.recv(), /busy|state|current/i);
    }
    finally {
        if (client) {
            client.destroy();
        }
        monitor.close();
        socket.close();
        ctx.close();
    }
});
test('stream disconnect monitor event carries the disconnected peer routing id', async () => {
    const port = await reservePort();
    const endpoint = `tcp://127.0.0.1:${port}`;
    const ctx = zlink.createContext();
    const stream = zlink.createStreamSocket(ctx);
    const monitor = stream.monitorOpen([
        zlink.MonitorEventType.Accepted,
        zlink.MonitorEventType.Disconnected
    ]);
    const client = new net.Socket();
    try {
        stream.bind(endpoint);
        await new Promise((resolve, reject) => {
            client.once('error', reject);
            client.connect(port, '127.0.0.1', resolve);
        });
        client.write(Buffer.from('monitor-probe'));
        const received = new zlink.Received();
        const packet = await waitFor(5000, () => stream.recv(received, zlink.RecvFlags.DontWait));
        assert.ok(packet);
        received.close();
        client.end();
        await once(client, 'close');
        let disconnected;
        for (let index = 0; index < 2; index += 1) {
            const event = monitor.recv();
            if (event.event === zlink.MonitorEventType.Disconnected) {
                disconnected = event;
                break;
            }
        }
        assert.ok(disconnected);
        assert.ok(disconnected.routingId);
        assert.ok(disconnected.routingId.size > 0);
    }
    finally {
        client.destroy();
        monitor.close();
        stream.close();
        ctx.close();
    }
});
test('stream disconnect monitor callback carries the disconnected peer routing id', async () => {
    const port = await reservePort();
    const endpoint = `tcp://127.0.0.1:${port}`;
    const ctx = zlink.createContext();
    const stream = zlink.createStreamSocket(ctx);
    const monitor = stream.monitorOpen([
        zlink.MonitorEventType.Accepted,
        zlink.MonitorEventType.Disconnected
    ]);
    const client = new net.Socket();
    let disconnectedEvent = null;
    monitor.onEvent((event) => {
        if (event.event === zlink.MonitorEventType.Disconnected) {
            disconnectedEvent = event;
        }
    });
    try {
        stream.bind(endpoint);
        await new Promise((resolve, reject) => {
            client.once('error', reject);
            client.connect(port, '127.0.0.1', resolve);
        });
        client.write(Buffer.from('monitor-callback-probe'));
        const received = new zlink.Received();
        const packet = await waitFor(5000, () => stream.recv(received, zlink.RecvFlags.DontWait));
        assert.ok(packet);
        received.close();
        client.end();
        await once(client, 'close');
        const event = await waitFor(5000, () => disconnectedEvent);
        assert.ok(event);
        assert.ok(event.routingId);
        assert.ok(event.routingId.size > 0);
    }
    finally {
        client.destroy();
        monitor.close();
        stream.close();
        ctx.close();
    }
});
test('spot node status snapshot starts empty', async () => {
    const ctx = zlink.createContext();
    const node = zlink.createSpotNode(ctx);
    const spot = node.createSpot();
    await setPubBindOnReservedPort(node);
    assert.equal(node.status().connectedPeerCount, 0);
    assert.equal(node.peers().length, 0);
    assert.equal(node.subjects().length, 0);
    assert.ok(node.status().nodeRoutingId instanceof zlink.RoutingId);
    spot.close();
    node.close();
    ctx.close();
});
test('spot node subject status reflects remote sub readiness after direct peer connect', async () => {
    const ctx = zlink.createContext();
    const serverNode = zlink.createSpotNode(ctx);
    const clientNode = zlink.createSpotNode(ctx);
    const serverSpot = serverNode.createSpot();
    const clientSpot = clientNode.createSpot();
    try {
        const endpoint = await setPubBindOnReservedPort(serverNode);
        await setPubBindOnReservedPort(clientNode);
        clientNode.connectPeer(endpoint);
        clientSpot.setSubscription('topic.monitor.remote');
        const deadline = Date.now() + 5000;
        while (Date.now() < deadline) {
            if (clientNode.status().readySubjectCount === 1) {
                assert.equal(clientNode.status().connectedPeerCount, 1);
                return;
            }
            await new Promise((resolve) => setImmediate(resolve));
        }
        assert.fail(`spot remote subject ready timeout: ${JSON.stringify({
            serverStatus: serverNode.status(),
            clientStatus: clientNode.status(),
            serverPeers: serverNode.peers(),
            clientPeers: clientNode.peers(),
            serverSubjects: serverNode.subjects(),
            clientSubjects: clientNode.subjects()
        })}`);
    }
    finally {
        clientSpot.close();
        serverSpot.close();
        clientNode.close();
        serverNode.close();
        ctx.close();
    }
});
test('spot node subject status stays unready before peer connect', async () => {
    const ctx = zlink.createContext();
    const node = zlink.createSpotNode(ctx);
    const spot = node.createSpot();
    try {
        await setPubBindOnReservedPort(node);
        spot.setSubscription('topic.monitor.local-only');
        await new Promise((resolve) => setImmediate(resolve));
        assert.equal(node.status().connectedPeerCount, 0);
        assert.equal(node.status().readySubjectCount, 0);
    }
    finally {
        spot.close();
        node.close();
        ctx.close();
    }
});
