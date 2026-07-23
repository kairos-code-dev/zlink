import assert from 'node:assert/strict';
import { test } from 'node:test';
import type {
  ZLinkAggregateId,
  ZLinkAuthorityKey,
  ZLinkLocationOwnerToken,
  ZLinkObjectCreationTarget
} from '../../packages/framework/src/contracts';
import {
  ZLinkSpotCloseReason,
  ZLinkSpotCreateState
} from '../../packages/framework/src/contracts';
import {
  ZLinkInMemoryAuthorityStore
} from '../../packages/framework/src/runtime/locations/in-memory-authority-store';
import {
  ZLinkInMemoryLocationStore
} from '../../packages/framework/src/runtime/locations/in-memory-location-store';
import { encodeAuthorityKey } from '../../packages/framework/src/runtime/locations/authority-key-codec';
import {
  ZLinkUserSpotCreationCoordinator
} from '../../packages/framework/src/runtime/host/user-spot-creation-coordinator';
import {
  ZLinkPublicSpotManager
} from '../../packages/framework/src/runtime/spots/spot-manager-public';
import {
  invokeSpotClosing
} from '../../packages/framework/src/runtime/spots/spot-closing';

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
  assert.deepEqual(reserved.creating.pendingCreation, {
    reservationId: reserved.reservationId,
    requestContentReference: 'request:room',
    requestSha256: Buffer.alloc(32, 1),
    requestEncodedSize: 10n
  });

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
  assert.equal(committed.ready.pendingCreation, undefined);
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
  assert.equal((await store.readAuthority(authorityKey('ephemeral'))).kind, 'missing');
});

test('public User Spot coordinator hides Pending, runs one factory, then publishes Ready generation', async () => {
  const store = authority(new Set(['mesh:node-a:1:owner-a:1']));
  const coordinator = new ZLinkUserSpotCreationCoordinator({
    store,
    target: async () => ({
      meshName: 'mesh',
      nodeRid: 'node-a',
      nodeGeneration: 1n,
      owner: owner('owner-a', 1n),
      isLocal: true
    }),
    pollIntervalMs: 1
  });
  let release!: () => void;
  const initialize = new Promise<void>(resolve => {
    release = resolve;
  });
  let materializations = 0;
  const request = {
    meshName: 'mesh',
    spotRid: 'room-42',
    stableType: 'room',
    requestPayload: Buffer.from('create-room'),
    timeoutMs: 1_000
  };
  const first = coordinator.getOrCreate(request, async () => {
    materializations++;
    await initialize;
    return {
      spotRid: request.spotRid,
      state: ZLinkSpotCreateState.Created
    };
  });
  await new Promise(resolve => setImmediate(resolve));
  const pending = await store.readAuthority(authorityKey('room-42'));
  assert.equal(pending.kind, 'snapshot');
  if (pending.kind === 'snapshot') {
    assert.equal(pending.allocation.state, 'pending');
  }

  const joined = coordinator.getOrCreate(request, async () => {
    materializations++;
    throw new Error('CAS loser must not materialize.');
  });
  release();
  const [created, existing] = await Promise.all([first, joined]);
  assert.equal(materializations, 1);
  assert.equal(created.result.state, ZLinkSpotCreateState.Created);
  assert.equal(existing.result.state, ZLinkSpotCreateState.Existing);
  assert.equal(created.spot.objectGeneration, existing.spot.objectGeneration);
  assert.ok(created.spot.objectGeneration > 0n);
  const ready = await store.readAuthority(authorityKey('room-42'));
  assert.equal(ready.kind, 'snapshot');
  if (ready.kind === 'snapshot') {
    assert.equal(ready.allocation.state, 'active');
    assert.equal(ready.objectGeneration, created.spot.objectGeneration);
  }
});

test('User Spot rejection is terminal and aborts the Location reservation without Relocation Store', async () => {
  const store = authority(new Set(['mesh:node-a:1:owner-a:1']));
  const coordinator = new ZLinkUserSpotCreationCoordinator({
    store,
    target: async () => ({
      meshName: 'mesh',
      nodeRid: 'node-a',
      nodeGeneration: 1n,
      owner: owner('owner-a', 1n),
      isLocal: true
    })
  });
  const result = await coordinator.getOrCreate({
    meshName: 'mesh',
    spotRid: 'rejected-room',
    stableType: 'room',
    requestPayload: Buffer.from('reject-me'),
    timeoutMs: 1_000
  }, async () => ({
    spotRid: 'rejected-room',
    state: ZLinkSpotCreateState.Rejected,
    reply: { reason: 'closed' }
  }));
  assert.equal(result.result.state, ZLinkSpotCreateState.Rejected);
  assert.deepEqual(result.result.reply, { reason: 'closed' });
  assert.equal(
    (await store.readAuthority(authorityKey('rejected-room'))).kind,
    'missing'
  );
});

test('User Spot creation applies one deadline signal through factory and abort cleanup', async () => {
  const store = authority(new Set(['mesh:node-a:1:owner-a:1']));
  const coordinator = new ZLinkUserSpotCreationCoordinator({
    store,
    target: async () => ({
      meshName: 'mesh',
      nodeRid: 'node-a',
      nodeGeneration: 1n,
      owner: owner('owner-a', 1n),
      isLocal: true
    })
  });
  await assert.rejects(
    () => coordinator.getOrCreate({
      meshName: 'mesh',
      spotRid: 'deadline-room',
      stableType: 'room',
      requestPayload: Buffer.from('slow'),
      timeoutMs: 5
    }, async (_target, signal) => await new Promise((_resolve, reject) => {
      signal.addEventListener('abort', () => reject(signal.reason), { once: true });
    })),
    (error: unknown) => error instanceof Error
      && 'kind' in error
      && error.kind === 'deadlineExceeded'
  );
  assert.equal(
    (await store.readAuthority(authorityKey('deadline-room'))).kind,
    'missing'
  );
});

test('User Spot reconciliation and abort cleanup stop at their bounded cleanup deadline', async () => {
  const store = authority(new Set(['mesh:node-a:1:owner-a:1']));
  store.abort = async () => await new Promise(() => undefined);
  const coordinator = new ZLinkUserSpotCreationCoordinator({
    store,
    target: async () => ({
      meshName: 'mesh',
      nodeRid: 'node-a',
      nodeGeneration: 1n,
      owner: owner('owner-a', 1n),
      isLocal: true
    }),
    cleanupTimeoutMs: 5
  });
  const started = Date.now();
  await assert.rejects(
    () => coordinator.getOrCreate({
      meshName: 'mesh',
      spotRid: 'cleanup-deadline-room',
      stableType: 'room',
      requestPayload: Buffer.from('create'),
      timeoutMs: 1_000
    }, async () => {
      throw new Error('factory failed');
    }),
    AggregateError
  );
  assert.ok(Date.now() - started < 100);
});

test('User Spot reservation maps capacity exhaustion and Pending expiry to exact typed errors', async () => {
  const base = authority(new Set(['mesh:node-a:1:owner-a:1']));
  const exhausted = Object.create(base) as ZLinkInMemoryAuthorityStore;
  exhausted.reserve = async () => ({ kind: 'placementCapacityExhausted' });
  const targetOptions = {
    meshName: 'mesh',
    nodeRid: 'node-a',
    nodeGeneration: 1n,
    owner: owner('owner-a', 1n),
    isLocal: true
  } as const;
  const exhaustedCoordinator = new ZLinkUserSpotCreationCoordinator({
    store: exhausted,
    target: async () => targetOptions
  });
  await assert.rejects(
    () => exhaustedCoordinator.getOrCreate({
      meshName: 'mesh',
      spotRid: 'full-room',
      stableType: 'room',
      requestPayload: Buffer.from('create'),
      timeoutMs: 100
    }, async () => {
      throw new Error('factory must not start');
    }),
    (error: unknown) => error instanceof Error
      && 'kind' in error
      && error.kind === 'placementCapacityExhausted'
  );

  const pendingStore = authority(new Set(['mesh:node-a:1:owner-a:1']));
  await pendingStore.reserve(reserveRequest('pending-room', target('node-a', 'owner-a')));
  const pendingCoordinator = new ZLinkUserSpotCreationCoordinator({
    store: pendingStore,
    target: async () => targetOptions,
    pollIntervalMs: 1
  });
  await assert.rejects(
    () => pendingCoordinator.getOrCreate({
      meshName: 'mesh',
      spotRid: 'pending-room',
      stableType: 'room',
      requestPayload: Buffer.from('same-request'),
      timeoutMs: 5
    }, async () => {
      throw new Error('CAS loser must not start factory');
    }),
    (error: unknown) => error instanceof Error
      && 'kind' in error
      && error.kind === 'deadlineExceeded'
  );
});

test('User Spot production placement maps no target to retriable capacity exhaustion and provider faults to RequestFailed', async () => {
  const store = authority(new Set(['mesh:node-a:1:owner-a:1']));
  const request = {
    meshName: 'mesh',
    spotRid: 'placement-room',
    stableType: 'room',
    requestPayload: Buffer.from('create'),
    timeoutMs: 100
  };
  const noTarget = new ZLinkUserSpotCreationCoordinator({
    store,
    target: async () => undefined
  });
  await assert.rejects(
    () => noTarget.getOrCreate(request, async () => {
      throw new Error('factory must not start');
    }),
    (error: unknown) => error instanceof Error
      && 'kind' in error
      && error.kind === 'placementCapacityExhausted'
      && 'isRetriable' in error
      && error.isRetriable === true
  );

  const providerFault = new Error('provider unavailable');
  const failedProvider = new ZLinkUserSpotCreationCoordinator({
    store,
    target: async () => {
      throw providerFault;
    }
  });
  await assert.rejects(
    () => failedProvider.getOrCreate(request, async () => {
      throw new Error('factory must not start');
    }),
    (error: unknown) => error instanceof Error
      && 'kind' in error
      && error.kind === 'requestFailed'
      && error.cause === providerFault
  );

  const storeFault = new Error('store unavailable');
  const failedStore = authority(new Set(['mesh:node-a:1:owner-a:1']));
  failedStore.reserve = async () => {
    throw storeFault;
  };
  const failedReservation = new ZLinkUserSpotCreationCoordinator({
    store: failedStore,
    target: async () => ({
      meshName: 'mesh',
      nodeRid: 'node-a',
      nodeGeneration: 1n,
      owner: owner('owner-a', 1n),
      isLocal: true
    })
  });
  await assert.rejects(
    () => failedReservation.getOrCreate(request, async () => {
      throw new Error('factory must not start');
    }),
    (error: unknown) => error instanceof Error
      && 'kind' in error
      && error.kind === 'requestFailed'
      && error.cause === storeFault
  );
});

test('User Spot creation fails closed before reservation when placement selects a remote owner', async () => {
  const store = authority(new Set(['mesh:node-b:2:owner-b:2']));
  const coordinator = new ZLinkUserSpotCreationCoordinator({
    store,
    target: async () => ({
      meshName: 'mesh',
      nodeRid: 'node-b',
      nodeGeneration: 2n,
      owner: owner('owner-b', 2n),
      isLocal: false
    })
  });
  await assert.rejects(
    () => coordinator.getOrCreate({
      meshName: 'mesh',
      spotRid: 'remote-selected-room',
      stableType: 'room',
      requestPayload: Buffer.from('create'),
      timeoutMs: 100
    }, async () => {
      throw new Error('remote owner must not start local factory');
    }),
    (error: unknown) => error instanceof Error
      && 'kind' in error
      && error.kind === 'requestFailed'
  );
  assert.equal(
    (await store.readAuthority(authorityKey('remote-selected-room'))).kind,
    'missing'
  );
});

test('User Spot Ready commit Store rejection is exposed as RequestFailed with the original cause', async () => {
  const store = authority(new Set(['mesh:node-a:1:owner-a:1']));
  const commitFault = new Error('commit unavailable');
  store.commit = async () => {
    throw commitFault;
  };
  const coordinator = new ZLinkUserSpotCreationCoordinator({
    store,
    target: async () => ({
      meshName: 'mesh',
      nodeRid: 'node-a',
      nodeGeneration: 1n,
      owner: owner('owner-a', 1n),
      isLocal: true
    })
  });
  await assert.rejects(
    () => coordinator.getOrCreate({
      meshName: 'mesh',
      spotRid: 'commit-failed-room',
      stableType: 'room',
      requestPayload: Buffer.from('create'),
      timeoutMs: 100
    }, async () => ({
      spotRid: 'commit-failed-room',
      state: ZLinkSpotCreateState.Created
    })),
    (error: unknown) => error instanceof Error
      && 'kind' in error
      && error.kind === 'requestFailed'
      && error.cause === commitFault
  );
  assert.equal(
    (await store.readAuthority(authorityKey('commit-failed-room'))).kind,
    'missing'
  );
});

test('User Spot CAS loser fails immediately when the existing owner is remote', async () => {
  const store = authority(new Set([
    'mesh:node-a:1:owner-a:1',
    'mesh:node-b:2:owner-b:2'
  ]));
  await createActive(store, 'remote-room', target('node-b', 'owner-b'));
  const coordinator = new ZLinkUserSpotCreationCoordinator({
    store,
    target: async () => ({
      meshName: 'mesh',
      nodeRid: 'node-a',
      nodeGeneration: 1n,
      owner: owner('owner-a', 1n),
      isLocal: true
    }),
    pollIntervalMs: 1
  });
  const started = Date.now();
  await assert.rejects(
    () => coordinator.getOrCreate({
      meshName: 'mesh',
      spotRid: 'remote-room',
      stableType: 'room',
      requestPayload: Buffer.from('create'),
      timeoutMs: 1_000
    }, async () => {
      throw new Error('remote CAS loser must not start factory');
    }),
    (error: unknown) => error instanceof Error
      && 'kind' in error
      && error.kind === 'requestFailed'
  );
  assert.ok(Date.now() - started < 100);
});

test('User Spot close fences the exact generation and deletes authority only after owner close', async () => {
  const store = authority(new Set(['mesh:node-a:1:owner-a:1']));
  const coordinator = new ZLinkUserSpotCreationCoordinator({
    store,
    target: async () => ({
      meshName: 'mesh',
      nodeRid: 'node-a',
      nodeGeneration: 1n,
      owner: owner('owner-a', 1n),
      isLocal: true
    })
  });
  const created = await coordinator.getOrCreate({
    meshName: 'mesh',
    spotRid: 'close-room',
    stableType: 'room',
    requestPayload: Buffer.from('create'),
    timeoutMs: 1_000
  }, async () => ({
    spotRid: 'close-room',
    state: ZLinkSpotCreateState.Created
  }));
  let ownerClosed = false;
  assert.equal(await coordinator.close(created.spot, async () => {
    ownerClosed = true;
    return true;
  }), true);
  assert.equal(ownerClosed, true);
  assert.equal(
    (await store.readAuthority(authorityKey('close-room'))).kind,
    'missing'
  );
});

test('Spot closing callback receives exact reason, absolute deadline, and cleanup signal', async () => {
  let observed = false;
  await invokeSpotClosing(async (context, cleanupSignal) => {
    observed = true;
    assert.equal(context.reason, ZLinkSpotCloseReason.HostShutdown);
    assert.ok(context.deadline.getTime() > Date.now());
    assert.equal(cleanupSignal.aborted, false);
  }, ZLinkSpotCloseReason.HostShutdown, 100);
  assert.equal(observed, true);
});

test('Spot closing deadline aborts cleanup and stops waiting for a stuck callback', async () => {
  const started = Date.now();
  let aborted = false;
  await invokeSpotClosing(async (_context, cleanupSignal) => {
    cleanupSignal.addEventListener('abort', () => {
      aborted = true;
    }, { once: true });
    await new Promise<void>(() => undefined);
  }, ZLinkSpotCloseReason.ExplicitClose, 5);
  assert.equal(aborted, true);
  assert.ok(Date.now() - started < 100);
});

test('exact User Spot manager call is single-use and returns the committed SpotRef', async () => {
  class RoomSpot {}
  const store = authority(new Set(['mesh:node-a:1:owner-a:1']));
  const coordinator = new ZLinkUserSpotCreationCoordinator({
    store,
    target: async () => ({
      meshName: 'mesh',
      nodeRid: 'node-a',
      nodeGeneration: 1n,
      owner: owner('owner-a', 1n),
      isLocal: true
    })
  });
  let created = 0;
  const manager = new ZLinkPublicSpotManager({
    local: {
      async getOrCreate(_mesh: string, _type: typeof RoomSpot, spotRid: string) {
        created++;
        return { spotRid, state: ZLinkSpotCreateState.Created };
      },
      async close() {
        return true;
      }
    } as never,
    coordinator,
    factories: new Map([[
      'mesh',
      new Map([[
        'room',
        {
          implementation: RoomSpot,
          relocation: { kind: 'disabled' } as never
        }
      ]])
    ]]) as never,
    resolver: () => undefined,
    isLocalNode: () => true,
    defaultTimeoutMs: 1_000
  });
  const call = manager.getOrCreate('room-7', 'room')
    .inMesh('mesh')
    .request({ title: 'room' })
    .placementProfile('interactive')
    .affinityKey('tenant-7')
    .timeout(500);
  const result = await call.submit();
  assert.equal(created, 1);
  assert.equal(result.state, ZLinkSpotCreateState.Created);
  assert.equal(result.spot.spotRid, 'room-7');
  assert.ok(result.spot.objectGeneration > 0n);
  await assert.rejects(() => call.submit(), /already been submitted/);
});

test('User Spot create retries a generated RID collision with another stable type within the original deadline', async () => {
  class RoomSpot {}
  const store = authority(new Set(['mesh:node-a:1:owner-a:1']));
  const collision = await store.reserve({
    ...reserveRequest('spot-collision', target('node-a', 'owner-a')),
    intent: {
      ...reserveRequest('spot-collision', target('node-a', 'owner-a')).intent,
      stableType: 'another-room'
    }
  });
  assert.equal(collision.kind, 'reserved');
  if (collision.kind !== 'reserved') return;
  await store.commit({
    key: { kind: 'user_spot', globalId: 'spot-collision' },
    reservationId: collision.reservationId,
    expectedStoreVersion: collision.creating.storeVersion.value,
    target: target('node-a', 'owner-a'),
    readyPayload: Buffer.from('ready')
  });
  const coordinator = new ZLinkUserSpotCreationCoordinator({
    store,
    target: async () => ({
      meshName: 'mesh',
      nodeRid: 'node-a',
      nodeGeneration: 1n,
      owner: owner('owner-a', 1n),
      isLocal: true
    })
  });
  const generated = ['spot-collision', 'spot-fresh'];
  const materialized: string[] = [];
  const manager = new ZLinkPublicSpotManager({
    local: {
      async getOrCreate(_mesh: string, _type: typeof RoomSpot, spotRid: string) {
        materialized.push(spotRid);
        return { spotRid, state: ZLinkSpotCreateState.Created };
      },
      async close() {
        return true;
      }
    } as never,
    coordinator,
    factories: new Map([[
      'mesh',
      new Map([[
        'room',
        { implementation: RoomSpot, relocation: { kind: 'disabled' } as never }
      ]])
    ]]) as never,
    resolver: () => undefined,
    isLocalNode: () => true,
    defaultTimeoutMs: 1_000,
    ridFactory: () => generated.shift() as never
  });
  const result = await manager.create('room').inMesh('mesh').submit();
  assert.equal(result.state, ZLinkSpotCreateState.Created);
  assert.deepEqual(materialized, ['spot-fresh']);
  assert.equal(result.spot.spotRid, 'spot-fresh');
});

test('User Spot create retries a generated same-type RID owned by a remote node while getOrCreate stays fail-closed', async () => {
  class RoomSpot {}
  const store = authority(new Set([
    'mesh:node-a:1:owner-a:1',
    'mesh:node-b:2:owner-b:2'
  ]));
  await createActive(store, 'spot-remote-collision', target('node-b', 'owner-b'));
  const coordinator = new ZLinkUserSpotCreationCoordinator({
    store,
    target: async () => ({
      meshName: 'mesh',
      nodeRid: 'node-a',
      nodeGeneration: 1n,
      owner: owner('owner-a', 1n),
      isLocal: true
    })
  });
  const generated = ['spot-remote-collision', 'spot-local-fresh'];
  const materialized: string[] = [];
  const manager = new ZLinkPublicSpotManager({
    local: {
      async getOrCreate(_mesh: string, _type: typeof RoomSpot, spotRid: string) {
        materialized.push(spotRid);
        return { spotRid, state: ZLinkSpotCreateState.Created };
      },
      async close() {
        return true;
      }
    } as never,
    coordinator,
    factories: new Map([[
      'mesh',
      new Map([[
        'room',
        { implementation: RoomSpot, relocation: { kind: 'disabled' } as never }
      ]])
    ]]) as never,
    resolver: () => undefined,
    isLocalNode: () => true,
    defaultTimeoutMs: 1_000,
    ridFactory: () => generated.shift() as never
  });

  const created = await manager.create('room').inMesh('mesh').submit();
  assert.equal(created.spot.spotRid, 'spot-local-fresh');
  assert.deepEqual(materialized, ['spot-local-fresh']);

  await assert.rejects(
    () => manager.getOrCreate('spot-remote-collision', 'room')
      .inMesh('mesh')
      .submit(),
    (error: unknown) => error instanceof Error
      && 'kind' in error
      && error.kind === 'requestFailed'
  );
});

test('User Spot manager close fails closed when authority resolves to a remote owner', async () => {
  const current = {
    spotRid: 'remote-close',
    objectGeneration: 1n,
    meshName: 'mesh',
    nodeRid: 'node-b'
  } as const;
  let localCloseCalled = false;
  const manager = new ZLinkPublicSpotManager({
    local: {
      async close() {
        localCloseCalled = true;
        return true;
      }
    } as never,
    coordinator: {
      async close(
        spot: typeof current,
        closeOwner: (resolved: typeof current) => Promise<boolean>
      ) {
        return await closeOwner(spot);
      }
    } as never,
    factories: new Map(),
    resolver: () => undefined,
    isLocalNode: () => false,
    defaultTimeoutMs: 1_000
  });
  await assert.rejects(
    () => manager.close(current),
    (error: unknown) => error instanceof Error
      && 'kind' in error
      && error.kind === 'requestFailed'
  );
  assert.equal(localCloseCalled, false);
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
    authorityKey: authorityKey('relocate'),
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
    authorityKey('relocate'),
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
    authorityKey: authorityKey('aggregate'),
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
      authorityKey: authorityKey('aggregate'),
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
  return encodeAuthorityKey('user_spot', value);
}

function owner(ownerId: string, generation: bigint): ZLinkLocationOwnerToken {
  return { ownerId, leaseGeneration: generation };
}
