'use strict';

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

test('service objects expose aligned monitor and query surface', () => {
  const ctx = new zlink.Context();
  const registry = new zlink.Registry(ctx);
  const discovery = new zlink.Discovery(ctx, zlink.ServiceType.SPOT, 'svc');
  const node = new zlink.SpotNode(ctx);
  const spot = new zlink.Spot(node);
  const query = new zlink.RegistryQueryClient(ctx);

  registry.bind('inproc://registry-pub', 'inproc://registry-router');
  query.connect('inproc://registry-router');
  discovery.setValue(7);
  discovery.setMetadata(Buffer.from('meta'));

  const discoveryMonitor = discovery.monitorOpen();

  assert.equal(registry.statusSnapshot().topologyEntryCount, 0);
  assert.deepEqual(registry.serviceSummarySnapshot(), []);
  assert.deepEqual(registry.topologySnapshot(), []);
  assert.deepEqual(registry.topologyQuery(), []);
  assert.deepEqual(query.snapshot(), []);
  assert.equal(discovery.getValue(), 7);
  assert.equal(discovery.getMetadata().toString(), 'meta');
  assert.deepEqual(discovery.memberPeers(), []);
  assert.equal(typeof discovery.memberPeerMetadata, 'function');
  assert.equal(node.peersSnapshot().length, 0);
  assert.equal(node.peersQuery().length, 0);
  assert.equal(node.subjectsSnapshot().length, 0);
  assert.equal(zlink.SpotNodeOption, undefined);
  assert.equal(zlink.SpotNodePubMode, undefined);
  assert.equal(zlink.SpotNodePubQueueFullPolicy, undefined);
  assert.equal(typeof discoveryMonitor.recv, 'function');
  assert.equal(typeof discoveryMonitor.tryRecv, 'function');
  assert.equal(typeof discoveryMonitor.onEvent, 'function');
  assert.equal(registry.setEndpoints, undefined);
  assert.equal(registry.start, undefined);
  assert.equal(registry.setSockOpt, undefined);
  assert.equal(node.setDiscovery, undefined);
  assert.equal(node.register, undefined);
  assert.equal(node.unregister, undefined);
  assert.equal(node.pubSocket, undefined);
  assert.equal(node.subSocket, undefined);
  assert.equal(node.pubPeers, undefined);
  assert.equal(node.subPeers, undefined);
  assert.throws(() => registry.bind('inproc://registry-pub-2', 'inproc://registry-router-2'), /only be called once/);

  discoveryMonitor.close();
  query.close();
  spot.close();
  node.close();
  discovery.close();
  registry.close();
  ctx.close();
});

test('removed receiver stays removed from aligned api', () => {
  assert.equal('Receiver' in zlink, false);
  assert.equal(zlink.Receiver, undefined);
});

test('spot close leaves spot node alive and discovery close tears down attached participants', () => {
  const ctx = new zlink.Context();
  const standaloneNode = new zlink.SpotNode(ctx);
  const spot = new zlink.Spot(standaloneNode);
  const discovery = new zlink.Discovery(ctx, zlink.ServiceType.SPOT, 'participant-close');
  const attachedNode = new zlink.SpotNode(ctx);

  attachedNode.attachDiscovery(discovery);
  spot.close();

  assert.equal(standaloneNode.statusSnapshot().configuredPeerCount, 0);

  discovery.close();
  assert.throws(() => attachedNode.statusSnapshot(), /shutdown|closed|transport/i);

  standaloneNode.close();
  ctx.close();
});

test('discovery requires a service name in the aligned api', () => {
  const ctx = new zlink.Context();
  assert.throws(() => new zlink.Discovery(ctx, zlink.ServiceType.SPOT), /serviceName/);
  assert.throws(() => new zlink.Discovery(ctx, zlink.ServiceType.SPOT, ''), /serviceName/);
  assert.throws(() => new zlink.Discovery(ctx, zlink.ServiceType.SPOT, 'x'.repeat(256)), /255 bytes/);
  ctx.close();
});

test('registry, discovery, and query client expose canonical service discovery flow', async () => {
  const ctx = new zlink.Context();
  const registry = new zlink.Registry(ctx);
  const query = new zlink.RegistryQueryClient(ctx);
  const providerDiscovery = new zlink.Discovery(ctx, zlink.ServiceType.SPOT, 'service-found');
  const watcherDiscovery = new zlink.Discovery(ctx, zlink.ServiceType.SPOT, 'service-found');
  const node = new zlink.SpotNode(ctx);
  const pubPort = await reservePort();
  const routerPort = await reservePort();
  const servicePort = await reservePort();
  const pubEndpoint = `tcp://127.0.0.1:${pubPort}`;
  const routerEndpoint = `tcp://127.0.0.1:${routerPort}`;
  const serviceEndpoint = `tcp://127.0.0.1:${servicePort}`;

  try {
    registry.bind(pubEndpoint, routerEndpoint);
    query.connect(routerEndpoint);
    providerDiscovery.connectRegistry(routerEndpoint);
    watcherDiscovery.connectRegistry(routerEndpoint);
    providerDiscovery.setMetadata(Buffer.from('peer-meta'));
    node.attachDiscovery(providerDiscovery);
    node.bind(serviceEndpoint);

    const topologyEntry = await waitFor(5000, () => (
      registry.topologySnapshot().find((entry) => (
        entry.serviceName === 'service-found' && entry.endpoint === serviceEndpoint
      )) ?? null
    ));
    assert.ok(topologyEntry);
    assert.equal(topologyEntry.endpoint, serviceEndpoint);

    const serviceSummary = await waitFor(5000, () => {
      const [entry] = registry.serviceSummarySnapshot({
        serviceKind: zlink.ServiceKind.SPOT_PUB,
        serviceName: 'service-found'
      });
      return entry ?? null;
    });
    assert.ok(serviceSummary);
    assert.equal(serviceSummary.serviceName, 'service-found');

    const queryEntry = await waitFor(5000, () => (
      query.snapshot({ serviceName: 'service-found' }).find((entry) => (
        entry.endpoint === serviceEndpoint
      )) ?? null
    ));
    assert.ok(queryEntry);
    assert.equal(queryEntry.endpoint, serviceEndpoint);

    const peers = registry.memberPeers(zlink.ServiceType.SPOT, 'service-found');
    assert.ok(Array.isArray(peers));
    assert.ok(peers.some((peer) => peer.endpoint === serviceEndpoint));
    assert.ok(Array.isArray(node.subjectsSnapshot({ subject: '' })));

    const discoveryPeer = await waitFor(5000, () => (
      watcherDiscovery.memberPeers().find((peer) => peer.endpoint === serviceEndpoint) ?? null
    ));
    assert.ok(discoveryPeer);

    const registryMetadata = await waitFor(5000, () => {
      try {
        return registry.memberPeerMetadata(
          zlink.ServiceType.SPOT,
          'service-found',
          zlink.ServiceRole.SPOT,
          serviceEndpoint
        );
      } catch (_) {
        return null;
      }
    });
    assert.ok(Buffer.isBuffer(registryMetadata));
    assert.equal(registryMetadata.toString(), 'peer-meta');

    const discoveryMetadata = await waitFor(5000, () => {
      try {
        return watcherDiscovery.memberPeerMetadata(
          discoveryPeer.serviceRole,
          discoveryPeer.endpoint
        );
      } catch (_) {
        return null;
      }
    });
    assert.ok(Buffer.isBuffer(discoveryMetadata));
    assert.equal(discoveryMetadata.toString(), 'peer-meta');
  } finally {
    watcherDiscovery.close();
    providerDiscovery.close();
    query.close();
    registry.close();
    ctx.close();
  }
});

test('attachDiscovery blocks manual lifecycle entry points on canonical sockets and spot nodes', () => {
  const ctx = new zlink.Context();
  const socketDiscovery = new zlink.Discovery(ctx, zlink.ServiceType.SOCKET, 'attached-socket');
  const spotDiscovery = new zlink.Discovery(ctx, zlink.ServiceType.SPOT, 'attached-spot');
  const pub = new zlink.PubSocket(ctx);
  const node = new zlink.SpotNode(ctx);

  try {
    pub.attachDiscovery(socketDiscovery);
    node.attachDiscovery(spotDiscovery);

    assert.throws(() => pub.connect('tcp://127.0.0.1:32001'), /busy|state|current|discovery/i);
    assert.throws(() => pub.disconnect('tcp://127.0.0.1:32001'), /busy|state|current|discovery/i);
    assert.throws(() => pub.unbind('tcp://127.0.0.1:32001'), /busy|state|current|discovery/i);
    assert.throws(() => pub.close(), /busy|state|current|discovery/i);
    assert.throws(() => node.connectPeer('tcp://127.0.0.1:32002'), /busy|state|current|discovery/i);
    assert.throws(() => node.disconnectPeer('tcp://127.0.0.1:32002'), /busy|state|current|discovery/i);
  } finally {
    spotDiscovery.close();
    socketDiscovery.close();
    ctx.close();
  }
});

test('canonical socket options are exposed through typed facades', () => {
  const ctx = new zlink.Context();
  const pair = new zlink.PairSocket(ctx);
  const dealer = new zlink.DealerSocket(ctx);
  const router = new zlink.RouterSocket(ctx);
  const stream = new zlink.StreamSocket(ctx);
  const xpub = new zlink.XPubSocket(ctx);
  const sub = new zlink.SubSocket(ctx);

  pair.options.linger = 0;
  pair.options.sendHwm = 7;
  pair.options.recvTimeout = 13;
  pair.options.immediate = true;
  pair.options.connectTimeout = 17;
  pair.options.ipv6 = false;
  pair.options.tcpNoDelay = true;
  pair.options.tcpKeepalive = 1;
  pair.options.heartbeatInterval = 100;
  pair.options.heartbeatTtl = 200;
  pair.options.heartbeatTimeout = 300;
  pair.options.maxMsgSize = 4096n;
  pair.options.backlog = 9;
  pair.options.reconnectInterval = 21;
  pair.options.reconnectIntervalMax = 34;

  dealer.options.probe = true;
  router.options.mandatory = true;
  router.options.handover = true;
  router.options.probe = true;
  router.options.connectRoutingId = Buffer.from('route');
  stream.options.notify = true;
  xpub.options.verbose = true;
  xpub.options.verboser = true;
  xpub.options.noDrop = true;
  xpub.options.manual = true;
  sub.setSubscription('topic');

  assert.equal(pair.options.linger, 0);
  assert.equal(pair.options.sendHwm, 7);
  assert.equal(pair.options.recvTimeout, 13);
  assert.equal(pair.options.immediate, true);
  assert.equal(pair.options.connectTimeout, 17);
  assert.equal(pair.options.ipv6, false);
  assert.equal(pair.options.tcpNoDelay, true);
  assert.equal(pair.options.tcpKeepalive, 1);
  assert.equal(pair.options.heartbeatInterval, 100);
  assert.equal(pair.options.heartbeatTtl, 200);
  assert.equal(pair.options.heartbeatTimeout, 300);
  assert.equal(pair.options.maxMsgSize, 4096n);
  assert.equal(pair.options.backlog, 9);
  assert.equal(pair.options.reconnectInterval, 21);
  assert.equal(pair.options.reconnectIntervalMax, 34);
  assert.equal(router.options.mandatory, true);
  assert.equal(router.options.handover, true);
  assert.equal(router.options.probe, true);
  assert.equal(stream.options.notify, true);
  assert.equal(typeof sub.options.topicsCount, 'number');

  sub.close();
  xpub.close();
  stream.close();
  router.close();
  dealer.close();
  pair.close();
  ctx.close();
});
