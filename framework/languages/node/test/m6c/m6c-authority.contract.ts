import assert from 'node:assert/strict';
import { test } from 'node:test';
import type {
  ZLinkAggregateId,
  ZLinkAuthorityKey,
  ZLinkLocationOwnerToken,
  ZLinkObjectCreationTarget
} from '../../packages/framework/src/contracts';
import {
  ZLinkInMemoryAuthorityStore
} from '../../packages/framework/src/runtime/locations/in-memory-authority-store';
import {
  ZLinkInMemoryLocationStore
} from '../../packages/framework/src/runtime/locations/in-memory-location-store';

test('owner lease uses exact claim read renew and release fencing', async () => {
  let now = 100;
  const store = new ZLinkInMemoryLocationStore(() => new Date(now));
  const claimed = await store.claimOwnerLease('owner-a', 50);
  assert.equal(claimed.kind, 'claimed');
  if (claimed.kind !== 'claimed') return;
  assert.equal(claimed.token.leaseGeneration, 1n);
  assert.deepEqual(await store.claimOwnerLease('owner-a', 50), { kind: 'conflict' });
  const found = await store.readOwnerLease('owner-a');
  assert.equal(found.kind, 'found');
  assert.deepEqual(
    await store.renewOwnerLease({ ownerId: 'owner-a', leaseGeneration: 2n }, 50),
    { kind: 'stale' }
  );
  const renewed = await store.renewOwnerLease(claimed.token, 50);
  assert.equal(renewed.kind, 'renewed');
  assert.equal(
    await store.releaseOwnerLease({ ownerId: 'owner-a', leaseGeneration: 2n }),
    'stale'
  );
  assert.equal(await store.releaseOwnerLease(claimed.token), 'released');
  now++;
  const reclaimed = await store.claimOwnerLease('owner-a', 50);
  assert.equal(reclaimed.kind, 'claimed');
  if (reclaimed.kind === 'claimed') {
    assert.ok(reclaimed.token.leaseGeneration > claimed.token.leaseGeneration);
  }
});

test('generic reservation is the only Missing to Pending to Active path', async () => {
  const live = new Set(['mesh:node-a:1:owner-a:1']);
  const store = authority(live);
  const reserved = await store.reserve(reserveRequest('room', target('node-a', 'owner-a')));
  assert.equal(reserved.kind, 'reserved');
  if (reserved.kind !== 'reserved') return;
  assert.equal(reserved.creating.allocation.state, 'pending');
  assert.equal(reserved.creating.allocation.stableType, 'room');
  assert.equal(reserved.creating.ownerId, 'owner-a');

  const committed = await store.commit({
    key: { kind: 'user_spot', globalId: 'room' },
    reservationId: reserved.reservationId,
    expectedStoreVersion: reserved.creating.storeVersion.value,
    target: target('node-a', 'owner-a'),
    readyPayload: Buffer.from('ready')
  });
  assert.equal(committed.kind, 'committed');
  if (committed.kind !== 'committed') return;
  assert.equal(committed.ready.allocation.state, 'active');
  assert.equal(committed.ready.objectGeneration, reserved.creating.objectGeneration);
  assert.equal(committed.ready.authorityOwnerGeneration, reserved.creating.authorityOwnerGeneration);
});

test('creation abort cleans pending capacity without requiring a live target', async () => {
  const live = new Set(['mesh:node-a:1:owner-a:1']);
  const store = authority(live);
  const reserved = await store.reserve(reserveRequest('ephemeral', target('node-a', 'owner-a')));
  assert.equal(reserved.kind, 'reserved');
  if (reserved.kind !== 'reserved') return;
  live.clear();
  assert.deepEqual(await store.abort({
    key: { kind: 'user_spot', globalId: 'ephemeral' },
    reservationId: reserved.reservationId,
    expectedStoreVersion: reserved.creating.storeVersion.value,
    target: target('node-a', 'owner-a')
  }), { kind: 'aborted' });
  assert.equal((await store.readAuthority(authorityKey('user_spot:ephemeral'))).kind, 'missing');
});

test('relocation matches durable source allocation and only requires the target to be live', async () => {
  const live = new Set([
    'mesh:node-a:1:owner-a:1',
    'mesh:node-b:2:owner-b:2'
  ]);
  const store = authority(live);
  const current = await createActive(store, 'relocate', target('node-a', 'owner-a'));
  live.delete('mesh:node-a:1:owner-a:1');

  const reservation = await store.reserveRelocationCapacity({
    reservationId: '11111111-1111-4111-8111-111111111111',
    authorityKey: authorityKey('user_spot:relocate'),
    expectedStoreVersion: current.storeVersion,
    objectKind: 'user_spot',
    stableType: 'room',
    sourceDescriptor: { meshName: 'mesh', rid: 'node-a' },
    sourceNodeLifecycleGeneration: 1n,
    sourceOwner: owner('owner-a', 1n),
    targetDescriptor: { meshName: 'mesh', rid: 'node-b' },
    targetNodeLifecycleGeneration: 2n,
    targetOwner: owner('owner-b', 2n),
    capacityDelta: 1
  });
  assert.equal(reservation.kind, 'reserved');
  if (reservation.kind !== 'reserved') return;
  const moved = await store.compareExchangeAuthority(
    authorityKey('user_spot:relocate'),
    current.storeVersion,
    {
      kind: 'put',
      payload: Buffer.from('moved'),
      generationTransition: 'newOwner',
      targetOwner: owner('owner-b', 2n),
      relocationCapacityFence: reservation.fence
    }
  );
  assert.equal(moved.kind, 'stored');
  if (moved.kind !== 'stored') return;
  assert.equal(moved.ownerId, 'owner-b');
  assert.equal(moved.allocation.descriptor.rid, 'node-b');
  assert.equal(moved.objectGeneration, current.objectGeneration);
  assert.ok(moved.authorityOwnerGeneration > current.authorityOwnerGeneration);
});

test('aggregate prepare binds relocation fences until aggregate commit or abort', async () => {
  const live = new Set([
    'mesh:node-a:1:owner-a:1',
    'mesh:node-b:2:owner-b:2'
  ]);
  const store = authority(live);
  const current = await createActive(store, 'aggregate', target('node-a', 'owner-a'));
  const capacity = await store.reserveRelocationCapacity({
    reservationId: '22222222-2222-4222-8222-222222222222',
    authorityKey: authorityKey('user_spot:aggregate'),
    expectedStoreVersion: current.storeVersion,
    objectKind: 'user_spot',
    stableType: 'room',
    sourceDescriptor: { meshName: 'mesh', rid: 'node-a' },
    sourceNodeLifecycleGeneration: 1n,
    sourceOwner: owner('owner-a', 1n),
    targetDescriptor: { meshName: 'mesh', rid: 'node-b' },
    targetNodeLifecycleGeneration: 2n,
    targetOwner: owner('owner-b', 2n),
    capacityDelta: 1
  });
  assert.equal(capacity.kind, 'reserved');
  if (capacity.kind !== 'reserved') return;
  const aggregateId = { value: '33333333-3333-4333-8333-333333333333' } as ZLinkAggregateId;
  const prepared = await store.prepareAggregate({
    aggregateId,
    aggregateGeneration: 1n,
    participants: [{
      authorityKey: authorityKey('user_spot:aggregate'),
      expectedStoreVersion: current.storeVersion,
      ownerTransition: 'newOwner',
      authorityPayload: Buffer.from('aggregate-ready'),
      membershipMutation: Buffer.from('membership')
    }],
    inventoryDigest: Buffer.alloc(32, 7),
    targetReservations: [capacity.fence],
    targetOwner: owner('owner-b', 2n)
  });
  assert.equal(prepared.kind, 'prepared');
  assert.equal(await store.abortRelocationCapacity(capacity.fence), 'stale');
  if (prepared.kind !== 'prepared') return;
  assert.deepEqual(await store.commitAggregate(prepared.fence), { kind: 'committed' });
  assert.equal(await store.abortRelocationCapacity(capacity.fence), 'alreadyCommitted');
});

function authority(live: Set<string>): ZLinkInMemoryAuthorityStore {
  return new ZLinkInMemoryAuthorityStore({
    isTargetLive(descriptor, lifecycle, token) {
      return live.has(
        `${descriptor.meshName}:${descriptor.rid}:${lifecycle}:${token.ownerId}:${token.leaseGeneration}`
      );
    }
  }, () => new Date(100));
}

function target(rid: string, ownerId: string): ZLinkObjectCreationTarget {
  const generation = rid === 'node-a' ? 1n : 2n;
  return {
    meshName: 'mesh',
    nodeRid: rid,
    nodeLifecycleGeneration: generation,
    owner: owner(ownerId, generation)
  };
}

function reserveRequest(globalId: string, placement: ZLinkObjectCreationTarget) {
  return {
    key: { kind: 'user_spot' as const, globalId },
    intent: {
      stableType: 'room',
      requestContentReference: `request:${globalId}`,
      requestSha256: Buffer.alloc(32, 1),
      requestEncodedSize: 10n
    },
    target: placement,
    creatingPayload: Buffer.from('creating'),
    pendingCapacityDelta: 1
  };
}

async function createActive(
  store: ZLinkInMemoryAuthorityStore,
  globalId: string,
  placement: ZLinkObjectCreationTarget
) {
  const reserved = await store.reserve(reserveRequest(globalId, placement));
  assert.equal(reserved.kind, 'reserved');
  if (reserved.kind !== 'reserved') throw new Error('reservation failed');
  const committed = await store.commit({
    key: { kind: 'user_spot', globalId },
    reservationId: reserved.reservationId,
    expectedStoreVersion: reserved.creating.storeVersion.value,
    target: placement,
    readyPayload: Buffer.from('ready')
  });
  assert.equal(committed.kind, 'committed');
  if (committed.kind !== 'committed') throw new Error('commit failed');
  return committed.ready;
}

function authorityKey(value: string): ZLinkAuthorityKey {
  return { value } as ZLinkAuthorityKey;
}

function owner(ownerId: string, generation: bigint): ZLinkLocationOwnerToken {
  return { ownerId, leaseGeneration: generation };
}
