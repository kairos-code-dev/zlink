const assert = require('node:assert/strict');
const test = require('node:test');

const zlink = require('@zlink-systems/zlink');
const framework = require('../../packages/framework/dist/internal');
const flowContext = require('../../packages/framework/dist/runtime/diagnostics/flow-context');
const nestjs = require('../../packages/nestjs/dist');

test('framework and NestJS builders register separate location and relocation stores', async () => {
  const store = new framework.ZLinkInMemoryLocationStore();
  const relocationStore = {};

  const options = framework.createFrameworkOptions((builder) => {
    builder.addLocationStore(store);
    builder.addRelocationStore(relocationStore);
    builder.configureLocations().heartbeatIntervalMs(123);
  });
  const registration = framework.createFrameworkRegistration(options);
  assert.equal(registration.locations.storeInstance, store);
  assert.equal(registration.locations.relocationStoreInstance, relocationStore);
  assert.equal(registration.locations.options.heartbeatIntervalMs, 123);

  const nestBuilder = nestjs.zlinkFramework()
    .addLocationStore(store)
    .addRelocationStore(relocationStore);
  nestBuilder.configureLocations().ownerLeaseTtlMs(456);
  const nestModule = nestjs.ZLinkModule.forRoot(nestBuilder.build());
  const nestRegistration = await resolveFrameworkRegistration(nestModule);
  assert.equal(nestRegistration.locations.storeInstance, store);
  assert.equal(nestRegistration.locations.relocationStoreInstance, relocationStore);
  assert.equal(nestRegistration.locations.options.ownerLeaseTtlMs, 456);

});

test('framework runtime host uses one explicit location store for every runtime role', () => {
  const store = new framework.ZLinkInMemoryLocationStore();
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({
      locations: {
        storeInstance: store
      }
    })
  });

  const actorOptions = runtime.createActorManagerOptions();
  const spotOptions = runtime.createSpotManagerOptions();

  assert.equal(typeof actorOptions.locationLifecycle, 'object');
  assert.equal(spotOptions.locationLifecycle, actorOptions.locationLifecycle);
});

test('framework runtime host starts location runtime and injects lifecycle into managers', async () => {
  const store = new framework.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const calls = [];
  const removePeer = store.removePeer.bind(store);
  store.removePeer = async (...args) => {
    calls.push('peer:remove');
    return await removePeer(...args);
  };
  const nodeRid = rid('node-a');
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({
      locations: {
        storeInstance: store,
        options: {
          heartbeatIntervalMs: 1000
        }
      },
      spotNodes: {
        play: {
          router: { bind: 'tcp://127.0.0.1:9101', routingId: 'node-a' }
        }
      }
    })
  }, {
    backendAdapterFactory: fakeBackendAdapterFactory(calls, nodeRid)
  });

  const preStartActorOptions = runtime.createActorManagerOptions();
  const preStartSpotOptions = runtime.createSpotManagerOptions();
  assert.equal(typeof preStartActorOptions.locationLifecycle, 'object');
  assert.equal(preStartSpotOptions.locationLifecycle, preStartActorOptions.locationLifecycle);
  assert.equal((await store.listOwnerLeases()).leases.length, 0);

  await runtime.start();

  const leases = await store.listOwnerLeases();
  assert.equal(leases.leases.length, 1);
  assert.equal(leases.leases[0].nodeRid, 'node-a');

  const actorOptions = runtime.createActorManagerOptions();
  const spotOptions = runtime.createSpotManagerOptions();
  assert.equal(actorOptions.locationLifecycle, preStartActorOptions.locationLifecycle);
  assert.equal(spotOptions.locationLifecycle, actorOptions.locationLifecycle);
  assert.equal(String(spotOptions.nodeRidProvider('play')), 'node-a');
  assert.equal(typeof spotOptions.nodeGenerationProvider('play'), 'bigint');
  assert.equal(typeof spotOptions.spotRouteResolver?.resolve, 'function');

  await actorOptions.locationLifecycle.claimActor('player', 'actor-1', nodeRid);
  await spotOptions.locationLifecycle.claimSpot(
    'play',
    rid('spot-1'),
    'StageSpot',
    nodeRid,
    framework.ZLinkSpotKind.User,
    7n,
    3n
  );
  assert.notEqual(await store.resolveActor({ meshName: 'play', actorId: 'actor-1' }), undefined);
  assert.notEqual(await store.resolveSpot({ meshName: 'play', spotRid: rid('spot-1') }), undefined);
  const routeTarget = await spotOptions.spotRouteResolver.resolve(rid('spot-1'));
  assert.equal(routeTarget.routerChannelId, 'play');
  assert.equal(routeTarget.targetNodeRid.toHex(), nodeRid.toHex());

  await runtime.stop();

  assert.equal((await store.listOwnerLeases()).leases.length, 0);
  assert.equal(await store.resolveActor({ meshName: 'play', actorId: 'actor-1' }), undefined);
  assert.equal(await store.resolveSpot({ meshName: 'play', spotRid: rid('spot-1') }), undefined);
  assert.deepEqual(calls, [
    'spot:dispose',
    'peer:remove',
    'context:dispose'
  ]);
});

test('framework host startup begins a lifecycle flow', async () => {
  const calls = [];
  const backendAdapterFactory = fakeBackendAdapterFactory(calls, rid('lifecycle-node'));
  const createChannelAdapter = backendAdapterFactory.createChannelAdapter;
  let startupFlow;
  backendAdapterFactory.createChannelAdapter = () => {
    startupFlow = flowContext.currentOrCreateFlow('Lifecycle', false);
    return createChannelAdapter();
  };
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({
      dispatch: {
        diagnostics: {
          messageFlow: framework.ZLinkMessageFlowLogMode.KeyTransitions,
          sampleRate: 1,
          includeMessageSizes: false
        }
      }
    })
  }, { backendAdapterFactory });

  await runtime.start();
  await runtime.stop();

  assert.equal(startupFlow?.flowOrigin, 'Lifecycle');
  assert.match(startupFlow?.flowId ?? '', /^[0-9a-f-]{36}$/);
});

test('framework runtime host starts channel auto-connect loops from location peers', async () => {
  const store = new framework.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const calls = [];
  const nodeRid = rid('node-a');

  await store.claimOwnerLease('owner-remote', 30000);
  await store.updatePeer(
    peer('api', 'owner-remote', 'node-b', 'tcp://remote-api'),
    framework.ZLinkLocationWriteIntent.NewClaim
  );
  await store.updatePeer(
    peer('manual', 'owner-remote', 'node-b', 'tcp://remote-manual'),
    framework.ZLinkLocationWriteIntent.NewClaim
  );
  await store.updatePeer(
    routePeer('mesh', 'owner-remote', 'node-b', 'tcp://remote-route'),
    framework.ZLinkLocationWriteIntent.NewClaim
  );
  await store.updatePeer(
    routePeer('manual-mesh', 'owner-remote', 'node-b', 'tcp://remote-manual-route'),
    framework.ZLinkLocationWriteIntent.NewClaim
  );
  await store.updatePeer(
    fanoutPeer('events', 'owner-remote', 'node-b', 'tcp://remote-events'),
    framework.ZLinkLocationWriteIntent.NewClaim
  );
  await store.updatePeer(
    fanoutPeer('manual-events', 'owner-remote', 'node-b', 'tcp://remote-manual-events'),
    framework.ZLinkLocationWriteIntent.NewClaim
  );

  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({
      locations: {
        storeInstance: store,
        options: {
          heartbeatIntervalMs: 1000,
          pollingIntervalMs: 1000
        }
      },
      channels: {
        api: {
          client: {}
        },
        manual: {
          client: {
            manualConnections: ['tcp://manual']
          }
        },
        events: {
          subscriber: {},
          publishHandlers: [{ packetName: 'Event', handler: { async handle() {} } }]
        },
        'local-events': {
          routingId: 'node-a',
          publisher: { bind: 'tcp://local-events' }
        },
        'manual-events': {
          subscriber: { manualConnections: ['tcp://manual-events'] },
          publishHandlers: [{ packetName: 'Event', handler: { async handle() {} } }]
        }
      },
      routeChannels: [{
        routerChannelId: 'mesh',
        bind: 'tcp://local-route',
        routingId: 'node-a'
      }, {
        routerChannelId: 'manual-mesh',
        bind: 'tcp://local-manual-route',
        routingId: 'node-a',
        manualConnections: ['tcp://manual-route']
      }],
      spotNodes: {
        play: {
          router: { bind: 'tcp://127.0.0.1:9101', routingId: 'node-a' }
        }
      }
    })
  }, {
    backendAdapterFactory: fakeBackendAdapterFactory(calls, nodeRid)
  });

  try {
    await runtime.start();
    assert.deepEqual(
      calls.filter((call) =>
        (call.startsWith('dealer:') || call.startsWith('router:') || call.startsWith('subscriber:'))
        && call.includes(':connect:')).sort(),
      [
        'dealer:api:connect:tcp://remote-api',
        'dealer:manual:connect:tcp://manual',
        'router:manual-mesh:connect:tcp://manual-route',
        'router:mesh:connect:tcp://remote-route',
        'subscriber:manual-events:connect:tcp://manual-events',
        'subscriber:events:connect:tcp://remote-events'
      ].sort()
    );
    const routeBind = calls.indexOf('router:mesh:bind:tcp://local-route');
    const routeDialStart = calls.indexOf(`router:mesh:connectRoutingId:${rid('node-b').toHex()}`);
    const routeProbe = calls.indexOf('router:mesh:probe:true');
    const routeConnect = calls.indexOf('router:mesh:connect:tcp://remote-route');
    assert.ok(routeBind >= 0);
    assert.ok(routeDialStart > routeBind);
    assert.ok(routeProbe > routeDialStart);
    assert.ok(routeConnect > routeProbe);
    const publisherRows = await store.listPeers({
      autoConnectType: framework.ZLinkLocationAutoConnectType.Fanout,
      meshName: 'local-events',
      role: framework.ZLinkLocationRole.Pub
    });
    assert.equal(publisherRows.length, 1);
    assert.equal(String(publisherRows[0].nodeRid), 'node-a');
  } finally {
    await runtime.stop();
  }
  assert.ok(calls.includes('dealer:api:disconnect:tcp://remote-api'));
  assert.ok(calls.includes('router:mesh:disconnect:tcp://remote-route'));
  assert.ok(calls.includes('subscriber:events:disconnect:tcp://remote-events'));
  assert.equal(calls.some((call) => call.includes('tcp://remote-manual')), false);
  assert.equal(
    calls.filter((call) => call === 'dealer:manual:disconnect:tcp://manual').length,
    0
  );
});

test('manual Mesh router connection suppresses only the matching store-driven route', async () => {
  const store = new framework.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const calls = [];
  const nodeRid = rid('node-a');

  await store.claimOwnerLease('owner-remote', 30000);
  await store.updatePeer(
    spotPeer('owner-remote', 'node-b', 'tcp://remote-spot', 'tcp://remote-pub'),
    framework.ZLinkLocationWriteIntent.NewClaim
  );
  await store.updatePeer(
    spotPeer('owner-lower', 'node-0', 'tcp://lower-spot', 'tcp://lower-pub'),
    framework.ZLinkLocationWriteIntent.NewClaim
  );
  await store.updatePeer(
    spotPeer('owner-remote', 'node-c', 'tcp://manual-spot', 'tcp://manual-pub'),
    framework.ZLinkLocationWriteIntent.NewClaim
  );

  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({
      locations: {
        storeInstance: store,
        options: {
          heartbeatIntervalMs: 1000,
          pollingIntervalMs: 1000
        }
      },
      spotNodes: {
        play: {
          router: {
            bind: 'tcp://local-spot',
            routingId: 'node-a',
            manualConnections: ['tcp://manual-spot']
          },
          pubSub: {
            bind: 'tcp://local-pub',
            routingId: 'node-a'
          }
        }
      }
    })
  }, {
    backendAdapterFactory: fakeBackendAdapterFactory(calls, nodeRid)
  });

  try {
    await runtime.start();
    await new Promise((resolve) => setImmediate(resolve));
    assert.equal(calls.some((call) => call.includes('tcp://remote-spot')), false);
    assert.ok(calls.includes('spot:connectPeer:tcp://remote-pub'));
    assert.equal(calls.some((call) => call.includes('tcp://lower-spot')), false);
    assert.equal(calls.some((call) => call.includes('tcp://lower-pub')), false);
    assert.ok(calls.includes('spot:connectPeer:tcp://manual-spot'));
  } finally {
    await runtime.stop();
  }

  assert.equal(calls.filter((call) => call === 'spot:disconnectPeer:tcp://manual-spot').length, 0);
});

async function resolveFrameworkRegistration(module) {
  const provider = module.providers.find((entry) => entry.provide === nestjs.ZLINK_FRAMEWORK_REGISTRATION);
  if ('useValue' in provider) {
    return provider.useValue;
  }
  return await provider.useFactory();
}

function rid(value) {
  return zlink.RoutingId.from(value);
}

function fakeBackendAdapterFactory(calls, nodeRid) {
  return {
    createChannelAdapter() {
      return {
        createContext() {
          return {
            nativeInstance: {},
            shutdown() {},
            async dispose() {
              calls.push('context:dispose');
            }
          };
        },
        createDealerSocket() {
          return fakeConnectableSocket(calls, 'dealer');
        },
        createRouterSocket() {
          const socket = fakeConnectableSocket(calls, 'router');
          return Object.assign(socket, {
            setRoutingId() {},
            recv() { return null; },
            reply() { return { message() { return this; }, submit() {} }; }
          });
        },
        createPublisherSocket() {
          return {
            nativeInstance: {},
            setChannelName(channelName) { this.channelName = channelName; },
            bind(endpoint) { calls.push(`publisher:${this.channelName ?? ''}:bind:${endpoint}`); },
            onSendReady() {},
            publish() { return true; },
            async dispose() {}
          };
        },
        createSubscriberSocket() {
          return {
            ...fakeConnectableSocket(calls, 'subscriber'),
            setSubscription() {},
            subscribe() { return false; }
          };
        },
        createReadablePoller() {
          return {
            wait() { return false; },
            dispose() {}
          };
        },
        createTopicMessage() {
          return { parts: [] };
        }
      };
    },
    createMeshAdapter() {
      return {
        createMeshNode(_context, options) {
          let routingId = options.routingId === undefined ? nodeRid : rid(options.routingId);
          let nextConnectionIntent = 1n;
          return {
            nativeInstance: {},
            setRoutingId(value) { routingId = value; },
            setBind() {},
            addChannelName() {},
            setChannelWeight() {},
            start() {},
            setReadyHandler() {},
            createReadyBatch() { return fakeReadyBatch(); },
            createReceiveBatch() { return fakeReceiveBatch(); },
            drainReady() { return { ok: false, hasResidue: false, records: [] }; },
            connectPeer({ endpoint, expectedRid }) {
              calls.push(`spot:connectPeer:${endpoint}`);
              if (expectedRid !== undefined) {
                calls.push(`spot:connectPeerRid:${expectedRid.toHex()}:${endpoint}`);
              }
              return nextConnectionIntent++;
            },
            removePeerConnection(connectionIntent) {
              calls.push(`spot:removePeerConnection:${connectionIntent}`);
            },
            disconnectPeer(peerRid) { calls.push(`spot:disconnectPeerRid:${peerRid.toHex()}`); },
            status() { return { routingId, lifecycleGeneration: 3n }; },
            peers() { return []; },
            shutdown() {},
            close() {
              calls.push('spot:dispose');
            }
          };
        }
      };
    },
    createStreamAdapter() {
      return {
        createStreamSocket() {
          throw new Error('not used');
        }
      };
    },
    createMonitoringAdapter() {
      return {
        openSocketMonitor() {
          return {
            nativeInstance: {},
            onEvent() {},
            recv() { return null; },
            status() { return {}; },
            async dispose() {}
          };
        }
      };
    }
  };
}

function fakeReadyBatch() {
  return {
    reset() {},
    takeClaim() { throw new Error('no ready records'); },
    close() {}
  };
}

function fakeReceiveBatch() {
  return {
    reset() {},
    close() {}
  };
}

function fakeConnectableSocket(calls, kind) {
  const socket = {
    nativeInstance: {},
    channelName: '',
    peerWeight: 100,
    sendHighWaterMark: 0,
    receiveHighWaterMark: 0,
    sendTimeoutMs: 0,
    setChannelName(channelName) {
      this.channelName = channelName;
    },
    bind(endpoint) {
      calls.push(`${kind}:${this.channelName}:bind:${endpoint}`);
    },
    connect(endpoint) {
      calls.push(`${kind}:${this.channelName}:connect:${endpoint}`);
    },
    disconnect(endpoint) {
      calls.push(`${kind}:${this.channelName}:disconnect:${endpoint}`);
    },
    attachDiscovery() {},
    onSendReady() {},
    send() { return true; },
    request() { return true; },
    recv() { return null; },
    async dispose() {}
  };
  if (kind !== 'router') {
    return socket;
  }
  socket.options = {
    set probe(value) {
      calls.push(`${kind}:${socket.channelName}:probe:${value}`);
    },
    setConnectRoutingId(routingId) {
      calls.push(`${kind}:${socket.channelName}:connectRoutingId:${routingId.toHex()}`);
    }
  };
  return socket;
}

function peer(meshName, ownerId, nodeRid, endpoint) {
  return {
    autoConnectType: framework.ZLinkLocationAutoConnectType.ClientServer,
    meshName,
    nodeRid: rid(nodeRid),
    role: framework.ZLinkLocationRole.Router,
    endpoint,
    weight: 100,
    value: 0n,
    ownerId,
    generation: 0n,
    updatedAt: new Date(0)
  };
}

function routePeer(meshName, ownerId, nodeRid, endpoint) {
  return {
    autoConnectType: framework.ZLinkLocationAutoConnectType.RouteMesh,
    meshName,
    nodeRid: rid(nodeRid),
    role: framework.ZLinkLocationRole.Router,
    endpoint,
    weight: 100,
    value: 0n,
    ownerId,
    generation: 0n,
    updatedAt: new Date(0)
  };
}

function fanoutPeer(meshName, ownerId, nodeRid, endpoint) {
  return {
    autoConnectType: framework.ZLinkLocationAutoConnectType.Fanout,
    meshName,
    nodeRid: rid(nodeRid),
    role: framework.ZLinkLocationRole.Pub,
    endpoint,
    weight: 100,
    value: 0n,
    ownerId,
    generation: 0n,
    updatedAt: new Date(0)
  };
}

function spotPeer(ownerId, nodeRid, endpoint, pubEndpoint) {
  return {
    autoConnectType: framework.ZLinkLocationAutoConnectType.SpotMesh,
    meshName: 'play',
    nodeRid: rid(nodeRid),
    role: framework.ZLinkLocationRole.Spot,
    endpoint,
    weight: 100,
    value: 0n,
    metadata: { 'pub-endpoint': pubEndpoint },
    ownerId,
    generation: 0n,
    updatedAt: new Date(0)
  };
}
