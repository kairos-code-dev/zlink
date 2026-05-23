'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const { once } = require('node:events');
const net = require('node:net');
const path = require('node:path');
const zlink = require('@zlink-systems/zlink');

const AUTO_CONNECT_SPOT_MESH = 5;
async function reservePort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const { port } = server.address();
  await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return port;
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
  const ctx = new zlink.Context();
  const socket = new zlink.PairSocket(ctx);
  const monitor = socket.monitorOpen();

  assert.equal(typeof monitor.recv, 'function');
  assert.equal(typeof monitor.snapshot, 'function');
  const snapshot = monitor.snapshot();
  assert.equal(typeof snapshot.autoHwmProfile, 'number');
  assert.equal(typeof snapshot.autoHwmPolicyClass, 'number');
  assert.equal(typeof snapshot.autoHwmUnitBudgetBytes, 'bigint');
  assert.equal(typeof snapshot.autoHwmSizeCap, 'number');
  assert.equal(typeof snapshot.autoHwmSocketMessageSlots, 'bigint');

  monitor.close();
  socket.close();
  ctx.close();
});

test('socket monitor receives bind state events', async () => {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const ctx = new zlink.Context();
  const socket = new zlink.PairSocket(ctx);
  const monitor = socket.monitorOpen();
  let client;

  try {
    socket.bind(endpoint);
    client = net.createConnection({ host: '127.0.0.1', port });
    await once(client, 'connect');

    const event = monitor.recv();
    assert.equal(event.event, zlink.MonitorEventType.Listening);
  } finally {
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
  const ctx = new zlink.Context();
  const socket = new zlink.PairSocket(ctx);
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
  } finally {
    if (client) {
      client.destroy();
    }
    monitor.close();
    socket.close();
    ctx.close();
  }
});

test('spot node status snapshot starts empty', async () => {
  const ctx = new zlink.Context();
  const node = new zlink.SpotNode(ctx);
  const spot = node.createSpot();
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;

  node.setPubBind(endpoint);

  assert.equal(node.statusSnapshot().connectedPeerCount, 0);
  assert.equal(node.peersSnapshot().length, 0);
  assert.equal(node.subjectsSnapshot().length, 0);
  assert.ok(node.statusSnapshot().nodeRoutingId instanceof zlink.RoutingId);

  spot.close();
  node.close();
  ctx.close();
});

test('discovery member peers reflect service-up state', async () => {
  const ctx = new zlink.Context();
  const registry = new zlink.Registry(ctx);
  const providerDiscovery = new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, 'monitor-service-up');
  const watcherDiscovery = new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, 'monitor-service-up');
  const node = new zlink.SpotNode(ctx);
  const pubEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const routerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const serviceEndpoint = `tcp://127.0.0.1:${await reservePort()}`;

  try {
    registry.bind(pubEndpoint, routerEndpoint);
    providerDiscovery.connectRegistry(routerEndpoint);
    watcherDiscovery.connectRegistry(routerEndpoint);
    node.attachDiscovery(providerDiscovery);
    node.setPubBind(serviceEndpoint);

    const peer = await waitFor(5000, () =>
      watcherDiscovery.memberPeers().find((entry) =>
        entry.channelName === 'monitor-service-up' && entry.endpoint.length > 0));
    assert.ok(peer);
  } finally {
    watcherDiscovery.close();
    providerDiscovery.close();
    registry.close();
    ctx.close();
  }
});

test('spot node subject status reflects remote sub readiness after direct peer connect', async () => {
  const port = await reservePort();
  const clientPort = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const clientEndpoint = `tcp://127.0.0.1:${clientPort}`;
  const ctx = new zlink.Context();
  const serverNode = new zlink.SpotNode(ctx);
  const clientNode = new zlink.SpotNode(ctx);
  const serverSpot = serverNode.createSpot();
  const clientSpot = clientNode.createSpot();
  try {
    serverNode.setPubBind(endpoint);
    clientNode.setPubBind(clientEndpoint);
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
  const spot = node.createSpot();
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;

  try {
    node.setPubBind(endpoint);
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
