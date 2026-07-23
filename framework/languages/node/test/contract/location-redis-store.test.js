const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');
const { createClient } = require('redis');
const zlink = require('@zlink-systems/zlink');
const framework = require('../../packages/framework/dist');
const internal = require('../../packages/framework/dist/internal');
const serviceContracts = require(
  '../../packages/framework/dist/runtime/foundation/service-runtime-contracts'
);
const redisLocations = require('../../packages/framework-locations-redis/dist');

test('redis authority fixture fixes the cross-language hybrid encoding', () => {
  const fixture = redisSemanticFixture('authority-store-v1.json');
  const capacity = fixture.capacityBuckets;
  const segment = value =>
    `${Buffer.byteLength(String(value), 'utf8')}:${String(value)}`;
  const node = segment(capacity.descriptorKey)
    + segment(capacity.descriptorLifecycleGeneration);
  assert.equal(fixture.format, 'location-authority-hybrid-v1');
  assert.equal(capacity.segmentLengthUnit, 'UTF-8 bytes');
  assert.equal(capacity.node, node);
  assert.equal(
    capacity.type,
    node + segment(capacity.objectKind) + segment(capacity.stableType)
  );
  assert.equal(
    capacity.unicodeType,
    node + segment(capacity.objectKind) + segment(capacity.unicodeStableType)
  );
  const descriptorFixture = redisSemanticFixture('mesh-node-descriptor-v1.json');
  const descriptorPayload = JSON.parse(descriptorFixture.row.hash.json);
  const immutableDigest = descriptorFixture.immutableDigest;
  const immutableSegment = value =>
    `${Buffer.byteLength(String(value), 'utf8')}:${String(value)}`;
  assert.equal(
    immutableDigest.preimage,
    immutableDigest.segments.map(immutableSegment).join('')
  );
  assert.equal(
    immutableDigest.sha256LowerHex,
    createHash('sha256').update(immutableDigest.preimage, 'utf8').digest('hex')
  );
  assert.equal(
    descriptorFixture.physicalKeys.ownerTokenSha256,
    createHash('sha256')
      .update(`${descriptorPayload.OwnerId}\0${descriptorPayload.LeaseGeneration}`, 'utf8')
      .digest('hex')
  );
  assert.deepEqual(
    [...new Set(fixture.currentHashFields)].sort(),
    fixture.currentHashFields.slice().sort()
  );
});

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

test('redis legacy Actor discovery row remains round-trippable', async (t) => {
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
    await store.claimOwnerLease('actor-owner-a', 30000);
    const rowKey = '4:game7:actor-1';
    await fixture.client.set(`${prefix}:gen:actor:${rowKey}`, '4');
    const result = await store.updateActor(
      exactActorLocation(),
      framework.ZLinkLocationWriteIntent.NewClaim
    );
    assert.equal(result.generation, 5n);
    assert.equal(
      JSON.parse(await fixture.client.hGet(`${prefix}:row:actor:${rowKey}`, 'json')).ActorId,
      'actor-1'
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
    const meshLease = await store.claimOwnerLease('mesh-owner-a', 30_000);
    const descriptorClaim = await store.updateMeshNode(
      exactMeshNodeDescriptor('game-a', 'mesh-owner-a', meshLease.token.leaseGeneration),
      framework.ZLinkLocationWriteIntent.NewClaim
    );
    assert.equal(descriptorClaim.generation, 1n);
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

    const fixtureKey = `${prefix}:transfer:4:game7:actor-1:${request.transferId}`;
    const activeKey = `${prefix}:transfer-by-actor:4:game7:actor-1`;
    assert.equal(await fixture.client.get(activeKey), request.transferId);
    const hash = await fixture.client.hGetAll(fixtureKey);
    assert.equal(hash.state, 'Committed');
    assert.equal(JSON.parse(hash.source).actorId, 'actor-1');
    assert.equal(JSON.parse(hash.target).actorId, 'actor-1');
    assert.ok(Number(hash.recoveryLeaseExpiresAtMs) >= Number(hash.updatedAtMs));
  } finally {
    await store.dispose();
    await cleanupPrefix(fixture.client, prefix);
    await fixture.client.quit();
  }
});

test('redis provider atomically fences creation, relocation capacity, and aggregate authority', async (t) => {
  const fixture = await redisFixture();
  if (fixture === undefined) {
    t.skip('Redis is not reachable; set ZLINK_REDIS_TEST_ENDPOINT or run Redis on 127.0.0.1:16379/6379.');
    return;
  }
  const prefix = `zlink:node-authority-exact:${process.pid}:${Date.now()}`;
  const storeA = new redisLocations.ZLinkRedisLocationStore({ url: fixture.url, keyPrefix: prefix });
  const storeB = new redisLocations.ZLinkRedisLocationStore({ url: fixture.url, keyPrefix: prefix });
  try {
    const sourceLease = await storeA.claimOwnerLease('source-owner', 30_000);
    const targetLease = await storeA.claimOwnerLease('target-owner', 30_000);
    assert.equal(sourceLease.kind, 'claimed');
    assert.equal(targetLease.kind, 'claimed');
    const sourceLeaseDigest = createHash('sha256')
      .update('source-owner', 'utf8').digest('hex');
    assert.deepEqual(
      Object.keys(await fixture.client.hGetAll(
        `${prefix}:{zlink-location-v1}:owner-lease:${sourceLeaseDigest}`
      )).sort(),
      redisSemanticFixture('mesh-node-descriptor-v1.json')
        .ownerLeaseHashFields.sort()
    );
    const sourceDescriptor = exactMeshNodeDescriptor(
      'source-node', 'source-owner', sourceLease.token.leaseGeneration
    );
    const targetDescriptor = exactMeshNodeDescriptor(
      'target-node', 'target-owner', targetLease.token.leaseGeneration
    );
    const immutableFixtureDescriptor = {
      ...exactMeshNodeDescriptor(
        'game-a',
        'source-owner',
        sourceLease.token.leaseGeneration
      ),
      objectRole: framework.ZLinkObjectRole.None,
      objectCapacity: {
        activeObjects: 0,
        pendingActivations: 0,
        maxActiveObjects: 10_000,
        maxPendingActivations: 128
      },
      applicationVersion: 0n,
      spotTypes: [],
      objectCapabilities: []
    };
    assert.equal((await storeA.updateMeshNode(
      immutableFixtureDescriptor,
      framework.ZLinkLocationWriteIntent.NewClaim
    )).status, framework.ZLinkLocationWriteStatus.Stored);
    const immutableFixture = redisSemanticFixture(
      'mesh-node-descriptor-v1.json');
    const immutableAdmissionKey =
      `${prefix}:{zlink-location-v1}:descriptor-admission:mesh:`
      + immutableFixture.physicalKeys.descriptorKeySha256;
    assert.equal(
      (await fixture.client.hGetAll(immutableAdmissionKey)).immutableDigest,
      immutableFixture.immutableDigest.sha256LowerHex
    );
    assert.equal((await storeA.updateMeshNode(
      sourceDescriptor, framework.ZLinkLocationWriteIntent.NewClaim
    )).status, framework.ZLinkLocationWriteStatus.Stored);
    assert.equal((await storeA.updateMeshNode(
      targetDescriptor, framework.ZLinkLocationWriteIntent.NewClaim
    )).status, framework.ZLinkLocationWriteStatus.Stored);
    const descriptorCanonicalKey =
      `4:game${rid('target-node').toHex().toLowerCase().length}:${rid('target-node').toHex().toLowerCase()}`;
    const descriptorHash = createHash('sha256')
      .update(descriptorCanonicalKey, 'utf8').digest('hex');
    const descriptorRedisKey =
      `${prefix}:{zlink-location-v1}:descriptor:mesh:${descriptorHash}`;
    assert.deepEqual(
      Object.keys(await fixture.client.hGetAll(descriptorRedisKey)).sort(),
      ['gen', 'json', 'mesh', 'owner', 'updatedAtMs']
    );
    const admissionRedisKey =
      `${prefix}:{zlink-location-v1}:descriptor-admission:mesh:${descriptorHash}`;
    assert.deepEqual(
      Object.keys(await fixture.client.hGetAll(admissionRedisKey)).sort(),
      redisSemanticFixture('mesh-node-descriptor-v1.json')
        .admissionHashFields.sort()
    );
    const targetOwnerTokenDigest = createHash('sha256')
      .update(`target-owner\0${targetLease.token.leaseGeneration}`, 'utf8')
      .digest('hex');
    assert.equal(
      (await fixture.client.sMembers(
        `${prefix}:{zlink-location-v1}:descriptor:mesh:index`
      )).includes(descriptorCanonicalKey),
      true
    );
    assert.equal(
      (await fixture.client.sMembers(
        `${prefix}:{zlink-location-v1}:descriptor:mesh:owner:${targetOwnerTokenDigest}`
      )).includes(descriptorCanonicalKey),
      true
    );
    assert.equal((await storeA.updateMeshNode({
      ...targetDescriptor,
      descriptorRevision: 4n,
      endpoint: 'tcp://immutable-change:7300'
    }, framework.ZLinkLocationWriteIntent.Renew)).status,
    framework.ZLinkLocationWriteStatus.IgnoredStale);
    assert.equal((await storeA.updateMeshNode({
      ...targetDescriptor,
      descriptorRevision: 4n,
      placementWeight: 50,
      state: framework.ZLinkFrameworkRuntimeState.Draining
    }, framework.ZLinkLocationWriteIntent.Renew)).status,
    framework.ZLinkLocationWriteStatus.Stored);
    assert.equal((await storeA.updateMeshNode({
      ...targetDescriptor,
      descriptorRevision: 5n,
      placementWeight: 50
    }, framework.ZLinkLocationWriteIntent.Renew)).status,
    framework.ZLinkLocationWriteStatus.Stored);

    const creation = {
      key: { kind: 'instance_spot', globalId: 'instance-1' },
      intent: {
        stableType: 'room',
        placementProfile: 'default',
        requestContentReference: 'root-1',
        requestSha256: Buffer.alloc(32, 1),
        requestEncodedSize: 10n
      },
      target: {
        meshName: 'game',
        nodeRid: sourceDescriptor.rid,
        nodeLifecycleGeneration: sourceDescriptor.lifecycleGeneration,
        owner: sourceLease.token
      },
      creatingPayload: Buffer.from('creating'),
      pendingCapacityDelta: 1
    };
    const reserved = await storeA.reserve(creation);
    assert.equal(reserved.kind, 'reserved');
    const duplicate = await storeB.reserve(creation);
    assert.equal(duplicate.kind, 'conflict');
    const ready = await storeA.commit({
      key: creation.key,
      reservationId: reserved.reservationId,
      expectedStoreVersion: reserved.creating.storeVersion.value,
      target: creation.target,
      readyPayload: Buffer.from('ready')
    });
    assert.equal(ready.kind, 'committed');

    const secondCreation = {
      ...creation,
      key: { kind: 'instance_spot', globalId: 'instance-2' }
    };
    const secondReserved = await storeA.reserve(secondCreation);
    const secondReady = await storeA.commit({
      key: secondCreation.key,
      reservationId: secondReserved.reservationId,
      expectedStoreVersion: secondReserved.creating.storeVersion.value,
      target: secondCreation.target,
      readyPayload: Buffer.from('ready-2')
    });
    assert.equal(secondReady.kind, 'committed');
    assert.equal(await storeA.releaseOwnerLease(sourceLease.token), 'released');

    const relocationRequest = {
      reservationId: '11111111-1111-4111-8111-111111111111',
      authorityKey: { value: 'instance_spot:instance-1' },
      expectedStoreVersion: ready.ready.storeVersion,
      objectKind: 'instance_spot',
      stableType: 'room',
      sourceDescriptor: { meshName: 'game', rid: sourceDescriptor.rid },
      sourceNodeLifecycleGeneration: sourceDescriptor.lifecycleGeneration,
      sourceOwner: sourceLease.token,
      targetDescriptor: { meshName: 'game', rid: targetDescriptor.rid },
      targetNodeLifecycleGeneration: targetDescriptor.lifecycleGeneration,
      targetOwner: targetLease.token,
      capacityDelta: 1
    };
    const capacity = await storeA.reserveRelocationCapacity(relocationRequest);
    assert.equal(capacity.kind, 'reserved');
    assert.equal((await storeB.reserveRelocationCapacity(relocationRequest)).kind, 'alreadyReserved');
    const moved = await storeB.compareExchangeAuthority(
      relocationRequest.authorityKey,
      ready.ready.storeVersion,
      {
        kind: 'put',
        payload: Buffer.from('moved'),
        generationTransition: 'newOwner',
        targetOwner: targetLease.token,
        relocationCapacityFence: capacity.fence
      }
    );
    assert.equal(moved.kind, 'stored');
    assert.equal(moved.ownerId, 'target-owner');
    assert.equal(moved.objectGeneration, ready.ready.objectGeneration);
    const secondKey = { value: 'instance_spot:instance-2' };
    const secondCapacity = await storeA.reserveRelocationCapacity({
      ...relocationRequest,
      reservationId: '22222222-2222-4222-8222-222222222222',
      authorityKey: secondKey,
      expectedStoreVersion: secondReady.ready.storeVersion
    });
    const aggregate = await storeA.prepareAggregate({
      aggregateId: { value: '33333333-3333-4333-8333-333333333333' },
      aggregateGeneration: 1n,
      participants: [{
        authorityKey: secondKey,
        expectedStoreVersion: secondReady.ready.storeVersion,
        ownerTransition: 'newOwner',
        authorityPayload: Buffer.from('aggregate-ready'),
        membershipMutation: Buffer.from('membership')
      }],
      inventoryDigest: Buffer.alloc(32, 2),
      targetReservations: [secondCapacity.fence],
      targetOwner: targetLease.token
    });
    assert.equal(aggregate.kind, 'prepared');
    assert.equal((await storeB.commitAggregate(aggregate.fence)).kind, 'committed');
    assert.equal((await storeA.commitAggregate(aggregate.fence)).kind, 'alreadyCommitted');
    const authorityKey = 'instance_spot:instance-1';
    const authorityHash = createHash('sha256')
      .update(authorityKey, 'utf8').digest('hex');
    const authorityRedisKey =
      `${prefix}:{zlink-location-v1}:authority:current:${authorityHash}`;
    const physicalAuthority = await fixture.client.hGetAll(authorityRedisKey);
    assert.deepEqual(
      Object.keys(physicalAuthority).sort(),
      redisSemanticFixture('authority-store-v1.json').currentHashFields.sort()
    );
    assert.equal(physicalAuthority.authorityKey, authorityKey);
    assert.equal(physicalAuthority.allocationState, 'active');
    assert.equal(physicalAuthority.objectKind, 'instance_spot');
    assert.notEqual(physicalAuthority.descriptorKey, '');
    assert.equal(await fixture.client.pTTL(authorityRedisKey), -1);
    assert.equal(await fixture.client.exists(`${prefix}:authority-state`), 0);
    assert.deepEqual(
      await fixture.client.hGetAll(
        `${prefix}:{zlink-location-v1}:schema`
      ),
      {
        format: 'location-authority-hybrid-v1',
        epoch: '1'
      }
    );

    const firstPage = await storeA.listAuthorities('instance_spot:', undefined, 1);
    assert.equal(firstPage.kind, 'page');
    assert.equal(firstPage.items.length, 1);
    assert.notEqual(firstPage.nextCursor, undefined);
    const secondBefore = await storeA.readAuthority(secondKey);
    const preserved = await storeA.compareExchangeAuthority(
      secondKey,
      secondBefore.storeVersion,
      {
        kind: 'put',
        payload: Buffer.from('after-watermark'),
        generationTransition: 'preserve'
      }
    );
    assert.equal(preserved.kind, 'stored');
    const secondAuthorityHash = createHash('sha256')
      .update(secondKey.value, 'utf8').digest('hex');
    const historyRevision = BigInt(secondBefore.storeVersion.value)
      .toString(16).padStart(16, '0');
    const physicalHistory = await fixture.client.hGetAll(
      `${prefix}:{zlink-location-v1}:authority:history:${secondAuthorityHash}`
    );
    for (const suffix of redisSemanticFixture('authority-store-v1.json')
      .historyEncoding.fullSnapshotSuffixes) {
      assert.equal(
        Object.hasOwn(physicalHistory, `${historyRevision}:${suffix}`),
        true
      );
    }
    assert.equal(physicalHistory[`${historyRevision}:deleted`], '0');
    const secondPage = await storeA.listAuthorities(
      'instance_spot:', firstPage.nextCursor, 1
    );
    assert.equal(secondPage.kind, 'page');
    assert.equal(secondPage.items.length, 1);
    assert.equal(
      Buffer.from(secondPage.items[0].snapshot.payload).toString(),
      'aggregate-ready'
    );
    assert.equal(
      (await storeA.listAuthorities(
        'instance_spot:', firstPage.nextCursor, 1
      )).kind,
      'scanExpired'
    );
    const projectedTarget = (await storeA.listMeshNodes('game'))
      .find(node => node.ownerId === 'target-owner');
    assert.equal(projectedTarget.objectCapacity.activeObjects, 2);
    assert.equal(projectedTarget.objectCapacity.pendingActivations, 0);
    const targetNodeBucket =
      `${Buffer.byteLength(descriptorCanonicalKey, 'utf8')}:${descriptorCanonicalKey}1:7`;
    const targetTypeBucket = `${targetNodeBucket}13:instance_spot4:room`;
    assert.equal(
      await fixture.client.hGet(
        `${prefix}:{zlink-location-v1}:capacity:node:active`,
        targetNodeBucket
      ),
      '2'
    );
    assert.equal(
      await fixture.client.hGet(
        `${prefix}:{zlink-location-v1}:capacity:type:active`,
        targetTypeBucket
      ),
      '2'
    );
  } finally {
    await storeA.dispose();
    await storeB.dispose();
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
      phase: serviceContracts.ActorTransferPhase.Fenced,
      role: serviceContracts.ActorTransferRole.Target,
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
    await successor.handle('game', { ...control, phase: serviceContracts.ActorTransferPhase.Activated });
    assert.equal((await store.resolveActorTransfer('game', source.actorId)).state, 'prepared');

    await new Promise((resolve) => setTimeout(resolve, 350));
    await successor.handle('game', { ...control, phase: serviceContracts.ActorTransferPhase.Activated });
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
      ownerId: 'owner-a',
      leaseGeneration: first.allocation.owner.leaseGeneration + 1n
    }), 'ignoredStale');
    assert.equal(await store.releaseRoutingIdSlot(
      'game', 1, first.allocation.owner
    ), 'released');
    const recycled = await store.acquireRoutingIdSlot(request('owner-c'));
    assert.equal(recycled.kind, 'acquired');
    assert.equal(recycled.allocation.slot, 1);
    assert.equal(recycled.allocation.owner.leaseGeneration, 2n);

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

test('redis ClientServer descriptors enforce revision, lifecycle takeover, paging, and owner cleanup', async (t) => {
  const fixture = await redisFixture();
  if (fixture === undefined) {
    t.skip('Redis is not reachable; set ZLINK_REDIS_TEST_ENDPOINT or run Redis on 127.0.0.1:16379/6379.');
    return;
  }
  const prefix = `zlink:node-location-client-server:${process.pid}:${Date.now()}`;
  const store = new redisLocations.ZLinkRedisLocationStore({ url: fixture.url, keyPrefix: prefix });
  try {
    const ownerA = await store.claimOwnerLease('channel-owner-a', 30_000);
    assert.equal(ownerA.kind, 'claimed');
    const initial = clientServerDescriptor(
      'orders-a',
      'channel-owner-a',
      ownerA.token.leaseGeneration
    );
    const claimed = await store.updateClientServer(
      initial,
      framework.ZLinkLocationWriteIntent.NewClaim
    );
    assert.equal(claimed.status, framework.ZLinkLocationWriteStatus.Stored);
    assert.equal(claimed.generation, 1n);

    const staleRevision = await store.updateClientServer(
      { ...initial, descriptorRevision: 2n },
      framework.ZLinkLocationWriteIntent.Renew
    );
    assert.equal(staleRevision.status, framework.ZLinkLocationWriteStatus.IgnoredStale);
    const changedImmutable = await store.updateClientServer(
      { ...initial, descriptorRevision: 4n, endpoint: 'tcp://10.0.0.2:7499' },
      framework.ZLinkLocationWriteIntent.Renew
    );
    assert.equal(changedImmutable.status, framework.ZLinkLocationWriteStatus.IgnoredStale);
    const renewed = await store.updateClientServer(
      { ...initial, descriptorRevision: 4n, weight: 50 },
      framework.ZLinkLocationWriteIntent.Renew
    );
    assert.equal(renewed.status, framework.ZLinkLocationWriteStatus.Stored);
    assert.equal(renewed.generation, claimed.generation);

    const fixtureContract = redisSemanticFixture('client-server-server-descriptor-v1.json');
    const canonicalKey = fixtureContract.row.key;
    const rowDigest = createHash('sha256').update(canonicalKey, 'utf8').digest('hex');
    const physicalKey = `${prefix}:{zlink-location-v1}:descriptor:client-server:${rowDigest}`;
    const storedHash = await fixture.client.hGetAll(physicalKey);
    assert.deepEqual(Object.keys(storedHash).sort(), fixtureContract.hashFields.slice().sort());
    assert.equal(storedHash.owner, initial.ownerId);
    assert.equal(storedHash.channel, initial.channelName);
    const storedJson = JSON.parse(storedHash.json);
    const expectedJson = JSON.parse(fixtureContract.row.hash.json);
    assert.deepEqual(storedJson, {
      ...expectedJson,
      DescriptorRevision: 4,
      Weight: 50,
      OwnerLeaseGeneration: Number(ownerA.token.leaseGeneration)
    });

    const second = clientServerDescriptor(
      'orders-b',
      'channel-owner-a',
      ownerA.token.leaseGeneration
    );
    assert.equal(
      (await store.updateClientServer(
        second,
        framework.ZLinkLocationWriteIntent.NewClaim
      )).status,
      framework.ZLinkLocationWriteStatus.Stored
    );
    const otherChannel = {
      ...clientServerDescriptor(
        'billing-a',
        'channel-owner-a',
        ownerA.token.leaseGeneration
      ),
      channelName: 'billing'
    };
    assert.equal(
      (await store.updateClientServer(
        otherChannel,
        framework.ZLinkLocationWriteIntent.NewClaim
      )).status,
      framework.ZLinkLocationWriteStatus.Stored
    );

    const listed = [];
    let continuationToken;
    do {
      const page = await store.listClientServers('orders', {
        pageSize: 1,
        continuationToken
      });
      assert.equal(page.items.length <= 1, true);
      listed.push(...page.items);
      continuationToken = page.continuationToken;
    } while (continuationToken !== undefined);
    assert.deepEqual(
      listed.map(row => row.serverRid.toHex()).sort(),
      [rid('orders-a').toHex(), rid('orders-b').toHex()].sort()
    );
    await assert.rejects(
      store.listClientServers('billing', {
        pageSize: 1,
        continuationToken: Buffer.from(JSON.stringify({
          kind: 'client-server-v1',
          channelName: 'orders',
          offset: 1
        })).toString('base64url')
      }),
      /continuation token/
    );

    const ownerB = await store.claimOwnerLease('channel-owner-b', 30_000);
    assert.equal(ownerB.kind, 'claimed');
    const replacement = {
      ...initial,
      lifecycleGeneration: 8n,
      descriptorRevision: 1n,
      endpoint: 'tcp://10.0.0.3:7400',
      ownerId: ownerB.token.ownerId,
      leaseGeneration: ownerB.token.leaseGeneration
    };
    assert.equal(
      (await store.updateClientServer(
        replacement,
        framework.ZLinkLocationWriteIntent.Takeover
      )).status,
      framework.ZLinkLocationWriteStatus.RejectedConflict
    );
    assert.equal(await store.releaseOwnerLease(ownerA.token), 'released');
    const takenOver = await store.updateClientServer(
      replacement,
      framework.ZLinkLocationWriteIntent.Takeover
    );
    assert.equal(takenOver.status, framework.ZLinkLocationWriteStatus.Stored);
    assert.equal(takenOver.generation, 4n);
    assert.equal(
      (await store.updateClientServer(
        { ...initial, descriptorRevision: 5n },
        framework.ZLinkLocationWriteIntent.Renew
      )).status,
      framework.ZLinkLocationWriteStatus.RejectedConflict
    );
    const ordersA = (await store.listClientServers('orders')).items
      .find(row => row.serverRid.toHex() === rid('orders-a').toHex());
    assert.equal(ordersA.lifecycleGeneration, 8n);
    assert.equal(ordersA.ownerId, ownerB.token.ownerId);

    const ownerBSecond = {
      ...second,
      ownerId: ownerB.token.ownerId,
      leaseGeneration: ownerB.token.leaseGeneration,
      lifecycleGeneration: 8n,
      descriptorRevision: 1n
    };
    assert.equal(
      (await store.updateClientServer(
        ownerBSecond,
        framework.ZLinkLocationWriteIntent.Takeover
      )).status,
      framework.ZLinkLocationWriteStatus.Stored
    );
    assert.equal(await store.removeAllByOwner(ownerB.token), 2n);
    assert.equal((await store.listClientServers('orders')).items.length, 0);
    assert.equal((await store.listClientServers('billing')).items.length, 1);
  } finally {
    await store.dispose();
    await cleanupPrefix(fixture.client, prefix);
    await fixture.client.quit();
  }
});

test('redis fanout publisher descriptors match the fixture and enforce dedicated fences', async (t) => {
  const fixture = await redisFixture();
  if (fixture === undefined) {
    t.skip('Redis is not reachable; set ZLINK_REDIS_TEST_ENDPOINT or run Redis on 127.0.0.1:16379/6379.');
    return;
  }
  const prefix = `zlink:node-location-fanout:${process.pid}:${Date.now()}`;
  const store = new redisLocations.ZLinkRedisLocationStore({ url: fixture.url, keyPrefix: prefix });
  try {
    const owner = await store.claimOwnerLease('fanout-owner-a', 30_000);
    assert.equal(owner.kind, 'claimed');
    const initial = fanoutPublisherDescriptor(
      'events-pub-a',
      owner.token.ownerId,
      owner.token.leaseGeneration
    );
    const claimed = await store.updateFanoutPublisher(
      initial,
      framework.ZLinkLocationWriteIntent.NewClaim
    );
    assert.equal(claimed.status, framework.ZLinkLocationWriteStatus.Stored);
    assert.equal(claimed.generation, 1n);

    assert.equal(
      (await store.updateFanoutPublisher(
        { ...initial, state: framework.ZLinkFrameworkRuntimeState.Draining },
        framework.ZLinkLocationWriteIntent.Renew
      )).status,
      framework.ZLinkLocationWriteStatus.IgnoredStale
    );
    assert.equal(
      (await store.updateFanoutPublisher(
        { ...initial, descriptorRevision: 4n, endpoint: 'tcp://10.0.0.9:7500' },
        framework.ZLinkLocationWriteIntent.Renew
      )).status,
      framework.ZLinkLocationWriteStatus.IgnoredStale
    );
    assert.equal(
      (await store.updateFanoutPublisher(
        { ...initial, lifecycleGeneration: 8n, descriptorRevision: 4n },
        framework.ZLinkLocationWriteIntent.Renew
      )).status,
      framework.ZLinkLocationWriteStatus.IgnoredStale
    );
    assert.equal(
      (await store.updateFanoutPublisher(
        {
          ...initial,
          descriptorRevision: 4n,
          state: framework.ZLinkFrameworkRuntimeState.Draining
        },
        framework.ZLinkLocationWriteIntent.Renew
      )).status,
      framework.ZLinkLocationWriteStatus.Stored
    );

    const fixtureContract = redisSemanticFixture('fanout-publisher-descriptor-v1.json');
    const rowDigest = createHash('sha256')
      .update(fixtureContract.row.key, 'utf8')
      .digest('hex');
    const physicalKey =
      `${prefix}:{zlink-location-v1}:descriptor:fanout-publisher:${rowDigest}`;
    const storedHash = await fixture.client.hGetAll(physicalKey);
    assert.deepEqual(Object.keys(storedHash).sort(), fixtureContract.hashFields.slice().sort());
    assert.equal(storedHash.owner, initial.ownerId);
    assert.equal(storedHash.channel, initial.channelName);
    assert.deepEqual(JSON.parse(storedHash.json), {
      ...JSON.parse(fixtureContract.row.hash.json),
      DescriptorRevision: 4,
      State: 'Draining',
      OwnerLeaseGeneration: Number(owner.token.leaseGeneration)
    });

    const second = fanoutPublisherDescriptor(
      'events-pub-b',
      owner.token.ownerId,
      owner.token.leaseGeneration
    );
    assert.equal(
      (await store.updateFanoutPublisher(
        second,
        framework.ZLinkLocationWriteIntent.NewClaim
      )).status,
      framework.ZLinkLocationWriteStatus.Stored
    );
    const listed = [];
    let continuationToken;
    do {
      const page = await store.listFanoutPublishers('events', {
        pageSize: 1,
        continuationToken
      });
      listed.push(...page.items);
      continuationToken = page.continuationToken;
    } while (continuationToken !== undefined);
    assert.deepEqual(
      listed.map(row => row.publisherRid.toHex()).sort(),
      [rid('events-pub-a').toHex(), rid('events-pub-b').toHex()].sort()
    );
    await assert.rejects(
      store.listFanoutPublishers('events', {
        pageSize: 1,
        continuationToken: Buffer.from(JSON.stringify({
          kind: 'client-server-v1',
          channelName: 'events',
          offset: 1
        })).toString('base64url')
      }),
      /continuation token/
    );

    const audit = {
      ...fanoutPublisherDescriptor(
        'audit-pub-a',
        owner.token.ownerId,
        owner.token.leaseGeneration
      ),
      channelName: 'audit'
    };
    assert.equal(
      (await store.updateFanoutPublisher(
        audit,
        framework.ZLinkLocationWriteIntent.NewClaim
      )).status,
      framework.ZLinkLocationWriteStatus.Stored
    );
    assert.equal(
      await store.removeFanoutPublisher(
        { channelName: 'audit', publisherRid: audit.publisherRid },
        {
          ownerId: owner.token.ownerId,
          leaseGeneration: owner.token.leaseGeneration + 1n
        }
      ),
      framework.ZLinkLocationWriteStatus.IgnoredStale
    );
    assert.equal(
      await store.removeFanoutPublisher(
        { channelName: 'audit', publisherRid: audit.publisherRid },
        owner.token
      ),
      framework.ZLinkLocationWriteStatus.Stored
    );
    assert.equal(await store.removeAllByOwner(owner.token), 2n);
    assert.equal((await store.listFanoutPublishers('events')).items.length, 0);
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

function redisSemanticFixture(fileName) {
  return JSON.parse(fs.readFileSync(path.resolve(
    __dirname,
    `../../../../testdata/location/redis/${fileName}`
  ), 'utf8'));
}

function exactMeshNodeDescriptor(nodeName = 'game-a', ownerId = 'mesh-owner-a', leaseGeneration = 1n) {
  return {
    meshName: 'game',
    rid: rid(nodeName),
    lifecycleGeneration: 7n,
    descriptorRevision: 3n,
    endpoint: 'tcp://10.0.0.1:7300',
    objectRole: framework.ZLinkObjectRole.Server,
    placementWeight: 100,
    objectCapacity: {
      activeObjects: 0,
      pendingActivations: 0,
      maxActiveObjects: 100,
      maxPendingActivations: 100
    },
    channelWeights: { orders: 100, world: 50 },
    applicationVersion: 1n,
    spotTypes: ['room'],
    objectCapabilities: [{
      objectKind: 'instance_spot',
      stableType: 'room',
      policy: 'recreate',
      hasSnapshotAdapter: false,
      placementProfiles: ['default'],
      activeLimit: 100,
      pendingLimit: 100
    }],
    state: framework.ZLinkFrameworkRuntimeState.Serving,
    securityIdentity: 'cluster-a',
    ownerId,
    leaseGeneration,
    updatedAt: new Date('2024-07-15T00:00:00.000Z')
  };
}

function clientServerDescriptor(serverName, ownerId, leaseGeneration) {
  return {
    channelName: 'orders',
    serverRid: rid(serverName),
    lifecycleGeneration: 7n,
    descriptorRevision: 3n,
    endpoint: 'tcp://10.0.0.2:7400',
    weight: 100,
    state: framework.ZLinkFrameworkRuntimeState.Serving,
    securityIdentity: 'cluster-a',
    ownerId,
    leaseGeneration,
    updatedAt: new Date('2024-07-15T00:00:00.000Z')
  };
}

function fanoutPublisherDescriptor(publisherName, ownerId, leaseGeneration) {
  return {
    channelName: 'events',
    publisherRid: rid(publisherName),
    lifecycleGeneration: 7n,
    descriptorRevision: 3n,
    endpoint: 'tcp://10.0.0.3:7500',
    state: framework.ZLinkFrameworkRuntimeState.Serving,
    securityIdentity: 'cluster-a',
    ownerId,
    leaseGeneration,
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
