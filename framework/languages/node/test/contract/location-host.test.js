const assert = require('node:assert/strict');
const test = require('node:test');

const zlink = require('@zlink-systems/zlink');
const framework = require('../../packages/framework/dist/internal');
const nestjs = require('../../packages/nestjs/dist');

test('framework and NestJS builders register in-memory and integrated location stores', async () => {
  const store = new framework.ZLinkInMemoryLocationStore();

  const options = framework.createFrameworkOptions((builder) => {
    builder.useInMemoryLocationStores();
    const locationOptions = builder.configureLocations();
    locationOptions.heartbeatIntervalMs = 123;
  });
  const registration = framework.createFrameworkRegistration(options);
  assert.equal(registration.locations.useInMemoryStores, true);
  assert.equal(registration.locations.options.heartbeatIntervalMs, 123);

  const nestBuilder = nestjs.zlinkFramework().addLocationStore(store);
  nestBuilder.configureLocations().ownerLeaseTtlMs = 456;
  const nestModule = nestjs.ZLinkModule.forRoot(nestBuilder.build());
  const nestRegistration = await resolveFrameworkRegistration(nestModule);
  assert.equal(nestRegistration.locations.storeInstance, store);
  assert.equal(nestRegistration.locations.options.ownerLeaseTtlMs, 456);

  assert.throws(
    () => framework.createFrameworkRegistration({
      locations: {
        useInMemoryStores: true,
        storeInstance: store
      }
    }),
    /In-memory location stores cannot be combined/
  );
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
  assert.equal(leases.leases[0].nodeRid.toHex(), nodeRid.toHex());

  const actorOptions = runtime.createActorManagerOptions();
  const spotOptions = runtime.createSpotManagerOptions();
  assert.equal(actorOptions.locationLifecycle, preStartActorOptions.locationLifecycle);
  assert.equal(spotOptions.locationLifecycle, actorOptions.locationLifecycle);
  assert.equal(spotOptions.locationMeshName, 'play');
  assert.equal(typeof spotOptions.spotRouteResolver?.resolve, 'function');

  await actorOptions.locationLifecycle.claimActor('player', 'actor-1', nodeRid);
  await spotOptions.locationLifecycle.claimSpot('play', rid('spot-1'), 'StageSpot', nodeRid, framework.ZLinkSpotKind.User);
  assert.notEqual(await store.resolveActor({ actorType: 'player', actorId: 'actor-1' }), undefined);
  assert.notEqual(await store.resolveSpot({ meshName: 'play', spotRid: rid('spot-1') }), undefined);
  const routeTarget = await spotOptions.spotRouteResolver.resolve(rid('spot-1'));
  assert.equal(routeTarget.routerChannelId, 'play');
  assert.equal(routeTarget.targetNodeRid.toHex(), nodeRid.toHex());

  await runtime.stop();

  assert.equal((await store.listOwnerLeases()).leases.length, 0);
  assert.equal(await store.resolveActor({ actorType: 'player', actorId: 'actor-1' }), undefined);
  assert.equal(await store.resolveSpot({ meshName: 'play', spotRid: rid('spot-1') }), undefined);
  assert.deepEqual(calls, [
    'spot:dispose',
    'context:dispose'
  ]);
});

test('framework runtime host starts channel auto-connect loops from location peers', async () => {
  const store = new framework.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const calls = [];
  const nodeRid = rid('node-a');

  await store.renewOwnerLease('owner-remote', rid('node-b'), 30000);
  await store.updatePeer(
    peer('api', 'owner-remote', 'node-b', 'tcp://remote-api'),
    framework.ZLinkLocationWriteIntent.NewClaim
  );
  await store.updatePeer(
    peer('manual', 'owner-remote', 'node-b', 'tcp://manual'),
    framework.ZLinkLocationWriteIntent.NewClaim
  );
  await store.updatePeer(
    routePeer('owner-remote', 'node-b', 'tcp://remote-route'),
    framework.ZLinkLocationWriteIntent.NewClaim
  );
  await store.updatePeer(
    fanoutPeer('events', 'owner-remote', 'node-b', 'tcp://remote-events'),
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
          subscriber: {}
        }
      },
      routeChannels: [{
        routerChannelId: 'mesh',
        bind: 'tcp://local-route',
        routingId: 'node-a'
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
        'router:mesh:connect:tcp://remote-route',
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
  } finally {
    await runtime.stop();
  }
  assert.ok(calls.includes('dealer:api:disconnect:tcp://remote-api'));
  assert.ok(calls.includes('router:mesh:disconnect:tcp://remote-route'));
  assert.ok(calls.includes('subscriber:events:disconnect:tcp://remote-events'));
  assert.equal(
    calls.filter((call) => call === 'dealer:manual:disconnect:tcp://manual').length,
    0
  );
});

test('manual SPOT roles keep location lookup without store-driven peer connections', async () => {
  const store = new framework.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const calls = [];
  const nodeRid = rid('node-a');

  await store.renewOwnerLease('owner-remote', rid('node-b'), 30000);
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
            routingId: 'node-a',
            manualConnections: ['tcp://manual-pub']
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
    assert.equal(calls.some((call) => call.includes('tcp://remote-pub')), false);
    assert.equal(calls.some((call) => call.includes('tcp://lower-spot')), false);
    assert.equal(calls.some((call) => call.includes('tcp://lower-pub')), false);
    assert.ok(calls.includes('spot:connectPeer:tcp://manual-spot'));
    assert.ok(calls.includes('spot:connectPeer:tcp://manual-pub'));
  } finally {
    await runtime.stop();
  }

  assert.equal(calls.some((call) => call.includes('disconnectPeerRid')), false);
  assert.equal(calls.some((call) => call === 'spot:disconnectPeer:tcp://remote-pub'), false);
  assert.equal(calls.filter((call) => call === 'spot:disconnectPeer:tcp://manual-spot').length, 0);
  assert.equal(calls.filter((call) => call === 'spot:disconnectPeer:tcp://manual-pub').length, 0);
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
        }
      };
    },
    createSpotAdapter() {
      return {
        createSpotNode() {
          return {
            nativeInstance: {},
            routingId: nodeRid,
            setRoutingId() {},
            setPublisherRoutingId() {},
            setSubscriberRoutingId() {},
            setRouterBind() {},
            setPubBind() {},
            attachDiscovery() {},
            connectPeer(endpoint) { calls.push(`spot:connectPeer:${endpoint}`); },
            connectPeerRid(peerRid, endpoint) { calls.push(`spot:connectPeerRid:${peerRid.toHex()}:${endpoint}`); },
            disconnectPeerRid(peerRid) { calls.push(`spot:disconnectPeerRid:${peerRid.toHex()}`); },
            disconnectPeer(endpoint) { calls.push(`spot:disconnectPeer:${endpoint}`); },
            createRouteBridge() {
              return fakeSpotRouteBridge();
            },
            createSpot() {
              return {
                nativeInstance: {},
                onSendReady() {},
                publish() { return true; },
                async dispose() {}
              };
            },
            getOrCreateSpot() { throw new Error('not used'); },
            status() { return {}; },
            peers() { return []; },
            subjects() { return []; },
            entrySpot() {
              return {
                nativeInstance: {},
                setRoutingId() {},
                setDispatchHandler() {},
                recvActorJoin() {},
                recvRoute() { return false; },
                recv() { return null; },
                async dispose() {}
              };
            },
            createActor() { throw new Error('not used'); },
            actorLookup() { return undefined; },
            joinActor() { throw new Error('not used'); },
            joinActorEntrySpot() { throw new Error('not used'); },
            async destroyActor() {},
            sendActorBoundSession() { return true; },
            async closeActorBoundSession() {},
            async dispose() {
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

function fakeSpotRouteBridge() {
  return {
    attachRouterChannel() {},
    send() { return { message() { return this; }, submit() { return true; } }; },
    request() { return { message() { return this; }, timeout() { return this; }, submit() { return true; } }; },
    handleRouterReceived() { return false; },
    async dispose() {}
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

function routePeer(ownerId, nodeRid, endpoint) {
  return {
    autoConnectType: framework.ZLinkLocationAutoConnectType.RouteMesh,
    meshName: 'mesh',
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
