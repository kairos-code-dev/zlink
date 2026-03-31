'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const test = require('node:test');
const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../dist');
async function reservePort() {
    const server = net.createServer();
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    const { port } = server.address();
    await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
    return port;
}
test('socket monitor exposes recv and tryRecv with empty path', () => {
    const ctx = new zlink.Context();
    const socket = new zlink.PairSocket(ctx);
    const monitor = socket.monitorOpen(zlink.MonitorEvent.ALL);
    assert.equal(monitor.tryRecv(), null);
    monitor.close();
    socket.close();
    ctx.close();
});
test('socket monitor receives bind state events', async () => {
    const port = await reservePort();
    const endpoint = `tcp://127.0.0.1:${port}`;
    const ctx = new zlink.Context();
    const socket = new zlink.PairSocket(ctx);
    const monitor = socket.monitorOpen(zlink.MonitorEvent.ALL);
    let client;
    try {
        socket.bind(endpoint);
        client = net.createConnection({ host: '127.0.0.1', port });
        await once(client, 'connect');
        const event = monitor.recv();
        assert.equal(event.event, zlink.MonitorEvent.LISTENING);
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
test('service monitors expose tryRecv empty path', () => {
    const ctx = new zlink.Context();
    const node = new zlink.SpotNode(ctx);
    const spot = new zlink.Spot(node);
    const monitor = spot.openMonitor();
    assert.equal(typeof monitor.recv, 'function');
    assert.equal(monitor.tryRecv(), null);
    monitor.close();
    spot.close();
    node.close();
    ctx.close();
});
