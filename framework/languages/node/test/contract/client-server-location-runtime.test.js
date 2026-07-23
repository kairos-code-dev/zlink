const assert = require('node:assert/strict');
const test = require('node:test');
const framework = require('../../packages/framework/dist');
const internal = require('../../packages/framework/dist/internal');
const {
  ZLinkChannelSocketRegistry
} = require('../../packages/framework/dist/runtime/channels/channel-socket-registry');

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
    }
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
  const calls = [];
  const dealer = {
    connect(endpoint) { calls.push(`connect:${endpoint}`); },
    disconnect(endpoint) { calls.push(`disconnect:${endpoint}`); }
  };
  const discovery = new internal.ZLinkClientServerLocationRuntime(
    registration,
    { clientDealer() { return dealer; } },
    localRuntime,
    stores(store),
    { pollingIntervalMs: 60_000 }
  );

  await discovery.start();
  assert.deepEqual(calls, ['connect:tcp://10.0.0.1:9401']);
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
  assert.deepEqual(calls, ['connect:tcp://10.0.0.1:9401']);

  await store.removeClientServer(
    { channelName: 'orders', serverRid: 'server-a' },
    remoteOwner.token
  );
  await discovery.tick();
  assert.deepEqual(calls, [
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
  const calls = [];
  const dealer = {
    connect(endpoint) { calls.push(`connect:${endpoint}`); },
    disconnect(endpoint) { calls.push(`disconnect:${endpoint}`); }
  };
  const discovery = new internal.ZLinkClientServerLocationRuntime(
    registration,
    { clientDealer() { return dealer; } },
    localRuntime,
    stores(store),
    { pollingIntervalMs: 60_000 }
  );
  await discovery.start();

  nowMs += 101;
  const newOwner = await store.claimOwnerLease('new-server-owner', 30_000);
  assert.equal(newOwner.kind, 'claimed');
  const takeover = await store.updateClientServer(
    descriptor(newOwner, { lifecycleGeneration: 8n }),
    framework.ZLinkLocationWriteIntent.Takeover
  );
  assert.equal(takeover.status, framework.ZLinkLocationWriteStatus.Stored);
  await discovery.tick();

  assert.deepEqual(calls, [
    'connect:tcp://10.0.0.1:9401',
    'disconnect:tcp://10.0.0.1:9401',
    'connect:tcp://10.0.0.1:9401'
  ]);
  assert.equal(discovery.activeTargets('orders')[0].lifecycleGeneration, 8n);
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
