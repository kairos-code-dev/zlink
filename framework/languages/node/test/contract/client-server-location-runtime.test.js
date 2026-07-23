const assert = require('node:assert/strict');
const test = require('node:test');
const zlink = require('@zlink-systems/zlink');
const framework = require('../../packages/framework/dist');
const internal = require('../../packages/framework/dist/internal');
const {
  ZLinkChannelSocketRegistry
} = require('../../packages/framework/dist/runtime/channels/channel-socket-registry');
const {
  ZLinkChannelReceiveLoop
} = require('../../packages/framework/dist/runtime/channels/channel-receive-loops');
const clientServerWire = require(
  '../../packages/framework/dist/runtime/channels/client-server-service-wire'
);

function descriptor(owner, overrides = {}) {
  return {
    channelName: 'orders',
    serverRid: 'server-a',
    lifecycleGeneration: 7n,
    descriptorRevision: 1n,
    endpoint: 'tcp://10.0.0.1:9401',
    weight: 100,
    state: framework.ZLinkFrameworkRuntimeState.Serving,
    securityIdentity: 'cluster-a',
    ownerId: owner.token.ownerId,
    leaseGeneration: owner.token.leaseGeneration,
    updatedAt: new Date(0),
    ...overrides
  };
}

function stores(store) {
  return {
    locationStore: store,
    clientServerStore: store,
    peerStore: store,
    spotStore: store,
    actorStore: store,
    routeStore: store,
    ownerLeaseStore: store
  };
}

test('dedicated ClientServer descriptor store fences immutable identity and mutable revision', async () => {
  const store = new internal.ZLinkInMemoryLocationStore(
    () => new Date(Date.UTC(2026, 6, 23, 0, 0, 0))
  );
  const owner = await store.claimOwnerLease('server-owner', 30_000);
  assert.equal(owner.kind, 'claimed');

  const claimed = await store.updateClientServer(
    descriptor(owner),
    framework.ZLinkLocationWriteIntent.NewClaim
  );
  assert.equal(claimed.status, framework.ZLinkLocationWriteStatus.Stored);

  const stale = await store.updateClientServer(
    descriptor(owner, { descriptorRevision: 1n, weight: 50 }),
    framework.ZLinkLocationWriteIntent.Renew
  );
  assert.equal(stale.status, framework.ZLinkLocationWriteStatus.IgnoredStale);

  const renewed = await store.updateClientServer(
    descriptor(owner, { descriptorRevision: 2n, weight: 50 }),
    framework.ZLinkLocationWriteIntent.Renew
  );
  assert.equal(renewed.status, framework.ZLinkLocationWriteStatus.Stored);
  const page = await store.listClientServers('orders', { pageSize: 1 });
  assert.equal(page.items.length, 1);
  assert.equal(page.items[0].weight, 50);
  assert.equal(page.items[0].descriptorRevision, 2n);

  assert.equal(
    await store.removeClientServer(
      { channelName: 'orders', serverRid: 'server-a' },
      owner.token
    ),
    framework.ZLinkLocationWriteStatus.Stored
  );
  assert.equal((await store.listClientServers('orders')).items.length, 0);
});

test('automatic ClientServer startup requires the dedicated descriptor store capability', () => {
  assert.throws(() => internal.createFrameworkRegistration({
    channels: { orders: { client: { manualConnections: [] } } },
    locations: { storeInstance: {} }
  }), /dedicated ClientServer descriptor store operations/);
});

test('ClientServer socket identity advertises the concrete port returned after bind', async () => {
  const registration = internal.createFrameworkRegistration({
    channels: {
      orders: {
        server: {
          bind: 'tcp://0.0.0.0:0',
          advertiseHost: 'orders.internal'
        },
        sendHandlers: [{ packetName: 'notice', handler: { handle() {} } }]
      }
    }
  });
  const router = {
    nativeInstance: {},
    lastEndpoint: undefined,
    peerWeight: 100,
    sendHighWaterMark: 0,
    receiveHighWaterMark: 0,
    sendTimeoutMs: -1,
    maxMessageSize: -1,
    setChannelName() {},
    setRoutingId(value) { this.routingId = value; },
    onSendReady() {},
    bind(endpoint) {
      assert.equal(endpoint, 'tcp://0.0.0.0:0');
      this.lastEndpoint = 'tcp://0.0.0.0:49152';
    },
    connect() {},
    disconnect() {},
    async dispose() {}
  };
  const sockets = new ZLinkChannelSocketRegistry(
    registration,
    { createRouterSocket() { return router; } },
    {}
  );

  const identity = sockets.clientServerServerIdentity('orders');
  assert.equal(identity.endpoint, 'tcp://orders.internal:49152');
  assert.equal(identity.serverRid, router.routingId);
  assert.ok(identity.lifecycleGeneration > 0n);
  await sockets.dispose();
});

test('production ClientServer outbound socket selection uses admitted descriptor weights', async () => {
  const registration = internal.createFrameworkRegistration({
    channels: { orders: { client: { manualConnections: [] } } },
    locations: { useInMemoryStores: true }
  });
  const dealers = [];
  const adapter = {
    createDealerSocket() {
      const dealer = fakeDealer(`dealer-${dealers.length}`);
      dealers.push(dealer);
      return dealer;
    }
  };
  const monitoringAdapter = {
    openSocketMonitor() {
      return {
        nativeInstance: {},
        onEvent() {},
        async dispose() {}
      };
    }
  };
  const sockets = new ZLinkChannelSocketRegistry(
    registration,
    adapter,
    {},
    monitoringAdapter
  );
  sockets.openClientServerConnection(
    'orders',
    'orders-a:7',
    'tcp://10.0.0.1:9401',
    { onTransportReady() {}, onTerminated() {} }
  );
  sockets.openClientServerConnection(
    'orders',
    'orders-b:7',
    'tcp://10.0.0.2:9401',
    { onTransportReady() {}, onTerminated() {} }
  );
  assert.equal(sockets.clientDealerForOutbound('orders'), undefined);
  sockets.admitClientServerConnection(discoveryDescriptor('server-a', 1), 'orders-a:7');
  sockets.admitClientServerConnection(discoveryDescriptor('server-b', 3), 'orders-b:7');

  const selected = Array.from(
    { length: 8 },
    () => sockets.clientDealerForOutbound('orders').id
  );
  assert.deepEqual(selected, [
    'dealer-0',
    'dealer-1',
    'dealer-1',
    'dealer-1',
    'dealer-0',
    'dealer-1',
    'dealer-1',
    'dealer-1'
  ]);
  sockets.admitClientServerConnection({
    ...discoveryDescriptor('server-a', 0),
    descriptorRevision: 2n
  }, 'orders-a:7');
  assert.deepEqual(
    Array.from({ length: 4 }, () => sockets.clientDealerForOutbound('orders').id),
    ['dealer-1', 'dealer-1', 'dealer-1', 'dealer-1']
  );
  await sockets.dispose();
});

test('ClientServer reserved hello is consumed before application dispatch and returns exact admit', async () => {
  const registration = internal.createFrameworkRegistration({
    channels: {
      orders: {
        server: { bind: 'tcp://127.0.0.1:9401' },
        sendHandlers: [{ packetName: 'notice', handler: { handle() {} } }]
      }
    }
  });
  const sockets = new ZLinkChannelSocketRegistry(registration, {}, {});
  const server = {
    ...descriptor({
      token: { ownerId: 'server-owner', leaseGeneration: 5n }
    }),
    updatedAt: new Date()
  };
  sockets.setClientServerServerDescriptor(server, 'orders');
  const hello = zlink.Message.from(clientServerWire.encodeClientServerHello({
    channelName: 'orders',
    securityIdentity: 'cluster-a',
    normalizedEffectiveMaxMessageBytes: 1024
  }));
  let reply;
  let received = {
    parts: [hello],
    requestSeq: 1n,
    routingId: 'client-a',
    close() { hello.close(); }
  };
  const router = {
    maxMessageSize: 4096,
    recv() {
      const value = received;
      received = undefined;
      return value;
    },
    reply(_routingId, _requestSeq, message) {
      reply = Buffer.from(message.data());
    }
  };
  let applicationDispatches = 0;
  const loop = new ZLinkChannelReceiveLoop(
    'orders',
    router,
    { async dispatch() { applicationDispatches++; } },
    undefined,
    (record, socket) =>
      sockets.tryHandleClientServerControl('orders', record, socket)
  );
  const controller = new AbortController();
  const running = loop.run(controller.signal);
  await new Promise(resolve => setImmediate(resolve));
  controller.abort();
  await loop.stop();
  await running;

  assert.equal(applicationDispatches, 0);
  const decoded = clientServerWire.decodeClientServerControl(reply);
  assert.equal(decoded.kind, 'admit');
  assert.equal(decoded.admission.serverRid, String(server.serverRid));
  assert.equal(decoded.admission.lifecycleGeneration, server.lifecycleGeneration);
  assert.equal(decoded.admission.securityIdentity, server.securityIdentity);
  assert.equal(decoded.admission.normalizedEffectiveMaxMessageBytes, 4096);
  await sockets.dispose();
});

test('ClientServer server publishes its concrete endpoint then drains and removes its descriptor', async () => {
  const store = new internal.ZLinkInMemoryLocationStore();
  const runtime = new internal.ZLinkLocationRuntime({
    stores: stores(store),
    ownerId: 'local-owner'
  });
  await runtime.start('host-a');
  const registration = internal.createFrameworkRegistration({
    channels: {
      orders: {
        server: { bind: 'tcp://0.0.0.0:0', advertiseHost: 'orders.internal', weight: 75 },
        sendHandlers: [{ packetName: 'notice', handler: { handle() {} } }]
      }
    },
    locations: { useInMemoryStores: true }
  });
  const dealerCalls = [];
  const sockets = {
    serverWeight: 75,
    clientServerServerIdentity() {
      return {
        serverRid: 'server-a',
        lifecycleGeneration: 11n,
        endpoint: 'tcp://orders.internal:49152'
      };
    },
    clientDealer() {
      return {
        connect(endpoint) { dealerCalls.push(`connect:${endpoint}`); },
        disconnect(endpoint) { dealerCalls.push(`disconnect:${endpoint}`); }
      };
    },
    clientServerServerSocket() { return { peerWeight: this.serverWeight }; },
    setClientServerServerDescriptor() {}
  };
  const discovery = new internal.ZLinkClientServerLocationRuntime(
    registration,
    sockets,
    runtime,
    stores(store),
    { pollingIntervalMs: 60_000 }
  );

  await discovery.start();
  const published = await store.listClientServers('orders');
  assert.equal(published.items.length, 1);
  assert.equal(published.items[0].endpoint, 'tcp://orders.internal:49152');
  assert.equal(published.items[0].serverRid, 'server-a');
  assert.equal(published.items[0].lifecycleGeneration, 11n);
  assert.deepEqual(dealerCalls, []);

  sockets.serverWeight = 25;
  await discovery.tick();
  const reweighted = await store.listClientServers('orders');
  assert.equal(reweighted.items[0].weight, 25);
  assert.equal(reweighted.items[0].descriptorRevision, 2n);
  await discovery.tick();
  assert.equal((await store.listClientServers('orders')).items[0].descriptorRevision, 2n);

  await discovery.stop();
  assert.equal((await store.listClientServers('orders')).items.length, 0);
  await runtime.stop();
});

test('automatic ClientServer client reconciles dedicated server descriptors by RID and lifecycle', async () => {
  const store = new internal.ZLinkInMemoryLocationStore();
  const localRuntime = new internal.ZLinkLocationRuntime({
    stores: stores(store),
    ownerId: 'client-owner'
  });
  await localRuntime.start('client-host');
  const remoteOwner = await store.claimOwnerLease('remote-owner', 30_000);
  assert.equal(remoteOwner.kind, 'claimed');
  await store.updateClientServer(
    descriptor(remoteOwner),
    framework.ZLinkLocationWriteIntent.NewClaim
  );

  const registration = internal.createFrameworkRegistration({
    channels: { orders: { client: { manualConnections: [] } } },
    locations: { useInMemoryStores: true }
  });
  const sockets = automaticClientServerSockets();
  const discovery = new internal.ZLinkClientServerLocationRuntime(
    registration,
    sockets,
    localRuntime,
    stores(store),
    { pollingIntervalMs: 60_000 }
  );

  await discovery.start();
  assert.deepEqual(sockets.calls, ['connect:tcp://10.0.0.1:9401']);
  assert.equal(discovery.activeTargets('orders').length, 0);
  await sockets.admit(descriptor(remoteOwner));
  assert.equal(discovery.activeTargets('orders')[0].lifecycleGeneration, 7n);

  await store.updateClientServer(
    descriptor(remoteOwner, {
      descriptorRevision: 2n,
      endpoint: 'tcp://10.0.0.2:9401'
    }),
    framework.ZLinkLocationWriteIntent.Renew
  );
  // Endpoint is immutable in one lifecycle, so the store fences the mutation.
  await discovery.tick();
  assert.deepEqual(sockets.calls, ['connect:tcp://10.0.0.1:9401']);

  await store.removeClientServer(
    { channelName: 'orders', serverRid: 'server-a' },
    remoteOwner.token
  );
  await discovery.tick();
  assert.deepEqual(sockets.calls, [
    'connect:tcp://10.0.0.1:9401',
    'disconnect:tcp://10.0.0.1:9401'
  ]);

  await discovery.stop();
  await localRuntime.stop();
});

test('same ClientServer RID and endpoint reset transport readiness on a new lifecycle', async () => {
  let nowMs = Date.UTC(2026, 6, 23, 0, 0, 0);
  const store = new internal.ZLinkInMemoryLocationStore(() => new Date(nowMs));
  const localRuntime = new internal.ZLinkLocationRuntime({
    stores: stores(store),
    ownerId: 'client-owner'
  });
  await localRuntime.start('client-host');
  const oldOwner = await store.claimOwnerLease('old-server-owner', 100);
  assert.equal(oldOwner.kind, 'claimed');
  await store.updateClientServer(
    descriptor(oldOwner),
    framework.ZLinkLocationWriteIntent.NewClaim
  );
  const registration = internal.createFrameworkRegistration({
    channels: { orders: { client: { manualConnections: [] } } },
    locations: { useInMemoryStores: true }
  });
  const sockets = automaticClientServerSockets();
  const discovery = new internal.ZLinkClientServerLocationRuntime(
    registration,
    sockets,
    localRuntime,
    stores(store),
    { pollingIntervalMs: 60_000 }
  );
  await discovery.start();
  await sockets.admit(descriptor(oldOwner));

  nowMs += 101;
  const newOwner = await store.claimOwnerLease('new-server-owner', 30_000);
  assert.equal(newOwner.kind, 'claimed');
  const takeover = await store.updateClientServer(
    descriptor(newOwner, { lifecycleGeneration: 8n }),
    framework.ZLinkLocationWriteIntent.Takeover
  );
  assert.equal(takeover.status, framework.ZLinkLocationWriteStatus.Stored);
  await discovery.tick();

  assert.deepEqual(sockets.calls, [
    'connect:tcp://10.0.0.1:9401',
    'connect:tcp://10.0.0.1:9401'
  ]);
  assert.equal(discovery.activeTargets('orders')[0].lifecycleGeneration, 7n);
  await sockets.admit(descriptor(newOwner, { lifecycleGeneration: 8n }));
  assert.deepEqual(sockets.calls, [
    'connect:tcp://10.0.0.1:9401',
    'connect:tcp://10.0.0.1:9401',
    'disconnect:tcp://10.0.0.1:9401'
  ]);
  assert.equal(discovery.activeTargets('orders')[0].lifecycleGeneration, 8n);
  sockets.terminate(7n);
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(discovery.activeTargets('orders')[0].lifecycleGeneration, 8n);
  await discovery.stop();
  await localRuntime.stop();
});

test('ClientServer target remains unavailable when service admission mismatches security identity', async () => {
  const store = new internal.ZLinkInMemoryLocationStore();
  const localRuntime = new internal.ZLinkLocationRuntime({
    stores: stores(store),
    ownerId: 'client-owner'
  });
  await localRuntime.start('client-host');
  const remoteOwner = await store.claimOwnerLease('remote-owner', 30_000);
  assert.equal(remoteOwner.kind, 'claimed');
  const expected = descriptor(remoteOwner);
  await store.updateClientServer(expected, framework.ZLinkLocationWriteIntent.NewClaim);
  const registration = internal.createFrameworkRegistration({
    channels: { orders: { client: { manualConnections: [] } } },
    locations: { useInMemoryStores: true }
  });
  const sockets = automaticClientServerSockets();
  const discovery = new internal.ZLinkClientServerLocationRuntime(
    registration,
    sockets,
    localRuntime,
    stores(store),
    { pollingIntervalMs: 60_000 }
  );
  await discovery.start();
  await sockets.admit(expected, { securityIdentity: 'wrong-cluster' });

  assert.equal(discovery.activeTargets('orders').length, 0);
  assert.deepEqual(sockets.calls, [
    'connect:tcp://10.0.0.1:9401',
    'disconnect:tcp://10.0.0.1:9401'
  ]);
  assert.match((await localRuntime.getStatus()).lastError, /admission does not match/);
  await discovery.stop();
  await localRuntime.stop();
});

test('periodic ClientServer discovery failures remain observable on location runtime status', async () => {
  const store = new internal.ZLinkInMemoryLocationStore();
  const runtime = new internal.ZLinkLocationRuntime({
    stores: stores(store),
    ownerId: 'client-owner'
  });
  await runtime.start('client-host');
  const registration = internal.createFrameworkRegistration({
    channels: { orders: { client: { manualConnections: [] } } },
    locations: { useInMemoryStores: true }
  });
  const dealer = { connect() {}, disconnect() {} };
  const discovery = new internal.ZLinkClientServerLocationRuntime(
    registration,
    { clientDealer() { return dealer; } },
    runtime,
    stores(store),
    { pollingIntervalMs: 1 }
  );
  await discovery.start();
  const originalList = store.listClientServers.bind(store);
  store.listClientServers = async () => {
    throw new Error('client-server-store-unavailable');
  };
  await new Promise((resolve) => setTimeout(resolve, 10));
  assert.match((await runtime.getStatus()).lastError, /client-server-store-unavailable/);
  store.listClientServers = originalList;
  await discovery.stop();
  await runtime.stop();
});

function automaticClientServerSockets() {
  const connections = new Map();
  const history = new Map();
  const ready = new Map();
  const calls = [];
  return {
    calls,
    openClientServerConnection(channelName, connectionId, endpoint, callbacks) {
      const dealer = {
        maxMessageSize: 0x7fff_ffff,
        request(message, callback) {
          const connection = connections.get(connectionId);
          connection.hello = Buffer.from(message.data());
          connection.reply = callback;
          return true;
        }
      };
      connections.set(connectionId, {
        channelName,
        connectionId,
        endpoint,
        callbacks,
        dealer
      });
      history.set(connectionId, connections.get(connectionId));
      calls.push(`connect:${endpoint}`);
      return dealer;
    },
    async closeClientServerConnection(connectionId) {
      const connection = connections.get(connectionId);
      if (connection === undefined) return;
      connections.delete(connectionId);
      ready.delete(connectionId);
      calls.push(`disconnect:${connection.endpoint}`);
    },
    admitClientServerConnection(value, connectionId) {
      if (!connections.has(connectionId)) return false;
      ready.set(connectionId, value);
      return true;
    },
    removeClientServerReady(_channelName, _serverRoutingId, connectionId) {
      return ready.delete(connectionId);
    },
    setClientServerServerDescriptor() {},
    clientServerServerSocket() { return { peerWeight: 100 }; },
    async admit(value, overrides = {}) {
      const connectionId = [...connections.keys()]
        .find(id => id.endsWith(`:${value.lifecycleGeneration}`));
      assert.notEqual(connectionId, undefined);
      const connection = connections.get(connectionId);
      connection.callbacks.onTransportReady(String(value.serverRid), value.endpoint);
      await new Promise(resolve => setImmediate(resolve));
      assert.notEqual(connection.reply, undefined);
      const reply = zlink.Message.from(clientServerWire.encodeClientServerAdmit({
        ...value,
        ...overrides
      }, 0x7fff_ffff));
      connection.reply(0, [reply]);
      await new Promise(resolve => setImmediate(resolve));
    },
    terminate(lifecycleGeneration) {
      const connectionId = [...history.keys()]
        .find(id => id.endsWith(`:${lifecycleGeneration}`));
      const connection = connectionId === undefined ? undefined : history.get(connectionId);
      connection?.callbacks.onTerminated(String(connection.serverRid), connection.endpoint);
    }
  };
}

function fakeDealer(id) {
  return {
    id,
    nativeInstance: {},
    peerWeight: 100,
    sendHighWaterMark: 0,
    receiveHighWaterMark: 0,
    sendTimeoutMs: -1,
    maxMessageSize: -1,
    setChannelName() {},
    setRoutingId() {},
    onSendReady() {},
    connect() {},
    disconnect() {},
    send() { return true; },
    request() { return true; },
    recv() { return undefined; },
    async dispose() {}
  };
}

function discoveryDescriptor(serverRoutingId, weight) {
  return {
    channelName: 'orders',
    serverRoutingId,
    lifecycleGeneration: 7n,
    descriptorRevision: 1n,
    weight,
    state: 'serving',
    securityIdentity: 'cluster-a',
    effectiveMaxMessageBytes: 1024,
    advertisedEndpoint: `tcp://10.0.0.${serverRoutingId.endsWith('a') ? 1 : 2}:9401`
  };
}
