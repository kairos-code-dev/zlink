const assert = require('node:assert/strict');
const test = require('node:test');

const framework = require('../../packages/framework/dist');
const nestjs = require('../../packages/nestjs/dist');

test('registry runtime applies dotnet defaults and supports lazy in-process query startup', async () => {
  const calls = [];
  const backend = fakeRegistryBackend(calls);
  const runtime = new framework.ZLinkRegistryRuntime({
    registration: {
      pubEndpoint: 'tcp://0.0.0.0:5550',
      routerEndpoint: 'tcp://0.0.0.0:5551',
      registryId: 7,
      peers: ['tcp://registry-2:5550']
    }
  }, {
    backendAdapterFactory: backend.factory
  });
  const query = new framework.DefaultZLinkRegistryQuery(runtime);

  const status = await query.statusAsync();
  const topology = await query.topologyAsync({ channelName: 'api' });
  const summary = await query.serviceSummaryAsync({ channelName: 'api' });
  const peers = await query.memberPeersAsync('api');
  await runtime.stop();

  assert.equal(status.registryId, 7);
  assert.equal(topology[0].channelName, 'api');
  assert.equal(summary[0].readyCount, 1);
  assert.equal(peers[0].endpoint, 'tcp://peer:7101');
  assert.deepEqual(calls, [
    ['context:create'],
    ['registry:create'],
    ['registry:setId', 7],
    ['registry:setHeartbeat', 5000, 15000],
    ['registry:setBroadcastInterval', 30000],
    ['registry:addPeer', 'tcp://registry-2:5550'],
    ['registry:bind', 'tcp://0.0.0.0:5550', 'tcp://0.0.0.0:5551'],
    ['registry:dispose'],
    ['context:dispose']
  ]);
});

test('registry query client owns backend context and exposes topologyAsync only', async () => {
  const calls = [];
  const backend = fakeRegistryBackend(calls);
  const client = new framework.DefaultZLinkRegistryQueryClient({
    registration: { endpoint: 'tcp://registry:5551' }
  }, {
    backendAdapterFactory: backend.factory
  });

  const topology = await client.topologyAsync({ channelName: 'remote' });
  await client.dispose();

  assert.equal(topology[0].channelName, 'remote');
  assert.equal(typeof client.serviceSummaryAsync, 'undefined');
  assert.deepEqual(calls, [
    ['context:create'],
    ['query:create'],
    ['query:connect', 'tcp://registry:5551'],
    ['query:dispose'],
    ['context:dispose']
  ]);
});

test('registry modules expose runtime query and remote query client providers', () => {
  const registryModule = nestjs.ZLinkRegistryModule.forRoot({
    pubEndpoint: 'tcp://0.0.0.0:5550',
    routerEndpoint: 'tcp://0.0.0.0:5551'
  });
  const queryClientModule = nestjs.ZLinkRegistryQueryClientModule.forRoot({
    endpoint: 'tcp://registry:5551'
  });

  assert.equal(providerTokens(registryModule).has(nestjs.ZLINK_REGISTRY_RUNTIME), true);
  assert.equal(providerTokens(registryModule).has(nestjs.ZLINK_REGISTRY_QUERY), true);
  assert.equal(providerTokens(queryClientModule).has(nestjs.ZLINK_REGISTRY_QUERY_CLIENT), true);

  const runtime = registryModule.providers.find((provider) => provider.provide === nestjs.ZLINK_REGISTRY_RUNTIME).useValue;
  const query = registryModule.providers.find((provider) => provider.provide === nestjs.ZLINK_REGISTRY_QUERY).useValue;
  const client = queryClientModule.providers.find((provider) => provider.provide === nestjs.ZLINK_REGISTRY_QUERY_CLIENT);

  assert.equal(runtime instanceof framework.ZLinkRegistryRuntime, true);
  assert.equal(query instanceof framework.DefaultZLinkRegistryQuery, true);
  assert.equal(typeof client.useFactory, 'function');
});

test('codec registry builder tracks dotnet named codecs and custom serializers', () => {
  const serializer = {
    serialize() {},
    deserialize() {}
  };
  const builder = new framework.DefaultZLinkCodecRegistryBuilder();

  builder.addJson().addMessagePack().addProtobuf().addSerializer('application/x-test', serializer);

  assert.deepEqual(builder.registeredCodecs, [
    'json',
    'messagepack',
    'protobuf',
    'application/x-test'
  ]);
  assert.equal(builder.registeredSerializers.get('application/x-test'), serializer);
});

function providerTokens(module) {
  return new Set(module.providers.map((provider) => provider.provide));
}

function fakeRegistryBackend(calls) {
  return {
    factory: {
      createChannelAdapter() {
        return {
          createContext() {
            calls.push(['context:create']);
            return {
              nativeInstance: {},
              shutdown() {},
              async dispose() {
                calls.push(['context:dispose']);
              }
            };
          }
        };
      },
      createRegistryAdapter() {
        return {
          createRegistry() {
            calls.push(['registry:create']);
            return fakeRegistry(calls);
          },
          createRegistryQueryClient() {
            calls.push(['query:create']);
            return fakeRegistryQueryClient(calls);
          }
        };
      }
    }
  };
}

function fakeRegistry(calls) {
  return {
    nativeInstance: {},
    setId(registryId) {
      calls.push(['registry:setId', registryId]);
    },
    setHeartbeat(intervalMs, timeoutMs) {
      calls.push(['registry:setHeartbeat', intervalMs, timeoutMs]);
    },
    setBroadcastInterval(intervalMs) {
      calls.push(['registry:setBroadcastInterval', intervalMs]);
    },
    addPeer(endpoint) {
      calls.push(['registry:addPeer', endpoint]);
    },
    bind(pubEndpoint, routerEndpoint) {
      calls.push(['registry:bind', pubEndpoint, routerEndpoint]);
    },
    status() {
      return {
        registryId: 7,
        bindEndpoint: 'tcp://0.0.0.0:5551',
        state: framework.ZLinkRegistryState.Active,
        topologyEntryCount: 1,
        peerRegistryCount: 1,
        connectedPeerRegistryCount: 1,
        listSeq: 1n,
        lastError: 0,
        lastChangedMs: 10n
      };
    },
    serviceSummary() {
      return [{
        autoConnectType: framework.ZLinkAutoConnectType.ClientServer,
        serviceRole: framework.ZLinkServiceRole.Router,
        channelName: 'api',
        totalCount: 1,
        connectingCount: 0,
        readyCount: 1,
        errorCount: 0,
        stoppedCount: 0,
        lastReportedMs: 20n
      }];
    },
    topology(filter = {}) {
      return [topologyEntry(filter.channelName ?? 'api')];
    },
    memberPeers(channelName) {
      return [{
        autoConnectType: framework.ZLinkAutoConnectType.ClientServer,
        serviceRole: framework.ZLinkServiceRole.Router,
        channelName,
        endpoint: 'tcp://peer:7101',
        routingId: 'peer-a',
        value: 0n,
        weight: 1
      }];
    },
    async dispose() {
      calls.push(['registry:dispose']);
    }
  };
}

function fakeRegistryQueryClient(calls) {
  return {
    nativeInstance: {},
    connect(endpoint) {
      calls.push(['query:connect', endpoint]);
    },
    topology(filter = {}) {
      return [topologyEntry(filter.channelName ?? 'remote')];
    },
    async dispose() {
      calls.push(['query:dispose']);
    }
  };
}

function topologyEntry(channelName) {
  return {
    autoConnectType: framework.ZLinkAutoConnectType.ClientServer,
    routingId: 'peer-a',
    serviceKind: framework.ZLinkServiceKind.Socket,
    serviceRole: framework.ZLinkServiceRole.Router,
    channelName,
    endpoint: 'tcp://peer:7101',
    source: framework.ZLinkTopologySource.Manual,
    state: framework.ZLinkTopologyState.Ready,
    desiredCount: 1,
    readyCount: 1,
    errorCode: 0,
    lastReportedMs: 20n,
    spotKind: framework.ZLinkSpotKind.User
  };
}
