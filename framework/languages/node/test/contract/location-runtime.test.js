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
  assert.equal(await resolvers.resolveActorSpotAddress('player', 'missing'), undefined);
  assert.equal(events[2][0], 'actor-miss');
  assert.equal(events[2][1].actorId, 'missing');
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

  await nodeA.lifecycle.notifyActorJoinedSpot('player', 'actor-1', 'play', rid('spot-1'));
  await Promise.resolve();
  assert.equal(deactivated, 1);
  assert.equal(nodeA.lifecycle.ownsActor('player', 'actor-1'), false);

  await nodeA.lifecycle.releaseActor('player', 'actor-1');
  const row = await store.resolveActor({ actorType: 'player', actorId: 'actor-1' });
  assert.equal(row.ownerId, 'owner-b');
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
  let actorAddress = await resolvers.resolveActorSpotAddress('player', 'actor-1');
  assert.equal(actorAddress.meshName, 'play');
  assert.equal(actorAddress.nodeRid.toHex(), rid('node-a').toHex());
  assert.equal(actorAddress.spotRid.toHex(), rid('node-a').toHex());

  await node.lifecycle.notifyActorJoinedSpot('player', 'actor-1', 'play', rid('spot-1'));
  actorAddress = await resolvers.resolveActorSpotAddress('player', 'actor-1');
  assert.equal(actorAddress.meshName, 'play');
  assert.equal(actorAddress.spotRid.toHex(), rid('spot-1').toHex());

  const firstSpotAddress = await resolvers.resolveSpotAddress('play', rid('spot-1'));
  const secondSpotAddress = await resolvers.resolveSpotAddress('play', rid('spot-1'));
  assert.equal(firstSpotAddress.meshName, 'play');
  assert.equal(firstSpotAddress.nodeRid.toHex(), rid('node-a').toHex());
  assert.equal(secondSpotAddress.spotRid.toHex(), rid('spot-1').toHex());
  assert.equal(spotResolveCount, 2);

  const routeKey = rid('session-1').toHex();
  await node.lifecycle.bindActorSessionRoute(rid('session-1'), 'actor-1', rid('node-a'));
  const route = await resolvers.resolveRoute({ routeKind: framework.ZLinkRouteKind.ActorSession, routeKey });
  assert.equal(route.ownerNodeRid.toHex(), rid('node-a').toHex());

  await node.runtime.stop();
  assert.equal(await resolvers.resolveSpotAddress('play', rid('spot-1')), undefined);
  assert.equal(await resolvers.resolveActorSpotAddress('player', 'actor-1'), undefined);
  assert.equal(await resolvers.resolveRoute({ routeKind: framework.ZLinkRouteKind.ActorSession, routeKey }), undefined);
});

test('location spot remote address resolver bridges current spot-rid outbound API', async () => {
  const store = new internal.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const node = await lifecycleNode(store, 'owner-a', 'node-a', 'play');
  const resolver = new internal.ZLinkLocationSpotRemoteAddressResolver(resolversFor(store), ['play']);

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
      peerStore: store,
      spotStore: store,
      actorStore: store,
      routeStore: store,
      ownerLeaseStore: store
    },
    leaseTracker: new internal.ZLinkOwnerLeaseTracker({ store }),
    events
  });
}

function runtimeFor(store, options = {}) {
  const timers = [];
  return new internal.ZLinkLocationRuntime({
    stores: {
      peerStore: store,
      spotStore: store,
      actorStore: store,
      routeStore: store,
      ownerLeaseStore: options.ownerLeaseStore ?? store
    },
    ownerId: options.ownerId,
    events: options.events,
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
