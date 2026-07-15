const assert = require('node:assert/strict');
const test = require('node:test');
const framework = require('../../packages/framework/dist');
const internal = require('../../packages/framework/dist/internal');
const nest = require('../../packages/nestjs/dist');

test('all routing-id builders expose fixed and allocated identity policies', () => {
  const options = internal.createFrameworkOptions((builder) => {
    builder.useInMemoryLocationStores();
    builder.addClientServerChannel('api')
      .enableClient()
      .routingId('api-fixed');
    builder.addFanoutChannel('events')
      .enablePublisher('tcp://127.0.0.1:9101')
      .routingId('events-fixed');
    builder.addRouteMeshChannel('play')
      .enableServer('tcp://127.0.0.1:9102')
      .useAllocatedRoutingId(8, 'play-')
      .setRoutingIdAllocationGroup('game');
    builder.addSpotMesh('rooms')
      .enableRouter('tcp://127.0.0.1:9103')
      .useAllocatedRoutingId(8, 'rooms-')
      .setRoutingIdAllocationGroup('game');
  });

  assert.equal(options.channels.api.server.routingId, 'api-fixed');
  assert.equal(options.channels.events.routingId, 'events-fixed');
  assert.deepEqual(options.channels.play.routeMesh.routingIdAllocation, {
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
  const options = nest.zlinkFramework()
    .useInMemoryLocationStores()
    .addFanoutChannel('events')
      .enablePublisher('tcp://127.0.0.1:9111')
      .useAllocatedRoutingId(3, 'event-')
      .setRoutingIdAllocationGroup('workers')
    .addRouteMeshChannel('play')
      .enableClient()
      .useAllocatedRoutingId(3)
      .setRoutingIdAllocationGroup('workers')
    .build();
  assert.deepEqual(options.fanoutChannels.events.routingIdAllocation, {
    slotCount: 3,
    routingIdPrefix: 'event-',
    groupName: 'workers'
  });
  assert.deepEqual(options.routerMeshes.play.routingIdAllocation, {
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
      builder.useInMemoryLocationStores();
      builder.addRouteMeshChannel('play')
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
      builder.addSpotMesh('rooms').setRoutingIdAllocationGroup('   ');
    }),
    /group name/i
  );
});

test('in-memory routing-id slots assign the lowest slot, retry idempotently, and fence stale release', async () => {
  let now = new Date('2026-07-15T00:00:00.000Z');
  const store = new internal.ZLinkInMemoryLocationStore(() => now);
  const members = [
    { channelName: 'play', routingIdPrefix: 'play-' },
    { channelName: 'rooms', routingIdPrefix: 'rooms-' }
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
    members: [{ channelName: 'play', routingIdPrefix: 'play-' }],
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
    ownerLeaseRenewTimeoutMs: 3_000
  });
});

test('runtime allocates before socket identity and connect, publishes readiness, and releases after disposal', async () => {
  const calls = [];
  const store = new RecordingAllocationStore(calls);
  const options = internal.createFrameworkOptions((builder) => {
    builder.addLocationStore(store);
    builder.addRouteMeshChannel('play')
      .enableServer('tcp://127.0.0.1:9201')
      .useAllocatedRoutingId(4, 'play-')
      .setRoutingIdAllocationGroup('game');
  });
  options.channels.play.routeMesh.sendHandlers = [{
    packetName: 'Ping',
    handler: { async handle() {} }
  }];
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
    builder.addRouteMeshChannel('play')
      .enableServer('tcp://127.0.0.1:9202')
      .useAllocatedRoutingId(2);
  });
  options.channels.play.routeMesh.sendHandlers = [{
    packetName: 'Ping', handler: { async handle() {} }
  }];
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
    Object.assign(builder.configureLocations(), {
      heartbeatIntervalMs: 20,
      ownerLeaseTtlMs: 200,
      routingIdFencingMarginMs: 50,
      ownerLeaseRenewTimeoutMs: 10,
      pollingIntervalMs: 5
    });
    builder.addRouteMeshChannel('play')
      .enableServer('tcp://127.0.0.1:9203')
      .useAllocatedRoutingId(2);
  });
  options.channels.play.routeMesh.sendHandlers = [{
    packetName: 'Ping', handler: { async handle() {} }
  }];
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
  const socket = {
    nativeInstance: {},
    options: {},
    peerWeight: 100,
    sendHighWaterMark: 0,
    receiveHighWaterMark: 0,
    sendTimeoutMs: 0,
    maxMessageSize: 0,
    setChannelName() {},
    setRoutingId(value) { calls.push(`router:routing-id:${value}`); },
    bind() {
      calls.push('router:bind');
      if (failBind) throw new Error('bind failed');
    },
    connect() { calls.push('router:connect'); },
    disconnect() {},
    onSendReady() {},
    recv() { return undefined; },
    send() { return true; },
    request() { return true; },
    reply() { return { message() { return this; }, submit() {} }; },
    async dispose() { calls.push('router:dispose'); }
  };
  return {
    createChannelAdapter() {
      return {
        createContext() {
          return { nativeInstance: {}, async dispose() { calls.push('context:dispose'); } };
        },
        createRouterSocket() { return socket; },
        createDealerSocket() { throw new Error('not used'); },
        createPublisherSocket() { throw new Error('not used'); },
        createSubscriberSocket() { throw new Error('not used'); },
        createReadablePoller() { return { wait() { return false; }, dispose() {} }; },
        createTopicMessage() { return { parts: [] }; }
      };
    },
    createSpotAdapter() {
      return { createSpotNode() { throw new Error('not used'); } };
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

async function waitUntil(predicate, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (!predicate()) {
    if (Date.now() >= deadline) throw new Error('condition timed out');
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
}
