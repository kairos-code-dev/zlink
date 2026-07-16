const assert = require('node:assert/strict');
const test = require('node:test');
const zlink = require('@zlink-systems/zlink');
const framework = require('../../packages/framework/dist');
const internal = require('../../packages/framework/dist/internal');

test('auto-connect planner applies role policy pairwise initiator and dial-only exception', () => {
  const routeLocal = local(framework.ZLinkLocationAutoConnectType.RouteMesh, framework.ZLinkLocationRole.Router, 'node-a', 'tcp://a');
  const routeDesired = internal.ZLinkAutoConnectPlanner.computeDesired(routeLocal, [
    peer('owner-b', framework.ZLinkLocationAutoConnectType.RouteMesh, framework.ZLinkLocationRole.Router, 'node-b', 'tcp://b'),
    peer('owner-0', framework.ZLinkLocationAutoConnectType.RouteMesh, framework.ZLinkLocationRole.Router, 'node-0', 'tcp://0'),
    peer('owner-dealer', framework.ZLinkLocationAutoConnectType.RouteMesh, framework.ZLinkLocationRole.Dealer, 'node-c', 'tcp://c'),
    peer('owner-self', framework.ZLinkLocationAutoConnectType.RouteMesh, framework.ZLinkLocationRole.Router, 'node-a', 'tcp://a')
  ]);

  assert.deepEqual([...routeDesired.values()].map((target) => target.endpoint), ['tcp://b']);

  const dialOnly = local(framework.ZLinkLocationAutoConnectType.RouteMesh, framework.ZLinkLocationRole.Router, 'node-z', '');
  const dialOnlyDesired = internal.ZLinkAutoConnectPlanner.computeDesired(dialOnly, [
    peer('owner-a', framework.ZLinkLocationAutoConnectType.RouteMesh, framework.ZLinkLocationRole.Router, 'node-a', 'tcp://a')
  ]);
  assert.deepEqual([...dialOnlyDesired.values()].map((target) => target.endpoint), ['tcp://a']);

  const dealer = local(framework.ZLinkLocationAutoConnectType.ClientServer, framework.ZLinkLocationRole.Dealer, 'node-d', 'tcp://dealer');
  const clientServerDesired = internal.ZLinkAutoConnectPlanner.computeDesired(dealer, [
    peer('owner-router', framework.ZLinkLocationAutoConnectType.ClientServer, framework.ZLinkLocationRole.Router, 'node-r', 'tcp://router'),
    peer('owner-dealer', framework.ZLinkLocationAutoConnectType.ClientServer, framework.ZLinkLocationRole.Dealer, 'node-x', 'tcp://peer-dealer')
  ]);
  assert.deepEqual([...clientServerDesired.values()].map((target) => target.endpoint), ['tcp://router']);

  const spot = local(framework.ZLinkLocationAutoConnectType.SpotMesh, framework.ZLinkLocationRole.Spot, 'node-a', 'tcp://spot-a');
  const spotDesired = internal.ZLinkAutoConnectPlanner.computeDesired(spot, [
    { ...peer('owner-b', framework.ZLinkLocationAutoConnectType.SpotMesh, framework.ZLinkLocationRole.Spot, 'node-b', 'tcp://spot-b'), metadata: { 'pub-endpoint': 'tcp://pub-b' } },
    { ...peer('owner-0', framework.ZLinkLocationAutoConnectType.SpotMesh, framework.ZLinkLocationRole.Spot, 'node-0', 'tcp://spot-0'), metadata: { 'pub-endpoint': 'tcp://pub-0' } },
    peer('owner-router', framework.ZLinkLocationAutoConnectType.SpotMesh, framework.ZLinkLocationRole.Router, 'node-r', 'tcp://router')
  ]);
  assert.deepEqual([...spotDesired.values()].map((target) => target.endpoint), ['tcp://spot-b', 'tcp://pub-b', 'tcp://pub-0']);
});

test('store peer resolver reads store each time and joins owner liveness', async () => {
  let nowMs = Date.UTC(2026, 6, 3, 0, 0, 0);
  const store = new internal.ZLinkInMemoryLocationStore(() => new Date(nowMs));
  await store.renewOwnerLease('owner-live', rid('node-live'), 1000);
  await store.updatePeer(
    peer('owner-live', framework.ZLinkLocationAutoConnectType.RouteMesh, framework.ZLinkLocationRole.Router, 'node-live', 'tcp://live'),
    framework.ZLinkLocationWriteIntent.NewClaim
  );
  await store.updatePeer(
    peer('owner-missing', framework.ZLinkLocationAutoConnectType.RouteMesh, framework.ZLinkLocationRole.Router, 'node-missing', 'tcp://missing'),
    framework.ZLinkLocationWriteIntent.NewClaim
  );
  const tracker = new internal.ZLinkOwnerLeaseTracker({
    store,
    options: { pollingIntervalMs: 0 },
    monotonicNowMs: () => 0
  });
  const resolver = new internal.ZLinkStoreLocationResolvers({
    stores: stores(store),
    leaseTracker: tracker
  });

  assert.deepEqual(
    (await resolver.listLivePeers({ meshName: 'play' })).map((row) => row.endpoint),
    ['tcp://live']
  );

  nowMs += 1001;
  assert.deepEqual(await resolver.listLivePeers({ meshName: 'play' }), []);
});

test('auto-connect reconciler publishes local row diffs handover and stays fail-static on store failure', async () => {
  const store = new internal.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const runtime = runtimeFor(store, 'owner-local');
  await runtime.start(rid('node-local'));

  await store.renewOwnerLease('owner-remote', rid('node-remote'), 30000);
  const remote = await store.updatePeer(
    peer('owner-remote', framework.ZLinkLocationAutoConnectType.ClientServer, framework.ZLinkLocationRole.Router, 'node-remote', 'tcp://remote'),
    framework.ZLinkLocationWriteIntent.NewClaim
  );
  assert.equal(remote.status, framework.ZLinkLocationWriteStatus.Stored);

  const resolver = new SwitchablePeerResolver(new internal.ZLinkStoreLocationResolvers({
    stores: stores(store),
    leaseTracker: new internal.ZLinkOwnerLeaseTracker({
      store,
      options: { pollingIntervalMs: 0 },
      monotonicNowMs: () => 0
    })
  }));
  const calls = [];
  const reconciler = new internal.ZLinkAutoConnectReconciler({
    local: local(framework.ZLinkLocationAutoConnectType.ClientServer, framework.ZLinkLocationRole.Dealer, 'node-local', 'tcp://dealer'),
    localRow: peer('ignored', framework.ZLinkLocationAutoConnectType.ClientServer, framework.ZLinkLocationRole.Dealer, 'node-local', 'tcp://dealer'),
    runtime,
    peerResolver: resolver,
    executor: executor(calls),
    options: { heartbeatIntervalMs: 1000 },
    monotonicNowMs: () => 0
  });

  await reconciler.tick();
  assert.deepEqual(calls, ['connect:tcp://remote:owner-remote']);
  assert.equal((await store.listPeers({ endpoint: 'tcp://dealer' })).length, 1);
  assert.equal(reconciler.knowsPeer(rid('node-remote')), true);

  await store.renewOwnerLease('owner-restarted', rid('node-remote'), 30000);
  await store.updatePeer(
    peer('owner-restarted', framework.ZLinkLocationAutoConnectType.ClientServer, framework.ZLinkLocationRole.Router, 'node-remote', 'tcp://remote'),
    framework.ZLinkLocationWriteIntent.Takeover
  );
  await reconciler.tick();
  assert.deepEqual(calls, [
    'connect:tcp://remote:owner-remote',
    'disconnect:tcp://remote:owner-remote',
    'connect:tcp://remote:owner-restarted'
  ]);

  resolver.fail = true;
  await reconciler.tick();
  assert.equal(reconciler.storeFailed, true);
  assert.equal(reconciler.activeTargets.length, 1);
  assert.deepEqual(calls.slice(-1), ['connect:tcp://remote:owner-restarted']);

  await reconciler.shutdown();
  assert.deepEqual(calls.slice(-1), ['disconnect:tcp://remote:owner-restarted']);
  assert.equal((await store.listPeers({ endpoint: 'tcp://dealer' })).length, 1);
  await runtime.stop();
  assert.equal((await store.listPeers({ endpoint: 'tcp://dealer' })).length, 0);
});

test('auto-connect reconciler does not mark a target active when executor skips dial', async () => {
  const store = new internal.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const runtime = runtimeFor(store, 'owner-local');
  await runtime.start(rid('node-local'));
  await store.renewOwnerLease('owner-remote', rid('node-remote'), 30000);
  await store.updatePeer(
    peer('owner-remote', framework.ZLinkLocationAutoConnectType.RouteMesh, framework.ZLinkLocationRole.Router, 'node-remote', 'tcp://manual'),
    framework.ZLinkLocationWriteIntent.NewClaim
  );

  const calls = [];
  const reconciler = new internal.ZLinkAutoConnectReconciler({
    local: local(framework.ZLinkLocationAutoConnectType.RouteMesh, framework.ZLinkLocationRole.Router, 'node-local', 'tcp://local'),
    localRow: peer('ignored', framework.ZLinkLocationAutoConnectType.RouteMesh, framework.ZLinkLocationRole.Router, 'node-local', 'tcp://local'),
    runtime,
    peerResolver: new internal.ZLinkStoreLocationResolvers({
      stores: stores(store),
      leaseTracker: new internal.ZLinkOwnerLeaseTracker({
        store,
        options: { pollingIntervalMs: 0 },
        monotonicNowMs: () => 0
      })
    }),
    executor: {
      connect(target) {
        calls.push(`skip:${target.endpoint}:${target.ownerId}`);
        return false;
      },
      disconnect(target) {
        calls.push(`disconnect:${target.endpoint}:${target.ownerId}`);
      }
    },
    options: { heartbeatIntervalMs: 1000 },
    monotonicNowMs: () => 0
  });

  await reconciler.tick();
  assert.deepEqual(calls, ['skip:tcp://manual:owner-remote']);
  assert.deepEqual(reconciler.activeTargets, []);

  await reconciler.shutdown();
  assert.deepEqual(calls, ['skip:tcp://manual:owner-remote']);
  await runtime.stop();
});

test('auto-connect reconciler waits for an old peer disconnect before reusing its route key', async () => {
  let disconnected = false;
  let rows = [peer(
    'owner-old',
    framework.ZLinkLocationAutoConnectType.SpotMesh,
    framework.ZLinkLocationRole.Spot,
    'node-z',
    'tcp://old'
  )];
  const calls = [];
  const reconciler = new internal.ZLinkAutoConnectReconciler({
    local: local(
      framework.ZLinkLocationAutoConnectType.SpotMesh,
      framework.ZLinkLocationRole.Spot,
      'node-a',
      'tcp://local'
    ),
    runtime: {},
    peerResolver: { async listLivePeers() { return rows; } },
    executor: {
      connect(target) {
        calls.push(`connect:${target.endpoint}`);
        return true;
      },
      disconnect(target) {
        calls.push(`disconnect:${target.endpoint}`);
      },
      isDisconnected() {
        return disconnected;
      }
    }
  });

  await reconciler.tick();
  rows = [peer(
    'owner-new',
    framework.ZLinkLocationAutoConnectType.SpotMesh,
    framework.ZLinkLocationRole.Spot,
    'node-z',
    'tcp://new'
  )];
  await reconciler.tick();
  assert.deepEqual(calls, ['connect:tcp://old', 'disconnect:tcp://old']);

  disconnected = true;
  await reconciler.tick();
  assert.deepEqual(calls, [
    'connect:tcp://old',
    'disconnect:tcp://old',
    'connect:tcp://new'
  ]);
});

test('auto-connect reconciler removes a disconnected endpoint until a fresh store read', async () => {
  let storeFailed = false;
  let disconnected;
  const calls = [];
  const remote = peer(
    'owner-remote',
    framework.ZLinkLocationAutoConnectType.ClientServer,
    framework.ZLinkLocationRole.Router,
    'node-remote',
    'tcp://remote'
  );
  const reconciler = new internal.ZLinkAutoConnectReconciler({
    local: local(
      framework.ZLinkLocationAutoConnectType.ClientServer,
      framework.ZLinkLocationRole.Dealer,
      'node-local',
      'tcp://dealer'
    ),
    runtime: {},
    peerResolver: {
      async listLivePeers() {
        if (storeFailed) throw new Error('store unavailable');
        return [remote];
      }
    },
    executor: {
      connect(target) {
        calls.push(`connect:${target.endpoint}`);
        return true;
      },
      disconnect() {},
      onDisconnected(handler) {
        disconnected = handler;
      }
    },
    options: { storeFailureGraceMs: 3000 },
    monotonicNowMs: () => 0
  });

  await reconciler.tick();
  assert.deepEqual(calls, ['connect:tcp://remote']);
  storeFailed = true;
  disconnected('tcp://remote');
  await reconciler.tick();
  assert.deepEqual(calls, ['connect:tcp://remote']);
  assert.deepEqual(reconciler.activeTargets, []);

  storeFailed = false;
  await reconciler.tick();
  assert.deepEqual(calls, ['connect:tcp://remote', 'connect:tcp://remote']);
  assert.equal(reconciler.activeTargets.length, 1);
});

test('auto-connect reconciler retries the last desired target only within store failure grace', async () => {
  let nowMs = 0;
  let storeFailed = false;
  let connectAttempts = 0;
  const reconciler = new internal.ZLinkAutoConnectReconciler({
    local: local(
      framework.ZLinkLocationAutoConnectType.ClientServer,
      framework.ZLinkLocationRole.Dealer,
      'node-local',
      'tcp://dealer'
    ),
    runtime: {},
    peerResolver: {
      async listLivePeers() {
        if (storeFailed) throw new Error('store unavailable');
        return [peer(
          'owner-remote',
          framework.ZLinkLocationAutoConnectType.ClientServer,
          framework.ZLinkLocationRole.Router,
          'node-remote',
          'tcp://remote'
        )];
      }
    },
    executor: {
      connect() {
        connectAttempts += 1;
        return false;
      },
      disconnect() {}
    },
    options: { storeFailureGraceMs: 3000 },
    monotonicNowMs: () => nowMs
  });

  await reconciler.tick();
  assert.equal(connectAttempts, 1);
  storeFailed = true;
  await reconciler.tick();
  assert.equal(connectAttempts, 2);

  nowMs = 4000;
  await reconciler.tick();
  assert.equal(connectAttempts, 2);
});

test('publish-only auto-connect capability does not query or reconcile peers', async () => {
  const store = new internal.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const runtime = runtimeFor(store, 'owner-local');
  await runtime.start(rid('node-local'));
  let peerQueries = 0;
  const reconciler = new internal.ZLinkAutoConnectReconciler({
    local: local(framework.ZLinkLocationAutoConnectType.RouteMesh, framework.ZLinkLocationRole.Router, 'node-local', 'tcp://local'),
    localRow: peer('ignored', framework.ZLinkLocationAutoConnectType.RouteMesh, framework.ZLinkLocationRole.Router, 'node-local', 'tcp://local'),
    runtime,
    peerResolver: {
      async listLivePeers() {
        peerQueries += 1;
        return [];
      }
    },
    executor: {
      connect() {
        assert.fail('publish-only capability must not connect peers');
      },
      disconnect() {
        assert.fail('publish-only capability must not disconnect peers');
      }
    },
    reconcilePeers: false
  });

  await reconciler.tick();
  assert.equal(peerQueries, 0);
  assert.equal((await store.listPeers({ endpoint: 'tcp://local' })).length, 1);

  await reconciler.shutdown();
  await runtime.stop();
});

test('auto-connect reconciler retains an existing draining peer without dialing a new draining peer', async () => {
  const store = new internal.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const runtime = runtimeFor(store, 'owner-local');
  await runtime.start(rid('node-local'));
  await store.renewOwnerLease('owner-existing', rid('node-existing'), 30000);
  const existing = await store.updatePeer(
    peer('owner-existing', framework.ZLinkLocationAutoConnectType.ClientServer, framework.ZLinkLocationRole.Router, 'node-existing', 'tcp://existing'),
    framework.ZLinkLocationWriteIntent.NewClaim
  );
  const calls = [];
  const reconciler = new internal.ZLinkAutoConnectReconciler({
    local: local(framework.ZLinkLocationAutoConnectType.ClientServer, framework.ZLinkLocationRole.Dealer, 'node-local', 'tcp://dealer'),
    localRow: peer('ignored', framework.ZLinkLocationAutoConnectType.ClientServer, framework.ZLinkLocationRole.Dealer, 'node-local', 'tcp://dealer'),
    runtime,
    peerResolver: new internal.ZLinkStoreLocationResolvers({
      stores: stores(store),
      leaseTracker: new internal.ZLinkOwnerLeaseTracker({ store, options: { pollingIntervalMs: 0 }, monotonicNowMs: () => 0 })
    }),
    executor: executor(calls),
    options: { heartbeatIntervalMs: 1000 },
    monotonicNowMs: () => 0
  });
  await reconciler.tick();
  await store.updatePeer({
    ...peer('owner-existing', framework.ZLinkLocationAutoConnectType.ClientServer, framework.ZLinkLocationRole.Router, 'node-existing', 'tcp://existing'),
    draining: true,
    generation: existing.generation
  }, framework.ZLinkLocationWriteIntent.Renew);
  await store.renewOwnerLease('owner-new', rid('node-new'), 30000);
  await store.updatePeer({
    ...peer('owner-new', framework.ZLinkLocationAutoConnectType.ClientServer, framework.ZLinkLocationRole.Router, 'node-new', 'tcp://new'),
    draining: true
  }, framework.ZLinkLocationWriteIntent.NewClaim);
  await reconciler.tick();
  assert.deepEqual(calls, ['connect:tcp://existing:owner-existing']);
  assert.deepEqual(reconciler.activeTargets.map((target) => target.endpoint), ['tcp://existing']);
  await reconciler.shutdown();
  await runtime.stop();
});

test('auto-connect loop skips unchanged change stamp until live owner set changes', async () => {
  const reconciler = {
    storeFailed: false,
    ticks: 0,
    async tick() {
      this.ticks++;
    },
    async shutdown() {}
  };
  const stampStore = {
    stamp: 1n,
    async getChangeStamp() {
      return this.stamp;
    }
  };
  const leaseTracker = {
    version: 1,
    async getLiveOwnerSetVersion() {
      return this.version;
    }
  };
  const loop = new internal.ZLinkAutoConnectLoop({
    reconciler,
    local: local(framework.ZLinkLocationAutoConnectType.RouteMesh, framework.ZLinkLocationRole.Router, 'node-a', 'tcp://a'),
    changeStampStore: stampStore,
    leaseTracker
  });

  await loop.tick();
  await loop.tick();
  assert.equal(reconciler.ticks, 1);

  leaseTracker.version = 2;
  await loop.tick();
  assert.equal(reconciler.ticks, 2);

  stampStore.stamp = 2n;
  await loop.tick();
  assert.equal(reconciler.ticks, 3);
});

class SwitchablePeerResolver {
  constructor(inner) {
    this.inner = inner;
    this.fail = false;
  }

  async listLivePeers(filter, signal) {
    if (this.fail) {
      throw new Error('store unavailable');
    }
    return this.inner.listLivePeers(filter, signal);
  }
}

function runtimeFor(store, ownerId) {
  return new internal.ZLinkLocationRuntime({
    stores: stores(store),
    ownerId,
    now: () => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)),
    setTimer() {
      return 0;
    },
    clearTimer() {}
  });
}

function stores(store) {
  return {
    locationStore: store,
    peerStore: store,
    spotStore: store,
    actorStore: store,
    routeStore: store,
    ownerLeaseStore: store
  };
}

function executor(calls) {
  return {
    connect(target) {
      calls.push(`connect:${target.endpoint}:${target.ownerId}`);
      return true;
    },
    disconnect(target) {
      calls.push(`disconnect:${target.endpoint}:${target.ownerId}`);
    }
  };
}

function local(autoConnectType, role, nodeRid, endpoint) {
  return {
    autoConnectType,
    meshName: 'play',
    role,
    nodeRid: rid(nodeRid),
    endpoint
  };
}

function peer(ownerId, autoConnectType, role, nodeRid, endpoint) {
  return {
    autoConnectType,
    meshName: 'play',
    nodeRid: rid(nodeRid),
    role,
    endpoint,
    weight: 100,
    value: 0n,
    metadata: { endpoint },
    ownerId,
    generation: 0n,
    updatedAt: new Date(0)
  };
}

function rid(value) {
  return zlink.RoutingId.from(value);
}
