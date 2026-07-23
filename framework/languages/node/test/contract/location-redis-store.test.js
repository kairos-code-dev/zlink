const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const path = require('node:path');
const { createClient } = require('redis');
const zlink = require('@zlink-systems/zlink');
const framework = require('../../packages/framework/dist');
const internal = require('../../packages/framework/dist/internal');
const redisLocations = require('../../packages/framework-locations-redis/dist');

test('redis location store matches core write lease and change-stamp contracts', async (t) => {
  const fixture = await redisFixture();
  if (fixture === undefined) {
    t.skip('Redis is not reachable; set ZLINK_REDIS_TEST_ENDPOINT or run Redis on 127.0.0.1:16379/6379.');
    return;
  }

  const prefix = `zlink:node-location-redis:${process.pid}:${Date.now()}`;
  const store = new redisLocations.ZLinkRedisLocationStore({
    url: fixture.url,
    keyPrefix: prefix
  });

  try {
    const renewedOwnerA = await store.claimOwnerLease('owner-a', 30000);
    assert.equal(renewedOwnerA.kind, 'claimed');
    assert.equal(renewedOwnerA.leaseExpiresAt.getTime() > renewedOwnerA.storeNow.getTime(), true);
    const first = await store.updateSpot(
      spot('owner-a', 0n, 'spot-1', 'node-a'),
      framework.ZLinkLocationWriteIntent.NewClaim
    );
    assert.equal(first.status, framework.ZLinkLocationWriteStatus.Stored);
    assert.equal(first.generation, 1n);

    const conflict = await store.updateSpot(
      spot('owner-b', 0n, 'spot-1', 'node-b'),
      framework.ZLinkLocationWriteIntent.NewClaim
    );
    assert.equal(conflict.status, framework.ZLinkLocationWriteStatus.RejectedConflict);

    const renewed = await store.updateSpot(
      spot('owner-a', first.generation, 'spot-1', 'node-a'),
      framework.ZLinkLocationWriteIntent.Renew
    );
    assert.equal(renewed.status, framework.ZLinkLocationWriteStatus.Stored);
    assert.equal(renewed.generation, first.generation);

    const resolved = await store.resolveSpot({ meshName: 'play', spotRid: rid('spot-1') });
    assert.equal(resolved.ownerId, 'owner-a');
    assert.equal('generation' in resolved, false);
    assert.equal(resolved.ownerNodeRid.toHex(), rid('node-a').toHex());

    assert.equal(
      await store.getChangeStamp({ kind: framework.ZLinkLocationKind.Spot, meshName: 'play' }),
      2n
    );
    assert.equal(
      await store.getChangeStamp({ kind: framework.ZLinkLocationKind.Spot }),
      2n
    );

    await store.updatePeer(peer('owner-a', 'node-a'), framework.ZLinkLocationWriteIntent.NewClaim);
    await store.updateActor(actor('owner-a', 'node-a'), framework.ZLinkLocationWriteIntent.NewClaim);
    await store.updateRoute(route('owner-a', 'node-a'), framework.ZLinkLocationWriteIntent.NewClaim);

    assert.equal((await store.listPeers({ meshName: 'play' })).length, 1);
    assert.equal((await store.listActors({ actorType: 'player' })).items.length, 1);
    assert.equal((await store.listRoutes({ routeKind: framework.ZLinkRouteKind.ActorSession })).items.length, 1);
    assert.deepEqual(
      [...(await store.resolveRoute({ routeKind: framework.ZLinkRouteKind.ActorSession, routeKey: 'session-1' })).value],
      [1, 2, 3]
    );

    const ownerA = await store.readOwnerLease('owner-a');
    assert.equal(ownerA.kind, 'found');
    assert.deepEqual(ownerA.token, renewedOwnerA.token);

    assert.equal(await store.removeAllByOwner(renewedOwnerA.token), 4n);
    assert.equal(await store.resolveSpot({ meshName: 'play', spotRid: rid('spot-1') }), undefined);

    const renewedOwnerB = await store.claimOwnerLease('owner-b', 30000);
    assert.equal(renewedOwnerB.kind, 'claimed');
    assert.equal(renewedOwnerB.leaseExpiresAt.getTime() > renewedOwnerB.storeNow.getTime(), true);
    const claimedAfterRemove = await store.updateSpot(
      spot('owner-b', 0n, 'spot-1', 'node-b'),
      framework.ZLinkLocationWriteIntent.NewClaim
    );
    assert.equal(claimedAfterRemove.status, framework.ZLinkLocationWriteStatus.Stored);
    assert.equal(claimedAfterRemove.generation, 2n);
  } finally {
    await store.dispose();
    await cleanupPrefix(fixture.client, prefix);
    await fixture.client.quit();
  }
});

test('redis location store row json matches the shared Redis fixture', async (t) => {
  const fixture = await redisFixture();
  if (fixture === undefined) {
    t.skip('Redis is not reachable; set ZLINK_REDIS_TEST_ENDPOINT or run Redis on 127.0.0.1:16379/6379.');
    return;
  }

  const prefix = `zlink:node-location-fixture:${process.pid}:${Date.now()}`;
  const store = new redisLocations.ZLinkRedisLocationStore({
    url: fixture.url,
    keyPrefix: prefix
  });

  try {
    const expected = redisActorLocationFixture();
    await store.claimOwnerLease('actor-owner-a', 30000);
    await fixture.client.set(`${prefix}:gen:actor:${expected.key}`, '4');
    const result = await store.updateActor(
      exactActorLocation(),
      framework.ZLinkLocationWriteIntent.NewClaim
    );
    assert.equal(result.generation, 5n);
    assert.equal(
      await fixture.client.hGet(`${prefix}:row:${expected.kind}:${expected.key}`, 'json'),
      expected.hash.json
    );
    const roundTrip = await store.resolveActor({ meshName: 'game', actorId: 'actor-1' });
    assert.equal(roundTrip.meshName, 'game');
    assert.equal(roundTrip.ownerNodeGeneration, 7n);
    assert.equal(roundTrip.spotGeneration, 3n);
    assert.equal(roundTrip.membershipEpoch, 4n);
    assert.equal(roundTrip.actorRef.generation, 11n);
  } finally {
    await store.dispose();
    await cleanupPrefix(fixture.client, prefix);
    await fixture.client.quit();
  }
});

test('redis exact MeshNode descriptor fixture and Actor transfer authority are atomic', async (t) => {
  const fixture = await redisFixture();
  if (fixture === undefined) {
    t.skip('Redis is not reachable; set ZLINK_REDIS_TEST_ENDPOINT or run Redis on 127.0.0.1:16379/6379.');
    return;
  }
  const prefix = `zlink:node-location-exact:${process.pid}:${Date.now()}`;
  const store = new redisLocations.ZLinkRedisLocationStore({ url: fixture.url, keyPrefix: prefix });
  try {
    const meshFixture = redisFixtureRow('mesh-node-descriptor-v1.json');
    await store.claimOwnerLease('mesh-owner-a', 30_000);
    await fixture.client.set(`${prefix}:gen:mesh:${meshFixture.key}`, '4');
    const descriptorClaim = await store.updateMeshNode(
      exactMeshNodeDescriptor(),
      framework.ZLinkLocationWriteIntent.NewClaim
    );
    assert.equal(descriptorClaim.generation, 5n);
    assert.equal(
      await fixture.client.hGet(`${prefix}:row:mesh:${meshFixture.key}`, 'json'),
      meshFixture.hash.json
    );
    assert.equal((await store.listMeshNodes('game'))[0].lifecycleGeneration, 7n);

    const request = actorTransferRequest();
    const prepared = await store.prepareActorTransfer(request);
    assert.equal(prepared.status, 'stored');
    assert.equal(prepared.record.state, 'prepared');
    assert.equal((await store.prepareActorTransfer(request)).status, 'stored');
    assert.equal((await store.prepareActorTransfer({
      ...request,
      transferId: '11111111-1111-1111-1111-111111111111'
    })).status, 'rejectedConflict');
    assert.equal((await store.commitActorTransfer(
      request.meshName, request.actorId, request.transferId, 'wrong-owner'
    )).status, 'rejectedConflict');
    const committed = await store.commitActorTransfer(
      request.meshName, request.actorId, request.transferId, request.recoveryOwnerId
    );
    assert.equal(committed.record.state, 'committed');
    assert.equal((await store.resolveActorTransfer('game', 'actor-1')).state, 'committed');

    const transferFixture = JSON.parse(fs.readFileSync(path.resolve(
      __dirname,
      '../../../../testdata/location/redis/actor-transfer-v1.json'
    ), 'utf8'));
    const fixtureKey = transferFixture.key.replace(/^P:/, `${prefix}:`);
    const activeKey = transferFixture.activeIndex.key.replace(/^P:/, `${prefix}:`);
    assert.equal(await fixture.client.get(activeKey), transferFixture.activeIndex.value);
    const hash = await fixture.client.hGetAll(fixtureKey);
    for (const field of transferFixture.hashFields.slice(1, 7)) {
      assert.equal(hash[field], transferFixture.hash[field], field);
    }
    assert.ok(Number(hash.recoveryLeaseExpiresAtMs) >= Number(hash.updatedAtMs));
  } finally {
    await store.dispose();
    await cleanupPrefix(fixture.client, prefix);
    await fixture.client.quit();
  }
});

test('two runtime owners resume an expired Redis transfer authority without native tokens', async (t) => {
  const fixture = await redisFixture();
  if (fixture === undefined) {
    t.skip('Redis is not reachable; set ZLINK_REDIS_TEST_ENDPOINT or run Redis on 127.0.0.1:16379/6379.');
    return;
  }
  const prefix = `zlink:node-transfer-recovery:${process.pid}:${Date.now()}`;
  const store = new redisLocations.ZLinkRedisLocationStore({ url: fixture.url, keyPrefix: prefix });
  try {
    const source = exactActorLocation();
    await store.updateActor(source, framework.ZLinkLocationWriteIntent.NewClaim);
    const control = {
      kind: 'transferControl',
      phase: zlink.ActorTransferPhase.Fenced,
      role: zlink.ActorTransferRole.Target,
      transferId: { high: 0x0123456789abcdefn, low: 0x0123456789abcdefn },
      actor: {
        nodeRid: rid('game-b'),
        actorId: source.actorId,
        generation: source.actorRef.generation
      },
      membershipEpoch: source.membershipEpoch,
      finalSequence: 0n,
      resultCode: 0,
      failureErrno: 0
    };
    const failedOwner = new internal.ZLinkActorTransferAuthorityRuntime({
      store: () => store,
      recoveryOwnerId: () => 'failed-runtime',
      recoveryLeaseTtlMs: 200
    });
    await failedOwner.handle('game', control);

    const successor = new internal.ZLinkActorTransferAuthorityRuntime({
      store: () => store,
      recoveryOwnerId: () => 'successor-runtime',
      recoveryLeaseTtlMs: 30_000
    });
    await successor.handle('game', { ...control, phase: zlink.ActorTransferPhase.Activated });
    assert.equal((await store.resolveActorTransfer('game', source.actorId)).state, 'prepared');

    await new Promise((resolve) => setTimeout(resolve, 350));
    await successor.handle('game', { ...control, phase: zlink.ActorTransferPhase.Activated });
    assert.equal(await store.resolveActorTransfer('game', source.actorId), undefined);
  } finally {
    await store.dispose();
    await cleanupPrefix(fixture.client, prefix);
    await fixture.client.quit();
  }
});

test('redis routing-id slot allocation is atomic, idempotent, configured, and fenced', async (t) => {
  const fixture = await redisFixture();
  if (fixture === undefined) {
    t.skip('Redis is not reachable; set ZLINK_REDIS_TEST_ENDPOINT or run Redis on 127.0.0.1:16379/6379.');
    return;
  }
  const prefix = `zlink:node-routing-id-redis:${process.pid}:${Date.now()}`;
  const store = new redisLocations.ZLinkRedisLocationStore({ url: fixture.url, keyPrefix: prefix });
  const members = [
    { meshName: 'play', routingIdPrefix: 'play-' },
    { meshName: 'rooms', routingIdPrefix: 'rooms-' }
  ];
  const request = (ownerId) => ({
    groupName: 'game', members, slotCount: 2, ownerId, leaseTtlMs: 30_000
  });
  try {
    const first = await store.acquireRoutingIdSlot(request('owner-a'));
    assert.equal(first.kind, 'acquired');
    assert.equal(first.allocation.slot, 1);
    assert.equal((await store.acquireRoutingIdSlot(request('owner-a'))).allocation.slot, 1);
    const second = await store.acquireRoutingIdSlot(request('owner-b'));
    assert.equal(second.kind, 'acquired');
    assert.equal(second.allocation.slot, 2);
    assert.equal((await store.acquireRoutingIdSlot(request('owner-c'))).kind, 'groupExhausted');
    assert.equal((await store.acquireRoutingIdSlot({
      ...request('owner-c'), slotCount: 3
    })).kind, 'groupConfigurationMismatch');

    assert.equal(await store.releaseRoutingIdSlot('game', 1, {
      ownerId: 'owner-a', generation: first.allocation.owner.generation + 1n
    }), 'ignoredStale');
    assert.equal(await store.releaseRoutingIdSlot(
      'game', 1, first.allocation.owner
    ), 'released');
    const recycled = await store.acquireRoutingIdSlot(request('owner-c'));
    assert.equal(recycled.kind, 'acquired');
    assert.equal(recycled.allocation.slot, 1);
    assert.equal(recycled.allocation.owner.generation, 2n);

    const snapshot = await store.listRoutingIdSlots('game');
    assert.deepEqual(snapshot.members, members);
    assert.equal(snapshot.allocations.length, 2);
    assert.equal(
      await fixture.client.hGet(`${prefix}:ridalloc:game`, 'config'),
      JSON.stringify([
        { MeshName: 'play', RoutingIdPrefix: 'play-' },
        { MeshName: 'rooms', RoutingIdPrefix: 'rooms-' }
      ])
    );
  } finally {
    await store.dispose();
    await cleanupPrefix(fixture.client, prefix);
    await fixture.client.quit();
  }
});

test('redis resolver rejects an Actor row after the same SPOT RID is recreated', async (t) => {
  const fixture = await redisFixture();
  if (fixture === undefined) {
    t.skip('Redis is not reachable; set ZLINK_REDIS_TEST_ENDPOINT or run Redis on 127.0.0.1:16379/6379.');
    return;
  }
  const prefix = `zlink:node-location-stale:${process.pid}:${Date.now()}`;
  const store = new redisLocations.ZLinkRedisLocationStore({ url: fixture.url, keyPrefix: prefix });
  try {
    await store.claimOwnerLease('actor-owner-a', 30_000);
    await store.updateSpot({
      ...spot('actor-owner-a', 0n, 'spot-1', 'game-a'),
      meshName: 'game',
      spotGeneration: 3n,
      ownerNodeGeneration: 7n
    }, framework.ZLinkLocationWriteIntent.NewClaim);
    await store.updateActor(exactActorLocation(), framework.ZLinkLocationWriteIntent.NewClaim);
    const resolvers = new internal.ZLinkStoreLocationResolvers({
      stores: {
        locationStore: store,
        peerStore: store,
        spotStore: store,
        actorStore: store,
        routeStore: store,
        ownerLeaseStore: store
      },
      leaseTracker: new internal.ZLinkOwnerLeaseTracker({ store }),
      spotMeshNames: ['game']
    });

    assert.notEqual(await resolvers.resolveActorRef('actor-1'), undefined);

    await store.claimOwnerLease('actor-owner-b', 30_000);
    await store.updateSpot({
      ...spot('actor-owner-b', 0n, 'spot-1', 'game-b'),
      meshName: 'game',
      spotGeneration: 4n,
      ownerNodeGeneration: 8n
    }, framework.ZLinkLocationWriteIntent.Takeover);

    assert.equal(await resolvers.resolveActorRef('actor-1'), undefined);
    assert.equal(await resolvers.resolveActorSpotHandle('game', 'actor-1'), undefined);
  } finally {
    await store.dispose();
    await cleanupPrefix(fixture.client, prefix);
    await fixture.client.quit();
  }
});

test('redis location store validates required connection options', () => {
  assert.throws(
    () => new redisLocations.ZLinkRedisLocationStore({ url: 'redis://127.0.0.1:6379', keyPrefix: '' }),
    /keyPrefix/
  );
  assert.throws(
    () => new redisLocations.ZLinkRedisLocationStore({ keyPrefix: 'zlink:test' }),
    /requires url/
  );
});

async function redisFixture() {
  const candidates = [
    process.env.ZLINK_REDIS_TEST_ENDPOINT,
    '127.0.0.1:16379',
    '127.0.0.1:6379'
  ].filter(Boolean);
  for (const endpoint of candidates) {
    const url = endpoint.startsWith('redis://') ? endpoint : `redis://${endpoint}`;
    const client = createClient({
      url,
      socket: {
        connectTimeout: 300
      }
    });
    client.on('error', () => {});
    try {
      await Promise.race([
        client.connect(),
        new Promise((_, reject) => setTimeout(() => reject(new Error('redis probe timeout')), 500))
      ]);
      await client.ping();
      return { url, client };
    } catch {
      try {
        await client.disconnect();
      } catch {}
    }
  }
  return undefined;
}

async function cleanupPrefix(client, prefix) {
  let cursor = '0';
  do {
    const reply = await client.scan(cursor, {
      MATCH: `${prefix}:*`,
      COUNT: 100
    });
    cursor = String(reply.cursor);
    if (reply.keys.length > 0) {
      await client.del(reply.keys);
    }
  } while (cursor !== '0');
}

function rid(value) {
  return zlink.RoutingId.from(value);
}

function peer(ownerId, nodeRid) {
  return {
    autoConnectType: framework.ZLinkLocationAutoConnectType.RouteMesh,
    meshName: 'play',
    nodeRid: rid(nodeRid),
    role: framework.ZLinkLocationRole.Router,
    endpoint: 'tcp://127.0.0.1:5001',
    weight: 100,
    value: 7n,
    metadata: { 'route-endpoint': 'tcp://127.0.0.1:6001' },
    capabilities: ['router', 'route-bridge'],
    ownerId,
    generation: 0n,
    updatedAt: new Date(0)
  };
}

function spot(ownerId, _generation, spotRid, nodeRid) {
  return {
    meshName: 'play',
    spotRid: rid(spotRid),
    spotType: 'game',
    spotGeneration: 3n,
    ownerNodeRid: rid(nodeRid),
    ownerNodeGeneration: 7n,
    spotKind: framework.ZLinkSpotKind.User,
    ownerId,
    updatedAt: new Date(0)
  };
}

function actor(ownerId, nodeRid) {
  return {
    meshName: 'play',
    actorType: 'player',
    actorId: 'actor-1',
    actorRef: { nodeRid: rid(nodeRid), actorId: 'actor-1', generation: 1n },
    ownerNodeRid: rid(nodeRid),
    ownerNodeGeneration: 7n,
    spotRid: rid(nodeRid),
    spotGeneration: 7n,
    membershipEpoch: 1n,
    spotKind: framework.ZLinkSpotKind.Entry,
    ownerId,
    updatedAt: new Date(0)
  };
}

function unpublishedActor(ownerId, nodeRid) {
  return {
    actorType: null,
    actorId: 'actor-2',
    actorRef: null,
    spotRid: null,
    ownerId,
    updatedAt: new Date(0)
  };
}

function route(ownerId, nodeRid) {
  return {
    routeKind: framework.ZLinkRouteKind.ActorSession,
    routeKey: 'session-1',
    ownerNodeRid: rid(nodeRid),
    ownerId,
    generation: 0n,
    value: Uint8Array.from([1, 2, 3]),
    updatedAt: new Date(0)
  };
}

function fixtureRoute(ownerId, nodeRid) {
  return {
    ...route(ownerId, nodeRid),
    routeKey: 'route-1',
    value: Uint8Array.from([1, 2, 3, 4])
  };
}

function redisActorLocationFixture() {
  const fixturePath = path.resolve(__dirname, '../../../../testdata/location/redis/actor-location-v2.json');
  return JSON.parse(fs.readFileSync(fixturePath, 'utf8')).row;
}

function redisFixtureRow(fileName) {
  return JSON.parse(fs.readFileSync(path.resolve(
    __dirname,
    `../../../../testdata/location/redis/${fileName}`
  ), 'utf8')).row;
}

function exactMeshNodeDescriptor() {
  return {
    meshName: 'game',
    rid: rid('game-a'),
    lifecycleGeneration: 7n,
    descriptorRevision: 3n,
    endpoint: 'tcp://10.0.0.1:7300',
    channelWeights: { orders: 100, world: 50 },
    draining: false,
    securityIdentity: 'cluster-a',
    ownerId: 'mesh-owner-a',
    updatedAt: new Date('2024-07-15T00:00:00.000Z')
  };
}

function actorTransferRequest() {
  return {
    meshName: 'game',
    actorId: 'actor-1',
    transferId: '01234567-89ab-cdef-0123-456789abcdef',
    source: { nodeRid: rid('game-a'), actorId: 'actor-1', generation: 11n },
    target: { nodeRid: rid('game-b'), actorId: 'actor-1', generation: 11n },
    expectedActorGeneration: 11n,
    expectedMembershipEpoch: 4n,
    participants: new Set([rid('game-a'), rid('game-b')]),
    recoveryOwnerId: 'recovery-a',
    recoveryLeaseTtlMs: 30_000
  };
}

function exactActorLocation() {
  return {
    meshName: 'game',
    actorId: 'actor-1',
    actorType: 'player',
    actorRef: { nodeRid: rid('game-a'), actorId: 'actor-1', generation: 11n },
    ownerNodeRid: rid('game-a'),
    ownerNodeGeneration: 7n,
    spotRid: rid('spot-1'),
    spotGeneration: 3n,
    spotKind: framework.ZLinkSpotKind.User,
    membershipEpoch: 4n,
    ownerId: 'actor-owner-a',
    updatedAt: new Date('2024-07-15T00:00:00.000Z')
  };
}
