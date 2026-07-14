const assert = require('node:assert/strict');
const test = require('node:test');
const zlink = require('@zlink-systems/zlink');
const framework = require('../../packages/framework/dist');
const internal = require('../../packages/framework/dist/internal');

test('location runtime renews owner lease, records store failure, and recovers', async () => {
  const store = new internal.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const leaseStore = new FlakyOwnerLeaseStore(store);
  const runtime = runtimeFor(store, { ownerLeaseStore: leaseStore });

  await runtime.start(rid('node-a'));
  assert.equal(runtime.ownerLeaseHealthy, true);
  assert.equal(runtime.lastError, undefined);

  leaseStore.fail = true;
  assert.equal(await runtime.renewOwnerLeaseOnce(), false);
  assert.equal(runtime.ownerLeaseHealthy, false);
  assert.match(runtime.lastError, /store unreachable/);

  leaseStore.fail = false;
  assert.equal(await runtime.renewOwnerLeaseOnce(), true);
  assert.equal(runtime.ownerLeaseHealthy, true);
  assert.equal(runtime.lastError, undefined);
});

test('location runtime stamps owner id, reports stale ownership, and removes rows on stop', async () => {
  const store = new internal.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const oldOwner = runtimeFor(store, { ownerId: 'owner-a' });
  const newOwner = runtimeFor(store, { ownerId: 'owner-b' });
  const lost = [];
  oldOwner.addOwnershipLostHandler((event) => lost.push(event));

  await oldOwner.start(rid('node-a'));
  await newOwner.start(rid('node-b'));

  const claimed = await oldOwner.writeActor(actor('ignored', 0n), framework.ZLinkLocationWriteIntent.NewClaim);
  assert.equal(claimed.status, framework.ZLinkLocationWriteStatus.Stored);
  assert.equal((await store.resolveActor({ actorType: 'player', actorId: 'actor-1' })).ownerId, 'owner-a');

  const takeover = await newOwner.writeActor(actor('ignored', 0n), framework.ZLinkLocationWriteIntent.Takeover);
  assert.equal(takeover.status, framework.ZLinkLocationWriteStatus.Stored);

  const stale = await oldOwner.writeActor(
    actor('ignored', claimed.generation),
    framework.ZLinkLocationWriteIntent.Renew
  );
  assert.equal(stale.status, framework.ZLinkLocationWriteStatus.IgnoredStale);
  assert.equal(lost.length, 1);
  assert.equal(lost[0].kind, framework.ZLinkLocationKind.Actor);

  await oldOwner.writeSpot(spot('ignored', 'spot-1'), framework.ZLinkLocationWriteIntent.NewClaim);
  await oldOwner.writePeer(peer('ignored'), framework.ZLinkLocationWriteIntent.NewClaim);
  await oldOwner.stop();

  const leases = await store.listOwnerLeases();
  assert.equal(leases.leases.some((lease) => lease.ownerId === 'owner-a'), false);
  assert.equal((await store.listPeers({ meshName: 'play' })).length, 0);
  assert.equal(await store.resolveSpot({ meshName: 'play', spotRid: rid('spot-1') }), undefined);
});

test('location runtime exposes owner cleanup failure instead of completing stop successfully', async () => {
  const store = new internal.ZLinkInMemoryLocationStore();
  const failingLocationStore = {
    async removeAllByOwner() { throw new Error('owner rows unavailable'); }
  };
  const runtime = runtimeFor(store, { locationStore: failingLocationStore });
  await runtime.start(rid('node-a'));

  await assert.rejects(() => runtime.stop(), /owner rows unavailable/);
  assert.match(runtime.lastError, /owner rows unavailable/);
});

test('location runtime emits row events and resolvers emit resolve misses', async () => {
  const store = new internal.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const events = [];
  const sink = {
    peerRowUpdated(key, row) { events.push(['peer-updated', key, row]); },
    peerRowRemoved(key) { events.push(['peer-removed', key]); },
    desiredSetChanged(change) { events.push(['desired', change]); },
    spotRowUpdated(key, row) { events.push(['spot-updated', key, row]); },
    spotRowRemoved(key) { events.push(['spot-removed', key]); },
    spotResolveMiss(key) { events.push(['spot-miss', key]); },
    actorRowUpdated(key, row) { events.push(['actor-updated', key, row]); },
    actorRowRemoved(key) { events.push(['actor-removed', key]); },
    actorResolveMiss(key) { events.push(['actor-miss', key]); },
    routeRowUpdated(key, row) { events.push(['route-updated', key, row]); },
    routeRowRemoved(key) { events.push(['route-removed', key]); },
    routeResolveMiss(key) { events.push(['route-miss', key]); }
  };
  const runtime = runtimeFor(store, { ownerId: 'owner-a', events: sink });

  await runtime.start(rid('node-a'));
  const claimed = await runtime.writeActor(actor('', 0n), framework.ZLinkLocationWriteIntent.NewClaim);
  assert.equal(events[0][0], 'actor-updated');
  assert.equal(events[0][2].ownerId, 'owner-a');
  assert.equal(events[0][2].generation, claimed.generation);

  await runtime.removeActor({ actorType: 'player', actorId: 'actor-1' }, claimed.generation);
  assert.equal(events[1][0], 'actor-removed');

  const resolvers = resolversFor(store, sink);
  assert.equal(await resolvers.resolveActorSpotHandle('missing'), undefined);
  assert.equal(events[2][0], 'actor-miss');
  assert.equal(events[2][1].actorId, 'missing');
});

test('location runtime applies listPageSize when callers omit a page size', async () => {
  const store = new internal.ZLinkInMemoryLocationStore();
  const pages = [];
  for (const methodName of ['listSpots', 'listActors', 'listRoutes']) {
    const original = store[methodName].bind(store);
    store[methodName] = (filter, page, signal) => {
      pages.push({ methodName, page });
      return original(filter, page, signal);
    };
  }
  const runtime = runtimeFor(store, { locationOptions: { listPageSize: 37 } });

  await runtime.listSpotLocations({});
  await runtime.listActorLocations({}, { continuationToken: 'next' });
  await runtime.listRouteLocations({}, { pageSize: 5 });

  assert.deepEqual(pages, [
    { methodName: 'listSpots', page: { pageSize: 37 } },
    { methodName: 'listActors', page: { continuationToken: 'next', pageSize: 37 } },
    { methodName: 'listRoutes', page: { pageSize: 5 } }
  ]);
});

test('location resolver owns actor placement and rotates eligible peers without RID sorting', async () => {
  const resolver = resolversFor(new internal.ZLinkInMemoryLocationStore());
  resolver.listLivePeers = async () => [
    { nodeRid: 'node-z', draining: false, capabilities: ['actor:Player'] },
    { nodeRid: 'node-a', draining: false, capabilities: ['actor:Player'] },
    { nodeRid: 'node-draining', draining: true, capabilities: ['actor:Player'] },
    { nodeRid: 'node-other', draining: false, capabilities: ['actor:Other'] }
  ];

  assert.equal(await resolver.selectActorPlacement('play', 'Player', 'node-source'), 'node-z');
  assert.equal(await resolver.selectActorPlacement('play', 'Player', 'node-source'), 'node-a');
  assert.equal(await resolver.selectActorPlacement('play', 'Player', 'node-z'), 'node-a');
});

test('location readiness returns false when ready state is missing or query fails', async () => {
  const ready = new internal.DefaultZLinkLocationReadiness({
    async listTopology(filter) {
      assert.equal(filter.meshName, 'play');
      assert.equal(filter.role, framework.ZLinkLocationRole.Router);
      assert.equal(filter.state, framework.ZLinkLocationTopologyState.Ready);
      return { items: [{ nodeRid: rid('node-a') }] };
    }
  });
  assert.equal(await ready.isPeerReady('play', framework.ZLinkLocationRole.Router, rid('node-a')), true);

  const notReady = new internal.DefaultZLinkLocationReadiness({
    async listTopology() {
      return { items: [] };
    }
  });
  assert.equal(await notReady.isPeerReady('play', framework.ZLinkLocationRole.Router), false);

  const unknown = new internal.DefaultZLinkLocationReadiness({
    async listTopology() {
      throw new Error('store unavailable');
    }
  });
  assert.equal(await unknown.isPeerReady('play', framework.ZLinkLocationRole.Router), false);
});

test('location lifecycle claims before activation and loser never activates', async () => {
  const store = new internal.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const nodeA = await lifecycleNode(store, 'owner-a', 'node-a');
  const nodeB = await lifecycleNode(store, 'owner-b', 'node-b');
  let activatedB = 0;

  const winner = await nodeA.lifecycle.executeActorClaimThenActivate(
    'player',
    'actor-1',
    rid('node-a'),
    undefined,
    async () => {
      const row = await store.resolveActor({ actorType: 'player', actorId: 'actor-1' });
      assert.equal(row.ownerId, 'owner-a');
      return 'instance-a';
    }
  );
  assert.equal(winner.activated, 'instance-a');
  assert.equal(winner.existingLocation, undefined);

  const loser = await nodeB.lifecycle.executeActorClaimThenActivate(
    'player',
    'actor-1',
    rid('node-b'),
    undefined,
    async () => {
      activatedB++;
      return 'instance-b';
    }
  );
  assert.equal(loser.activated, undefined);
  assert.equal(activatedB, 0);
  assert.equal(loser.existingLocation.nodeRid.toHex(), rid('node-a').toHex());
  assert.equal(loser.existingLocation.ownerId, 'owner-a');
});

test('location lifecycle already-owned claim does not activate a second instance', async () => {
  const store = new internal.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const node = await lifecycleNode(store, 'owner-a', 'node-a');

  const first = await node.lifecycle.executeActorClaimThenActivate(
    'player',
    'actor-1',
    rid('node-a'),
    undefined,
    async () => 'instance-a'
  );
  assert.equal(first.activated, 'instance-a');

  let secondActivated = 0;
  const second = await node.lifecycle.executeActorClaimThenActivate(
    'player',
    'actor-1',
    rid('node-a'),
    undefined,
    async () => {
      secondActivated += 1;
      throw new Error('duplicate activation');
    }
  );

  assert.equal(second.activated, undefined);
  assert.equal(second.existingLocation, undefined);
  assert.equal(secondActivated, 0);
  assert.equal(node.lifecycle.ownsActor('player', 'actor-1'), true);
  assert.notEqual(await store.resolveActor({ actorId: 'actor-1' }), undefined);
});

test('location lifecycle rolls failed activation back and renews actor spot state', async () => {
  const store = new internal.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const node = await lifecycleNode(store, 'owner-a', 'node-a');

  await assert.rejects(
    () => node.lifecycle.executeActorClaimThenActivate(
      'player',
      'actor-1',
      rid('node-a'),
      undefined,
      async () => {
        throw new Error('factory failed');
      }
    ),
    /factory failed/
  );
  assert.equal(await store.resolveActor({ actorType: 'player', actorId: 'actor-1' }), undefined);

  const claim = await node.lifecycle.claimActor('player', 'actor-1', rid('node-a'));
  assert.equal(claim.status, internal.ZLinkActorClaimStatus.Claimed);
  const claimed = await store.resolveActor({ actorType: 'player', actorId: 'actor-1' });

  await node.lifecycle.setActorRef('player', 'actor-1', 'ref-1');
  await node.lifecycle.notifyActorJoinedSpot('player', 'actor-1', 'play', rid('spot-1'));
  const joined = await store.resolveActor({ actorType: 'player', actorId: 'actor-1' });
  assert.equal(joined.actorRef, 'ref-1');
  assert.equal(joined.locationKind, framework.ZLinkSpotKind.User);
  assert.equal(joined.spotMeshName, 'play');
  assert.equal(joined.spotRid.toHex(), rid('spot-1').toHex());
  assert.equal(joined.generation, claimed.generation);

  await node.lifecycle.notifyActorLeftSpot('player', 'actor-1');
  const left = await store.resolveActor({ actorType: 'player', actorId: 'actor-1' });
  assert.equal(left.locationKind, framework.ZLinkSpotKind.Entry);
  assert.equal(left.spotRid, undefined);
  assert.equal(left.generation, claimed.generation);
});

test('location lifecycle deactivates stale hosted actor and protects new owner row', async () => {
  const store = new internal.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const nodeA = await lifecycleNode(store, 'owner-a', 'node-a');
  const nodeB = await lifecycleNode(store, 'owner-b', 'node-b');
  let deactivated = 0;

  const claim = await nodeA.lifecycle.claimActor('player', 'actor-1', rid('node-a'), async () => {
    deactivated++;
  });
  assert.equal(claim.status, internal.ZLinkActorClaimStatus.Claimed);

  const takeover = await nodeB.runtime.writeActor(actor('ignored', 0n), framework.ZLinkLocationWriteIntent.Takeover);
  assert.equal(takeover.status, framework.ZLinkLocationWriteStatus.Stored);

  await assert.rejects(
    () => nodeA.lifecycle.notifyActorJoinedSpot('player', 'actor-1', 'play', rid('spot-1')),
    /renewal.*rejected/i
  );
  await Promise.resolve();
  assert.equal(deactivated, 1);
  assert.equal(nodeA.lifecycle.ownsActor('player', 'actor-1'), false);

  await nodeA.lifecycle.releaseActor('player', 'actor-1');
  const row = await store.resolveActor({ actorType: 'player', actorId: 'actor-1' });
  assert.equal(row.ownerId, 'owner-b');
});

test('location lifecycle retries source actor cleanup without losing the tracked generation', async () => {
  const store = new internal.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const node = await lifecycleNode(store, 'owner-a', 'node-a');
  await node.lifecycle.claimActor('player', 'actor-retry', rid('node-a'));
  const removeActor = node.runtime.removeActor.bind(node.runtime);
  let attempts = 0;
  node.runtime.removeActor = async (...args) => {
    attempts += 1;
    if (attempts === 1) throw new Error('temporary store failure');
    return await removeActor(...args);
  };

  await node.lifecycle.releaseActorEventually('player', 'actor-retry');

  assert.equal(attempts, 2);
  assert.equal(node.lifecycle.ownsActor('player', 'actor-retry'), false);
  assert.equal(await store.resolveActor({ actorId: 'actor-retry' }), undefined);
});

test('location lifecycle claims spots and binds actor session routes with takeover', async () => {
  const store = new internal.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const nodeA = await lifecycleNode(store, 'owner-a', 'node-a');
  const nodeB = await lifecycleNode(store, 'owner-b', 'node-b');

  const spotStatus = await nodeA.lifecycle.claimSpot(
    'play',
    rid('spot-1'),
    'game',
    rid('node-a'),
    framework.ZLinkSpotKind.User
  );
  assert.equal(spotStatus, framework.ZLinkLocationWriteStatus.Stored);
  assert.equal((await store.resolveSpot({ meshName: 'play', spotRid: rid('spot-1') })).ownerId, 'owner-a');

  await nodeA.lifecycle.releaseSpot('play', rid('spot-1'));
  assert.equal(await store.resolveSpot({ meshName: 'play', spotRid: rid('spot-1') }), undefined);

  const sessionRid = rid('session-1');
  const routeKey = sessionRid.toHex();
  await nodeA.lifecycle.bindActorSessionRoute(sessionRid, 'actor-1', rid('node-a'));
  assert.equal(
    (await store.resolveRoute({ routeKind: framework.ZLinkRouteKind.ActorSession, routeKey })).ownerId,
    'owner-a'
  );

  await nodeB.lifecycle.bindActorSessionRoute(sessionRid, 'actor-1', rid('node-b'));
  const rebound = await store.resolveRoute({ routeKind: framework.ZLinkRouteKind.ActorSession, routeKey });
  assert.equal(rebound.ownerId, 'owner-b');
  assert.equal(rebound.ownerNodeRid.toHex(), rid('node-b').toHex());

  await nodeB.lifecycle.removeActorSessionRoute(sessionRid);
  assert.equal(await store.resolveRoute({ routeKind: framework.ZLinkRouteKind.ActorSession, routeKey }), undefined);
});

test('store location resolvers return live spot actor and route rows without cache', async () => {
  const store = new internal.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const node = await lifecycleNode(store, 'owner-a', 'node-a', 'play');
  const resolvers = resolversFor(store);
  let spotResolveCount = 0;
  const resolveSpot = store.resolveSpot.bind(store);
  store.resolveSpot = (key, signal) => {
    spotResolveCount++;
    return resolveSpot(key, signal);
  };

  assert.equal(
    await node.lifecycle.claimSpot('play', rid('spot-1'), 'game', rid('node-a'), framework.ZLinkSpotKind.User),
    framework.ZLinkLocationWriteStatus.Stored
  );
  await node.lifecycle.claimActor('player', 'actor-1', rid('node-a'));
  let actorAddress = await resolvers.resolveActorSpotHandle('actor-1');
  assert.equal(actorAddress, undefined);

  await node.lifecycle.setActorRef('player', 'actor-1', 'ref-1');
  await node.lifecycle.notifyActorJoinedSpot('player', 'actor-1', 'play', rid('spot-1'));
  actorAddress = await resolvers.resolveActorSpotHandle('actor-1');
  assert.equal(actorAddress.spotRid, String(rid('spot-1')));
  const actorTarget = await internal.resolveSpotHandle(actorAddress);
  assert.equal(actorTarget.meshName, 'play');
  assert.equal(actorTarget.spotKind, framework.ZLinkSpotKind.User);

  const firstSpotRef = await resolvers.resolveSpotHandle(rid('spot-1'));
  const secondSpotRef = await resolvers.resolveSpotHandle(rid('spot-1'));
  const firstTarget = await internal.resolveSpotHandle(firstSpotRef);
  assert.equal(firstTarget.meshName, 'play');
  assert.equal(firstTarget.nodeRid, String(rid('node-a')));
  assert.equal(firstTarget.spotKind, framework.ZLinkSpotKind.User);
  assert.equal(secondSpotRef.spotRid, String(rid('spot-1')));
  assert.equal(spotResolveCount, 3);

  await node.runtime.stop();
  assert.equal(await resolvers.resolveSpotHandle(rid('spot-1')), undefined);
  assert.equal(await resolvers.resolveActorSpotHandle('actor-1'), undefined);
});

test('store location resolver returns a live remote ActorRef', async () => {
  const store = new internal.ZLinkInMemoryLocationStore();
  const runtime = runtimeFor(store, { ownerId: 'owner-a' });
  await runtime.start(rid('node-a'));
  const actorRef = { nodeRid: rid('node-b'), actorId: 'alice', generation: 9n };
  await runtime.writeActor({
    ...actor('owner-a', 0n),
    actorId: 'alice',
    actorRef,
    nodeRid: rid('node-b')
  }, framework.ZLinkLocationWriteIntent.NewClaim);

  assert.deepEqual(await resolversFor(store).resolveActorRef('alice'), actorRef);
  await runtime.stop();
});

test('location spot route resolver bridges internal routed transport', async () => {
  const store = new internal.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const node = await lifecycleNode(store, 'owner-a', 'node-a', 'play');
  const resolver = new internal.ZLinkLocationSpotRouteResolver(resolversFor(store), ['play']);

  await node.lifecycle.claimSpot('play', rid('spot-1'), 'game', rid('node-a'), framework.ZLinkSpotKind.User);
  const address = await resolver.resolve(rid('spot-1'));
  assert.equal(address.routerChannelId, 'play');
  assert.equal(address.targetNodeRid.toHex(), rid('node-a').toHex());
  assert.equal(address.spotRid.toHex(), rid('spot-1').toHex());
  assert.equal(address.spotKind, framework.ZLinkSpotKind.User);

  await node.runtime.stop();
  await assert.rejects(
    () => resolver.resolve(rid('spot-1')),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.SpotRouteNotFound
  );
});

test('location spot route resolver resolves Entry Spots from live Spot peer rows', async () => {
  const store = new internal.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const runtime = runtimeFor(store, { ownerId: 'owner-a' });
  await runtime.start(rid('node-a'));
  await runtime.writePeer({
    autoConnectType: framework.ZLinkLocationAutoConnectType.SpotMesh,
    meshName: 'play',
    nodeRid: rid('node-b'),
    role: framework.ZLinkLocationRole.Spot,
    endpoint: 'tcp://127.0.0.1:5002',
    weight: 100,
    value: 7n,
    ownerId: 'owner-a',
    generation: 0n,
    updatedAt: new Date(0)
  }, framework.ZLinkLocationWriteIntent.NewClaim);
  await runtime.writePeer({
    autoConnectType: framework.ZLinkLocationAutoConnectType.SpotMesh,
    meshName: 'play',
    nodeRid: rid('node-a'),
    role: framework.ZLinkLocationRole.Spot,
    endpoint: 'tcp://127.0.0.1:5001',
    weight: 100,
    value: 7n,
    ownerId: 'owner-a',
    generation: 0n,
    updatedAt: new Date(0)
  }, framework.ZLinkLocationWriteIntent.NewClaim);
  const resolver = new internal.ZLinkLocationSpotRouteResolver(resolversFor(store), ['play']);

  const address = await resolver.resolve(rid('node-a'));
  assert.equal(address.routerChannelId, 'play');
  assert.equal(address.targetNodeRid.toHex(), rid('node-a').toHex());
  assert.equal(address.spotRid.toHex(), rid('node-a').toHex());
  assert.equal(address.spotKind, framework.ZLinkSpotKind.Entry);

  await runtime.stop();
});

async function lifecycleNode(store, ownerId, nodeRid, entryMeshName = '') {
  const runtime = runtimeFor(store, { ownerId });
  await runtime.start(rid(nodeRid));
  return {
    runtime,
    lifecycle: new internal.ZLinkLocationLifecycle(runtime, store, entryMeshName)
  };
}

function resolversFor(store, events) {
  return new internal.ZLinkStoreLocationResolvers({
    stores: {
      locationStore: store,
      peerStore: store,
      spotStore: store,
      actorStore: store,
      routeStore: store,
      ownerLeaseStore: store
    },
    leaseTracker: new internal.ZLinkOwnerLeaseTracker({ store }),
    events,
    spotMeshNames: ['play']
  });
}

function runtimeFor(store, options = {}) {
  const timers = [];
  return new internal.ZLinkLocationRuntime({
    stores: {
      locationStore: options.locationStore ?? store,
      peerStore: store,
      spotStore: store,
      actorStore: store,
      routeStore: store,
      ownerLeaseStore: options.ownerLeaseStore ?? store
    },
    ownerId: options.ownerId,
    events: options.events,
    options: options.locationOptions,
    now: () => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)),
    setTimer(callback, delayMs) {
      timers.push({ callback, delayMs });
      return timers.length - 1;
    },
    clearTimer() {}
  });
}

function rid(value) {
  return zlink.RoutingId.from(value);
}

function actor(ownerId, generation) {
  return {
    actorType: 'player',
    actorId: 'actor-1',
    actorRef: 'actor-ref',
    nodeRid: rid('node-1'),
    generation,
    locationKind: framework.ZLinkSpotKind.Entry,
    spotMeshName: '',
    spotRid: undefined,
    spotKind: framework.ZLinkSpotKind.Entry,
    ownerId,
    updatedAt: new Date(0)
  };
}

function peer(ownerId) {
  return {
    autoConnectType: framework.ZLinkLocationAutoConnectType.RouteMesh,
    meshName: 'play',
    nodeRid: rid('node-a'),
    role: framework.ZLinkLocationRole.Router,
    endpoint: 'tcp://127.0.0.1:5001',
    weight: 100,
    value: 7n,
    ownerId,
    generation: 0n,
    updatedAt: new Date(0)
  };
}

function spot(ownerId, spotRid) {
  return {
    meshName: 'play',
    spotRid: rid(spotRid),
    spotType: 'game',
    nodeRid: rid('node-a'),
    spotKind: framework.ZLinkSpotKind.User,
    ownerId,
    generation: 0n,
    updatedAt: new Date(0)
  };
}

class FlakyOwnerLeaseStore {
  constructor(inner) {
    this.inner = inner;
    this.fail = false;
  }

  renewOwnerLease(ownerId, nodeRid, leaseTtlMs, signal) {
    if (this.fail) {
      throw new Error('store unreachable');
    }
    return this.inner.renewOwnerLease(ownerId, nodeRid, leaseTtlMs, signal);
  }

  removeOwnerLease(ownerId, signal) {
    return this.inner.removeOwnerLease(ownerId, signal);
  }

  listOwnerLeases(signal) {
    return this.inner.listOwnerLeases(signal);
  }
}
