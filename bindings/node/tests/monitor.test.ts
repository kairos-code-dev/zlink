'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const { once } = require('node:events');
const net = require('node:net');
const path = require('node:path');
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
  } finally {
    if (client) {
      client.destroy();
    }
    monitor.close();
    socket.close();
    ctx.close();
  }
});

test('spot node status snapshot starts empty', () => {
  const ctx = new zlink.Context();
  const node = new zlink.SpotNode(ctx);
  const spot = new zlink.Spot(node);

  assert.equal(node.statusSnapshot().connectedPeerCount, 0);
  assert.equal(node.peersSnapshot().length, 0);
  assert.equal(node.subjectsSnapshot().length, 0);

  spot.close();
  node.close();
  ctx.close();
});

test('discovery service monitor reports service-up events', async () => {
  const ctx = new zlink.Context();
  const registry = new zlink.Registry(ctx);
  const discovery = new zlink.Discovery(ctx, zlink.ServiceType.SPOT, 'monitor-service-up');
  const node = new zlink.SpotNode(ctx);
  const pubEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const routerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const serviceEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const monitor = discovery.monitorOpen(
    zlink.ServiceMonitorEvent.DISCOVERY_SERVICE_UP
      | zlink.ServiceMonitorEvent.ERROR
  );

  try {
    registry.bind(pubEndpoint, routerEndpoint);
    discovery.connectRegistry(routerEndpoint);
    node.attachDiscovery(discovery);

    const eventPromise = new Promise((resolve, reject) => {
      const timeoutId = setTimeout(() => {
        reject(new Error('discovery service monitor timeout'));
      }, 5000);
      monitor.onEvent((event) => {
        if (event.eventType === zlink.ServiceMonitorEvent.ERROR) {
          clearTimeout(timeoutId);
          reject(new Error(`discovery monitor error: ${JSON.stringify(event)}`));
          return;
        }
        if (event.eventType === zlink.ServiceMonitorEvent.DISCOVERY_SERVICE_UP) {
          clearTimeout(timeoutId);
          resolve(event);
        }
      });
    });

    node.bind(serviceEndpoint);
    const event = await eventPromise;
    assert.equal(event.eventType, zlink.ServiceMonitorEvent.DISCOVERY_SERVICE_UP);
    assert.equal(event.serviceName, 'monitor-service-up');
  } finally {
    monitor.close();
    discovery.close();
    registry.close();
    ctx.close();
  }
});

test('spot node subject status reflects remote sub readiness after direct peer connect', async () => {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const ctx = new zlink.Context();
  const serverNode = new zlink.SpotNode(ctx);
  const clientNode = new zlink.SpotNode(ctx);
  const serverSpot = new zlink.Spot(serverNode);
  const clientSpot = new zlink.Spot(clientNode);
  try {
    serverNode.bind(endpoint);
    clientNode.connectPeer(endpoint);
    clientSpot.setSubscription('topic.monitor.remote');

    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
      if (clientNode.statusSnapshot().readySubjectCount === 1) {
        assert.equal(clientNode.statusSnapshot().connectedPeerCount, 1);
        return;
      }
      await new Promise((resolve) => setImmediate(resolve));
    }

    assert.fail(`spot remote subject ready timeout: ${JSON.stringify({
      serverStatus: serverNode.statusSnapshot(),
      clientStatus: clientNode.statusSnapshot(),
      serverPeers: serverNode.peersSnapshot(),
      clientPeers: clientNode.peersSnapshot(),
      serverSubjects: serverNode.subjectsSnapshot(),
      clientSubjects: clientNode.subjectsSnapshot()
    })}`);
  } finally {
    clientSpot.close();
    serverSpot.close();
    clientNode.close();
    serverNode.close();
    ctx.close();
  }
});

test('spot node subject status stays unready before peer connect', async () => {
  const ctx = new zlink.Context();
  const node = new zlink.SpotNode(ctx);
  const spot = new zlink.Spot(node);

  try {
    spot.setSubscription('topic.monitor.local-only');
    await new Promise((resolve) => setImmediate(resolve));
    assert.equal(node.statusSnapshot().connectedPeerCount, 0);
    assert.equal(node.statusSnapshot().readySubjectCount, 0);
  } finally {
    spot.close();
    node.close();
    ctx.close();
  }
});
