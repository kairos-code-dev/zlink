const assert = require('node:assert/strict');
const test = require('node:test');

const framework = require('../../packages/framework/dist/internal');

test('runtime event publisher continues after a monitoring handler fails', async () => {
  const events = [];
  const publisher = new framework.DefaultZLinkRuntimeEventPublisher();
  publisher.register({ async handle() { throw new Error('handler failed'); } });
  publisher.register({ async handle(event) { events.push(event); } });
  const originalError = console.error;
  console.error = () => undefined;
  try {
    await publisher.publish({ sourceName: 'runtime', timestamp: new Date(), event: 'test' });
  } finally {
    console.error = originalError;
  }

  assert.equal(events.length, 1);
});

test('socket monitoring source maps backend raw events into framework typed events', async () => {
  const events = [];
  const publisher = new framework.DefaultZLinkRuntimeEventPublisher();
  publisher.register({ async handle(event) { events.push(event); } });
  const source = new framework.ZLinkSocketMonitoringSource(
    {
      sourceName: 'api.server',
      events: [framework.ZLinkSocketEventKind.ConnectionReady]
    },
    fakeSocketMonitor(),
    publisher
  );

  await source.publish({
    nativeEvent: framework.ZLinkSocketNativeEventType.Connected,
    routingId: 'peer-a',
    localAddr: 'tcp://local',
    remoteAddr: 'tcp://remote',
    value: 1
  });
  const opaqueRoutingId = {
    toHex() { return '706565722d61'; },
    toString() { return 'opaque backend object'; }
  };
  await source.publish({
    nativeEvent: framework.ZLinkSocketNativeEventType.ConnectionReady,
    routingId: opaqueRoutingId,
    localAddr: 'tcp://local',
    remoteAddr: 'tcp://remote',
    value: 2
  });

  assert.equal(events.length, 1);
  assert.equal(events[0].sourceName, 'api.server');
  assert.equal(events[0].event, framework.ZLinkSocketEventKind.ConnectionReady);
  assert.equal(events[0].routingId, '706565722d61');
  assert.equal(typeof events[0].routingId, 'string');
  assert.equal(events[0].diagnostic.nativeEvent, framework.ZLinkSocketNativeEventType.ConnectionReady);
  assert.equal(events[0].diagnostic.nativeValue, 2);
});

test('location runtime monitoring source publishes snapshot changes and suppresses unchanged polls', async () => {
  const events = [];
  let readyCount = 1;
  const publisher = new framework.DefaultZLinkRuntimeEventPublisher();
  publisher.register({ async handle(event) { events.push(event); } });
  const source = new framework.ZLinkLocationRuntimeMonitoringSource(
    { sourceName: 'location-runtime', intervalMs: 1000 },
    {
      async getStatus() {
        return {
          storeHealthy: true,
          watchEnabled: false,
          pollingIntervalMs: 1000,
          lastRefreshAt: new Date(readyCount),
          ownerLeaseHealthy: true,
          ownerLeaseRenewedAt: new Date(readyCount)
        };
      },
      async listTopology() {
        return { items: [locationTopologyEntry(readyCount)] };
      },
      async listServiceSummaries() {
        return [{
          meshName: 'api',
          autoConnectType: framework.ZLinkLocationAutoConnectType.RouteMesh,
          role: framework.ZLinkLocationRole.Router,
          totalCount: 1,
          readyCount,
          errorCount: 0,
          stoppedCount: 0,
          updatedAt: new Date(readyCount)
        }];
      },
      async listPeerLocations() {
        return [];
      },
      async listSpotLocations() {
        return { items: [] };
      },
      async listActorLocations() {
        return { items: [] };
      },
      async listRouteLocations() {
        return { items: [] };
      }
    },
    publisher
  );

  await source.pollOnce();
  await source.pollOnce();
  readyCount = 2;
  await source.pollOnce();

  assert.deepEqual(events.map((event) => event.event), [
    framework.ZLinkLocationRuntimeEventKind.StatusChanged,
    framework.ZLinkLocationRuntimeEventKind.TopologyChanged,
    framework.ZLinkLocationRuntimeEventKind.ServiceSummaryChanged,
    framework.ZLinkLocationRuntimeEventKind.StatusChanged,
    framework.ZLinkLocationRuntimeEventKind.TopologyChanged,
    framework.ZLinkLocationRuntimeEventKind.ServiceSummaryChanged
  ]);
  assert.equal(events[2].serviceSummary[0].readyCount, 1);
  assert.equal(events[5].serviceSummary[0].readyCount, 2);
});

test('location runtime monitoring source publishes StoreFailure and StoreRecovered', async () => {
  const events = [];
  let fail = true;
  const publisher = new framework.DefaultZLinkRuntimeEventPublisher();
  publisher.register({ async handle(event) { events.push(event); } });
  const query = {
    async getStatus() {
      if (fail) throw new Error('store unavailable');
      return {
        storeHealthy: true,
        watchEnabled: false,
        pollingIntervalMs: 1000,
        ownerLeaseHealthy: true
      };
    },
    async listTopology() { return { items: [] }; },
    async listServiceSummaries() { return []; }
  };
  const source = new framework.ZLinkLocationRuntimeMonitoringSource(
    { sourceName: 'location-runtime', intervalMs: 1000 },
    query,
    publisher
  );

  await source.pollOnce();
  await source.pollOnce();
  fail = false;
  await source.pollOnce();

  assert.equal(framework.ZLinkLocationRuntimeEventKind.StoreUnavailable, undefined);
  assert.deepEqual(events.slice(0, 2).map((event) => event.event), [
    framework.ZLinkLocationRuntimeEventKind.StoreFailure,
    framework.ZLinkLocationRuntimeEventKind.StoreRecovered
  ]);
});

test('location monitoring event emitter publishes registered row and resolve-miss events', async () => {
  const events = [];
  const publisher = new framework.DefaultZLinkRuntimeEventPublisher();
  publisher.register({ async handle(event) { events.push(event); } });
  const emitter = new framework.ZLinkLocationMonitoringEventEmitter({
    peer: { sourceName: 'location-peer' },
    spot: { sourceName: 'location-spot' },
    actor: { sourceName: 'location-actor' },
    route: { sourceName: 'location-route' }
  }, publisher);

  emitter.peerRowUpdated({
    kind: framework.ZLinkLocationKind.Peer,
    key: {
      autoConnectType: framework.ZLinkLocationAutoConnectType.RouteMesh,
      meshName: 'api',
      role: framework.ZLinkLocationRole.Router,
      endpoint: 'tcp://127.0.0.1:7001'
    }
  }, peerRow());
  emitter.spotResolveMiss({ meshName: 'game', spotId: 'spot-1' });
  emitter.actorResolveMiss({ actorType: 'GameActor', actorId: 'room-1' });
  emitter.routeRowRemoved({ routeKind: framework.ZLinkRouteKind.FrameworkRoute, routeKey: 'api' });
  await Promise.resolve();

  assert.deepEqual(events.map((event) => [event.sourceName, event.event]), [
    ['location-peer', framework.ZLinkLocationPeerEventKind.RowUpdated],
    ['location-spot', framework.ZLinkLocationSpotEventKind.ResolveMiss],
    ['location-actor', framework.ZLinkLocationActorEventKind.ResolveMiss],
    ['location-route', framework.ZLinkLocationRouteEventKind.RowRemoved]
  ]);
  assert.equal(events[0].peer.endpoint, 'tcp://127.0.0.1:7001');
  assert.equal(events[1].key.meshName, 'game');
  assert.equal(events[2].key.actorId, 'room-1');
  assert.equal(events[3].key.routeKey, 'api');
});

test('mesh monitoring source adapts formal peer endpoint into the public spot event contract', async () => {
  const events = [];
  const publisher = new framework.DefaultZLinkRuntimeEventPublisher();
  publisher.register({ async handle(event) { events.push(event); } });
  const source = new framework.ZLinkMeshMonitoringSource(
    { sourceName: 'room-route-mesh', intervalMs: 1000 },
    {
      status() {
        return {
          state: 2,
          routingId: 'room-node',
          meshName: 'room',
          localEndpoint: 'tcp://room',
          lifecycleGeneration: 1n,
          descriptorRevision: 1n,
          channelCount: 1,
          configuredPeerCount: 1,
          admittedPeerCount: 1,
          drainingPeerCount: 0,
          pendingApplicationMessages: 0n,
          pendingInfrastructureMessages: 0n,
          pendingBytes: 0n,
          lastError: 0,
          lastChangedMs: 2n
        };
      },
      peers() {
        return [{
          connectionIntentId: 1n,
          source: framework.ZLinkSpotPeerSource.Manual,
          state: 3,
          routingId: 'play-node',
          lifecycleGeneration: 1n,
          descriptorRevision: 1n,
          endpoint: 'tcp://play',
          channelCount: 1,
          lastError: 0,
          lastChangedMs: 2n
        }];
      },
      peerChannels() {
        return { names: ['bingo.room'], weights: [3] };
      }
    },
    publisher,
    { meshChannels: { 'bingo.room': { weight: 2 } } }
  );

  await source.pollOnce();

  const statusEvent = events.find((event) => event.event === framework.ZLinkSpotEventKind.StatusChanged);
  const peerEvent = events.find((event) => event.event === framework.ZLinkSpotEventKind.PeersChanged);
  assert.equal(statusEvent.status.meshName, 'room');
  assert.equal(statusEvent.status.rid, 'room-node');
  assert.equal(statusEvent.status.channels[0].channelName, 'bingo.room');
  assert.equal(statusEvent.status.channels[0].localWeight, 2);
  assert.equal(statusEvent.status.channels[0].readyMemberCount, 1n);
  assert.equal(peerEvent.peers[0].endpoint, 'tcp://play');
  assert.equal(peerEvent.peers[0].ready, true);
  assert.equal(peerEvent.peers[0].admissionState, 'ready');
  assert.deepEqual(peerEvent.peers[0].channelNames, ['bingo.room']);
});

test('mesh monitoring preserves a connecting peer before its routing identity is known', async () => {
  const events = [];
  let peerChannelQueries = 0;
  const publisher = new framework.DefaultZLinkRuntimeEventPublisher();
  publisher.register({ async handle(event) { events.push(event); } });
  const source = new framework.ZLinkMeshMonitoringSource(
    { sourceName: 'room-route-mesh', intervalMs: 1000 },
    {
      status() {
        return {
          state: 2,
          routingId: 'room-node',
          meshName: 'room',
          localEndpoint: 'tcp://room',
          lifecycleGeneration: 1n,
          descriptorRevision: 1n,
          channelCount: 1,
          configuredPeerCount: 1,
          admittedPeerCount: 0,
          drainingPeerCount: 0,
          pendingApplicationMessages: 0n,
          pendingInfrastructureMessages: 0n,
          pendingBytes: 0n,
          lastError: 0,
          lastChangedMs: 2n
        };
      },
      peers() {
        return [{
          connectionIntentId: 1n,
          source: framework.ZLinkSpotPeerSource.Manual,
          state: 2,
          routingId: null,
          lifecycleGeneration: 0n,
          descriptorRevision: 0n,
          endpoint: 'tcp://play',
          channelCount: 0,
          lastError: 0,
          lastChangedMs: 2n
        }];
      },
      peerChannels() {
        peerChannelQueries += 1;
        throw new Error('A connecting peer has no routing identity for a channel query.');
      }
    },
    publisher
  );

  await source.pollOnce();

  assert.equal(peerChannelQueries, 0);
  const peerEvent = events.find((event) => event.event === framework.ZLinkSpotEventKind.PeersChanged);
  assert.equal(peerEvent.peers.length, 1);
  assert.equal(peerEvent.peers[0].ready, false);
  assert.equal(peerEvent.peers[0].admissionState, 'connecting');
  assert.equal(peerEvent.peers[0].endpoint, 'tcp://play');
  assert.deepEqual(peerEvent.peers[0].channelNames, []);
});

test('spot timer reports handler failure immediately through runtime publisher', async () => {
  const events = [];
  const publisher = new framework.DefaultZLinkRuntimeEventPublisher();
  publisher.register({ async handle(event) { events.push(event); } });
  const timer = new framework.ZLinkManagedTimer(
    'idle',
    1,
    {
      overrunPolicy: framework.ZLinkTimerOverrunPolicy.DelayNextTick,
      maxCatchUpTicks: 1,
      stopOnUnhandledException: true
    },
    async () => {
      throw new TypeError('timer failed');
    },
    async (tick, cause) => {
      await publisher.publish({
        sourceName: 'stage-node',
        timestamp: new Date(),
        event: framework.ZLinkSpotEventKind.TimerHandlerFailed,
        timerDiagnostic: {
          spotId: 'stage-node',
          isEntrySpot: true,
          timerName: 'idle',
          handlerType: 'IdleTimerHandler',
          deliveryIndex: tick.deliveryIndex,
          scheduledIndex: tick.scheduledIndex,
          exceptionType: cause.name,
          exceptionMessage: cause.message
        }
      });
    }
  );

  try {
    await waitFor(() => events.length >= 1, 1000);
  } finally {
    await timer.dispose();
  }

  assert.equal(events[0].event, framework.ZLinkSpotEventKind.TimerHandlerFailed);
  assert.equal(events[0].timerDiagnostic.timerName, 'idle');
  assert.equal(events[0].timerDiagnostic.exceptionType, 'TypeError');
});

function fakeSocketMonitor() {
  return {
    nativeInstance: {},
    onEvent() {},
    recv() {},
    async dispose() {}
  };
}

function locationTopologyEntry(readyCount) {
  return {
    kind: framework.ZLinkLocationKind.Peer,
    meshName: 'api',
    role: framework.ZLinkLocationRole.Router,
    nodeRid: 'peer-a',
    endpoint: 'tcp://peer:7101',
    state: framework.ZLinkLocationTopologyState.Ready,
    desiredCount: 1,
    readyCount,
    errorCode: 0,
    updatedAt: new Date(readyCount)
  };
}

function peerRow() {
  return {
    autoConnectType: framework.ZLinkLocationAutoConnectType.RouteMesh,
    meshName: 'api',
    role: framework.ZLinkLocationRole.Router,
    endpoint: 'tcp://127.0.0.1:7001',
    weight: 1,
    value: 0n,
    ownerId: 'owner-a',
    generation: 1n,
    updatedAt: new Date(1)
  };
}

async function waitFor(predicate, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (predicate()) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 5));
  }
  assert.fail('Timed out waiting for predicate.');
}
