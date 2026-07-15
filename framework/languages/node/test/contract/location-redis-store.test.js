const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const path = require('node:path');
const { createClient } = require('redis');
const zlink = require('@zlink-systems/zlink');
const framework = require('../../packages/framework/dist');
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
    const renewedOwnerA = await store.renewOwnerLease('owner-a', rid('node-a'), 30000);
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
    assert.equal(resolved.generation, first.generation);
    assert.equal(resolved.nodeRid.toHex(), rid('node-a').toHex());

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
    await store.updateActor(unpublishedActor('owner-a', 'node-a'), framework.ZLinkLocationWriteIntent.NewClaim);
    await store.updateRoute(route('owner-a', 'node-a'), framework.ZLinkLocationWriteIntent.NewClaim);

    assert.equal((await store.listPeers({ meshName: 'play' })).length, 1);
    assert.equal((await store.listActors({ actorType: 'player' })).items.length, 1);
    const unresolvedActor = await store.resolveActor({ actorId: 'actor-2' });
    assert.equal(unresolvedActor.actorRef, undefined);
    assert.equal(unresolvedActor.actorType, undefined);
    assert.equal(unresolvedActor.spotRid, undefined);
    const rawActorJson = JSON.parse(await fixture.client.hGet(`${prefix}:row:actor:7:actor-2`, 'json'));
    assert.equal(rawActorJson.ActorRef, null);
    assert.equal(rawActorJson.ActorType, null);
    assert.equal(rawActorJson.SpotRid, null);
    assert.equal((await store.listRoutes({ routeKind: framework.ZLinkRouteKind.ActorSession })).items.length, 1);
    assert.deepEqual(
      [...(await store.resolveRoute({ routeKind: framework.ZLinkRouteKind.ActorSession, routeKey: 'session-1' })).value],
      [1, 2, 3]
    );

    const leases = await store.listOwnerLeases();
    assert.equal(leases.leases.length, 1);
    assert.equal(leases.leases[0].ownerId, 'owner-a');
    assert.equal(leases.leases[0].nodeRid.toHex(), rid('node-a').toHex());

    assert.equal(await store.removeAllByOwner('owner-a'), 5);
    assert.equal(await store.resolveSpot({ meshName: 'play', spotRid: rid('spot-1') }), undefined);

    const renewedOwnerB = await store.renewOwnerLease('owner-b', rid('node-b'), 30000);
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
    await store.renewOwnerLease('owner-a', rid('node-1'), 30000);
    await store.updateActor(actor('owner-a', 'node-1'), framework.ZLinkLocationWriteIntent.NewClaim);
    await store.updatePeer(peer('owner-a', 'node-1'), framework.ZLinkLocationWriteIntent.NewClaim);
    await store.updateSpot(spot('owner-a', 0n, 'spot-1', 'node-1'), framework.ZLinkLocationWriteIntent.NewClaim);
    await store.updateRoute(fixtureRoute('owner-a', 'node-1'), framework.ZLinkLocationWriteIntent.NewClaim);

    const expectedRows = redisLocationFixtureRows();
    for (const row of expectedRows) {
      assert.equal(
        await fixture.client.hGet(`${prefix}:row:${row.kind}:${row.key}`, 'json'),
        row.hash.json,
        `${row.kind}:${row.key}`
      );
    }
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
    { channelName: 'play', routingIdPrefix: 'play-' },
    { channelName: 'rooms', routingIdPrefix: 'rooms-' }
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
        { ChannelName: 'play', RoutingIdPrefix: 'play-' },
        { ChannelName: 'rooms', RoutingIdPrefix: 'rooms-' }
      ])
    );
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

function spot(ownerId, generation, spotRid, nodeRid) {
  return {
    meshName: 'play',
    spotRid: rid(spotRid),
    spotType: 'game',
    nodeRid: rid(nodeRid),
    spotKind: framework.ZLinkSpotKind.User,
    ownerId,
    generation,
    updatedAt: new Date(0)
  };
}

function actor(ownerId, nodeRid) {
  return {
    actorType: 'player',
    actorId: 'actor-1',
    actorRef: { nodeRid: rid(nodeRid), actorId: 'actor-1', generation: 1n },
    nodeRid: rid(nodeRid),
    generation: 0n,
    locationKind: framework.ZLinkSpotKind.Entry,
    spotMeshName: 'play',
    spotRid: undefined,
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
    nodeRid: rid(nodeRid),
    generation: 0n,
    locationKind: framework.ZLinkSpotKind.Entry,
    spotMeshName: 'play',
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

function redisLocationFixtureRows() {
  const fixturePath = path.resolve(__dirname, '../../../../testdata/location/redis/actor-location-v2.json');
  return JSON.parse(fs.readFileSync(fixturePath, 'utf8')).rows;
}
