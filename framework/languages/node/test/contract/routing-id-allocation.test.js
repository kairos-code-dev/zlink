const assert = require('node:assert/strict');
const test = require('node:test');
const framework = require('../../packages/framework/dist');
const internal = require('../../packages/framework/dist/internal');
const nest = require('../../packages/nestjs/dist');

test('all routing-id builders expose fixed and allocated identity policies', () => {
  const store = new internal.ZLinkInMemoryLocationStore();
  const options = internal.createFrameworkOptions((builder) => {
    builder.addLocationStore(store);
    builder.addRouteMesh('api')
      .listen('tcp://127.0.0.1:9100')
      .routingId('api-fixed');
    builder.addFanoutChannel('events')
      .enablePublisher('tcp://127.0.0.1:9101')
      .routingId('events-fixed');
    builder.addRouteMesh('play')
      .listen('tcp://127.0.0.1:9102')
      .useAllocatedRoutingId(8, 'play-')
      .setRoutingIdAllocationGroup('game');
    builder.addRouteMesh('rooms')
      .listen('tcp://127.0.0.1:9103')
      .useAllocatedRoutingId(8, 'rooms-')
      .setRoutingIdAllocationGroup('game');
  });

  assert.equal(options.spotNodes.api.router.routingId, 'api-fixed');
  assert.equal(options.channels.events.routingId, 'events-fixed');
  assert.deepEqual(options.spotNodes.play.routingIdAllocation, {
    slotCount: 8,
    routingIdPrefix: 'play-',
    groupName: 'game'
  });
  assert.deepEqual(options.spotNodes.rooms.routingIdAllocation, {
    slotCount: 8,
    routingIdPrefix: 'rooms-',
    groupName: 'game'
  });
});

test('Nest builders and injection token preserve the allocated routing-id contract', () => {
  const store = new internal.ZLinkInMemoryLocationStore();
  const options = nest.zlinkFramework()
    .addLocationStore(store)
    .addFanoutChannel('events')
      .enablePublisher('tcp://127.0.0.1:9111')
      .useAllocatedRoutingId(3, 'event-')
      .setRoutingIdAllocationGroup('workers')
    .addRouteMesh('play')
      .listen('tcp://127.0.0.1:9112')
      .useAllocatedRoutingId(3)
      .setRoutingIdAllocationGroup('workers')
    .build();
  assert.deepEqual(options.fanoutChannels.events.routingIdAllocation, {
    slotCount: 3,
    routingIdPrefix: 'event-',
    groupName: 'workers'
  });
  assert.deepEqual(options.spotNodes.play.routingIdAllocation, {
    slotCount: 3,
    routingIdPrefix: 'play',
    groupName: 'workers'
  });
  assert.equal(
    nest.ZLINK_ALLOCATED_ROUTING_ID_PROVIDER,
    framework.ZLINK_ALLOCATED_ROUTING_ID_PROVIDER
  );
});

test('allocated routing-id builder rejects invalid declarations and fixed identity conflicts', () => {
  assert.throws(
    () => internal.createFrameworkOptions((builder) => {
      builder.addLocationStore(new internal.ZLinkInMemoryLocationStore());
      builder.addRouteMesh('play')
        .routingId('play-fixed')
        .useAllocatedRoutingId(2);
    }),
    framework.ZLinkConfigurationException
  );
  assert.throws(
    () => internal.createFrameworkOptions((builder) => {
      builder.addFanoutChannel('events').useAllocatedRoutingId(0);
    }),
    /slot count/i
  );
  assert.throws(
    () => internal.createFrameworkOptions((builder) => {
      builder.addRouteMesh('rooms').setRoutingIdAllocationGroup('   ');
    }),
    /group name/i
  );
});

test('in-memory routing-id slots assign the lowest slot, retry idempotently, and fence stale release', async () => {
  let now = new Date('2026-07-15T00:00:00.000Z');
  const store = new internal.ZLinkInMemoryLocationStore(() => now);
  const members = [
    { meshName: 'play', routingIdPrefix: 'play-' },
    { meshName: 'rooms', routingIdPrefix: 'rooms-' }
  ];
  const request = (ownerId) => ({
    groupName: 'game',
    members,
    slotCount: 2,
    ownerId,
    leaseTtlMs: 30_000
  });

  const first = await store.acquireRoutingIdSlot(request('owner-a'));
  assert.equal(first.kind, 'acquired');
  assert.equal(first.allocation.slot, 1);
  assert.equal(first.allocation.owner.generation, 1n);
  assert.equal((await store.acquireRoutingIdSlot(request('owner-a'))).allocation.slot, 1);

  const second = await store.acquireRoutingIdSlot(request('owner-b'));
  assert.equal(second.kind, 'acquired');
  assert.equal(second.allocation.slot, 2);
  assert.equal((await store.acquireRoutingIdSlot(request('owner-c'))).kind, 'groupExhausted');

  now = new Date('2026-07-15T00:00:31.000Z');
  const recycled = await store.acquireRoutingIdSlot(request('owner-c'));
  assert.equal(recycled.kind, 'acquired');
  assert.equal(recycled.allocation.slot, 1);
  assert.equal(recycled.allocation.owner.generation, 2n);
  assert.equal(
    await store.releaseRoutingIdSlot('game', 1, first.allocation.owner),
    'ignoredStale'
  );
  assert.equal(
    await store.releaseRoutingIdSlot('game', 1, recycled.allocation.owner),
    'released'
  );

  const snapshot = await store.listRoutingIdSlots('game');
  assert.equal(snapshot.slotCount, 2);
  assert.deepEqual(snapshot.members, members);
  assert.equal(snapshot.allocations.length, 0);
});

test('in-memory routing-id slots preserve group configuration and identity mode', async () => {
  const store = new internal.ZLinkInMemoryLocationStore();
  const base = {
    groupName: 'game',
    members: [{ meshName: 'play', routingIdPrefix: 'play-' }],
    slotCount: 2,
    ownerId: 'owner-a',
    leaseTtlMs: 30_000
  };
  assert.equal((await store.acquireRoutingIdSlot(base)).kind, 'acquired');
  assert.equal((await store.acquireRoutingIdSlot({
    ...base,
    ownerId: 'owner-b',
    slotCount: 3
  })).kind, 'groupConfigurationMismatch');
});

test('allocated routing-id location defaults match the common lease contract', () => {
  assert.deepEqual(framework.zlinkDefaultLocationOptions, {
    heartbeatIntervalMs: 10_000,
    ownerLeaseTtlMs: 30_000,
    pollingIntervalMs: 1_000,
    listPageSize: 1_000,
    storeFailureGraceMs: 30_000,
    routingIdFencingMarginMs: 5_000,
    ownerLeaseRenewTimeoutMs: 3_000,
    routeCacheMaxAgeMs: 15_000,
    relocationForwardingWindowMs: 30_000,
    maxActiveOutboundRelocations: 64,
    maxActiveInboundRelocations: 64,
    maxConcurrentRelocationCaptures: 8,
    maxConcurrentRelocationRestores: 8,
    maxRelocationPayloadInFlightBytes: 256 * 1024 * 1024
  });
});

test('runtime allocates before socket identity and connect, publishes readiness, and releases after disposal', async () => {
  const calls = [];
  const store = new RecordingAllocationStore(calls);
  const options = internal.createFrameworkOptions((builder) => {
    builder.addLocationStore(store);
    builder.addRouteMesh('play')
      .listen('tcp://127.0.0.1:9201')
      .useAllocatedRoutingId(4, 'play-')
      .setRoutingIdAllocationGroup('game');
  });
  const registration = internal.createFrameworkRegistration(options);
  const host = new internal.ZLinkFrameworkRuntimeHost(
    { registration },
    { backendAdapterFactory: allocationBackend(calls) }
  );

  const ready = host.waitForReadyAllocation('game');
  await host.start();
  const allocation = await ready;
  assert.equal(allocation.slot, 1);
  assert.equal(allocation.memberRoutingIds.get('play'), 'play-1');
  assert.ok(
    calls.indexOf('store:acquire') < calls.indexOf('router:routing-id:play-1'),
    calls.join(',')
  );
  assert.ok(
    calls.indexOf('router:routing-id:play-1') < calls.indexOf('router:bind'),
    calls.join(',')
  );

  await host.stop();
  assert.ok(calls.indexOf('router:dispose') < calls.indexOf('store:release'));
  assert.equal((await store.listRoutingIdSlots('game')).allocations.length, 0);
});

test('runtime bind failure disposes sockets before rolling back the allocated slot', async () => {
  const calls = [];
  const store = new RecordingAllocationStore(calls);
  const options = internal.createFrameworkOptions((builder) => {
    builder.addLocationStore(store);
    builder.addRouteMesh('play')
      .listen('tcp://127.0.0.1:9202')
      .useAllocatedRoutingId(2);
  });
  const host = new internal.ZLinkFrameworkRuntimeHost(
    { registration: internal.createFrameworkRegistration(options) },
    { backendAdapterFactory: allocationBackend(calls, true) }
  );

  const ready = host.waitForReadyAllocation('play');
  await assert.rejects(() => host.start(), /bind failed/);
  await assert.rejects(() => ready, /bind failed/);
  assert.equal(host.isStarted, false);
  assert.ok(calls.indexOf('router:dispose') < calls.indexOf('store:release'), calls.join(','));
  assert.equal((await store.listRoutingIdSlots('play')).allocations.length, 0);
});

test('runtime fences and closes allocated sockets before an unrenewed lease can expire', async () => {
  const calls = [];
  const store = new FailingRenewAllocationStore(calls);
  const options = internal.createFrameworkOptions((builder) => {
    builder.addLocationStore(store);
    builder.configureLocations()
      .heartbeatIntervalMs(20)
      .ownerLeaseTtlMs(200)
      .routingIdFencingMarginMs(50)
      .ownerLeaseRenewTimeoutMs(10)
      .pollingIntervalMs(5);
    builder.addRouteMesh('play')
      .listen('tcp://127.0.0.1:9203')
      .useAllocatedRoutingId(2);
  });
  const host = new internal.ZLinkFrameworkRuntimeHost(
    { registration: internal.createFrameworkRegistration(options) },
    { backendAdapterFactory: allocationBackend(calls) }
  );

  await host.start();
  await waitUntil(() => calls.includes('router:dispose') && calls.includes('store:release'), 1_000);
  assert.equal(host.isStarted, false);
  assert.ok(calls.includes('router:dispose'), calls.join(','));
  assert.ok(calls.includes('store:release'), calls.join(','));
});

test('runtime fences an allocated identity when the event loop resumes after its lease deadline', async () => {
  const calls = [];
  const store = new RecordingAllocationStore(calls);
  const options = internal.createFrameworkOptions((builder) => {
    builder.addLocationStore(store);
    builder.configureLocations()
      .heartbeatIntervalMs(20)
      .ownerLeaseTtlMs(200)
      .routingIdFencingMarginMs(50)
      .ownerLeaseRenewTimeoutMs(10)
      .pollingIntervalMs(5);
    builder.addRouteMesh('play')
      .listen('tcp://127.0.0.1:9204')
      .useAllocatedRoutingId(2);
  });
  const host = new internal.ZLinkFrameworkRuntimeHost(
    { registration: internal.createFrameworkRegistration(options) },
    { backendAdapterFactory: allocationBackend(calls) }
  );

  await host.start();
  const blockedUntil = performance.now() + 220;
  while (performance.now() < blockedUntil) {
    // Model a synchronous native stall that delays both heartbeat and fence timers.
  }
  await waitUntil(() => calls.includes('router:dispose') && calls.includes('store:release'), 1_000);
  assert.equal(host.isStarted, false);
});

class RecordingAllocationStore extends internal.ZLinkInMemoryLocationStore {
  constructor(calls) {
    super();
    this.calls = calls;
  }

  async acquireRoutingIdSlot(request, signal) {
    this.calls.push('store:acquire');
    return await super.acquireRoutingIdSlot(request, signal);
  }

  async releaseRoutingIdSlot(groupName, slot, owner, signal) {
    this.calls.push('store:release');
    return await super.releaseRoutingIdSlot(groupName, slot, owner, signal);
  }
}

class FailingRenewAllocationStore extends RecordingAllocationStore {
  renewCount = 0;

  async renewOwnerLease(...args) {
    this.renewCount += 1;
    if (this.renewCount > 1) return await new Promise(() => {});
    return await super.renewOwnerLease(...args);
  }
}

function allocationBackend(calls, failBind = false) {
  return {
    createChannelAdapter() {
      return {
        createContext() {
          return {
            nativeInstance: {},
            shutdown() {},
            async dispose() { calls.push('context:dispose'); }
          };
        },
        createRouterSocket() { throw new Error('not used'); },
        createDealerSocket() { throw new Error('not used'); },
        createPublisherSocket() { throw new Error('not used'); },
        createSubscriberSocket() { throw new Error('not used'); },
        createReadablePoller() { return { wait() { return false; }, dispose() {} }; },
        createTopicMessage() { return { parts: [] }; }
      };
    },
    createMeshAdapter() {
      return {
        createMeshNode(_context, options) {
          let routingId = options.routingId;
          if (routingId !== undefined) {
            calls.push(`router:routing-id:${routingId}`);
          }
          return {
            nativeInstance: {},
            setRoutingId(value) {
              routingId = value;
              calls.push(`router:routing-id:${value}`);
            },
            setBind() {
              calls.push('router:bind');
              if (failBind) throw new Error('bind failed');
            },
            addChannelName() {},
            setChannelWeight() {},
            start() {},
            createPublisher() {
              return {
                publish() { return true; },
                close() {}
              };
            },
            setReadyHandler() {},
            createReadyBatch() { return emptyReadyBatch(); },
            createReceiveBatch() { return emptyReceiveBatch(); },
            drainReady() { return { ok: false, hasResidue: false, records: [] }; },
            connectPeer() { return 1n; },
            removePeerConnection() {},
            disconnectPeer() {},
            status() { return { routingId, lifecycleGeneration: 1n }; },
            peers() { return []; },
            shutdown() {},
            close() { calls.push('router:dispose'); }
          };
        }
      };
    },
    createStreamAdapter() {
      return { createStreamSocket() { throw new Error('not used'); } };
    },
    createMonitoringAdapter() {
      return {
        openSocketMonitor() {
          return {
            nativeInstance: {},
            onEvent() {},
            recv() { return undefined; },
            async dispose() {}
          };
        }
      };
    }
  };
}

function emptyReadyBatch() {
  return {
    reset() {},
    takeClaim() { throw new Error('no ready records'); },
    close() {}
  };
}

function emptyReceiveBatch() {
  return {
    reset() {},
    close() {}
  };
}

async function waitUntil(predicate, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (!predicate()) {
    if (Date.now() >= deadline) throw new Error('condition timed out');
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
}
