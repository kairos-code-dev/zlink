const assert = require('node:assert/strict');
const test = require('node:test');
const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');

const framework = require('../../packages/framework/dist/internal');
const nestjs = require('../../packages/nestjs/dist');
const {
  providerTokens,
  reserveTcpEndpoint
} = require('./helpers/nestjs-test-utils');

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

  const status = await query.status();
  const topology = await query.topology({ channelName: 'api' });
  const summary = await query.serviceSummary({ channelName: 'api' });
  const peers = await query.memberPeers('api');
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

test('registry query client owns backend context and exposes topology only', async () => {
  const calls = [];
  const backend = fakeRegistryBackend(calls);
  const client = new framework.DefaultZLinkRegistryQueryClient({
    registration: { endpoint: 'tcp://registry:5551' }
  }, {
    backendAdapterFactory: backend.factory
  });

  const topology = await client.topology({ channelName: 'remote' });
  await client.dispose();

  assert.equal(topology[0].channelName, 'remote');
  assert.equal(typeof client.serviceSummary, 'undefined');
  assert.deepEqual(calls, [
    ['context:create'],
    ['query:create'],
    ['query:connect', 'tcp://registry:5551'],
    ['query:dispose'],
    ['context:dispose']
  ]);
});

test('registry spot remote address resolver resolves spot owner route through discovery', async () => {
  const calls = [];
  const registration = framework.createFrameworkRegistration({
    discovery: { registries: ['tcp://127.0.0.1:5551'] },
    routeChannels: ['play'],
    registrySpotRemoteAddresses: { namespace: 'bingo' }
  });
  const resolver = new framework.ZLinkRegistrySpotRemoteAddressResolver({ registration }, {
    backendAdapterFactory: fakeSpotRouteBackend(calls).factory
  });

  const route = await resolver.resolve('spot-a');
  await resolver.dispose();

  assert.deepEqual(route, {
    routerChannelId: 'play',
    targetNodeRid: 'node-a',
    spotRid: 'spot-a',
    spotKind: framework.ZLinkSpotKind.User
  });
  assert.deepEqual(calls, [
    ['context:create'],
    ['discovery:create', framework.ZLinkAutoConnectType.ClientServer, 'bingo'],
    ['discovery:connectRegistry', 'tcp://127.0.0.1:5551'],
    ['discovery:resolveSpot', 'spot-a'],
    ['discovery:dispose'],
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

test('registry modules support NestJS async imports inject and lifecycle', async () => {
  const REGISTRY_CONFIG = Symbol('registry-config');
  const pubEndpoint = await reserveTcpEndpoint();
  const routerEndpoint = await reserveTcpEndpoint();
  class ConfigModule {}
  Module({
    providers: [{
      provide: REGISTRY_CONFIG,
      useValue: {
        pubEndpoint,
        routerEndpoint
      }
    }],
    exports: [REGISTRY_CONFIG]
  })(ConfigModule);

  class RegistryModule {}
  Module({
    imports: [
      nestjs.ZLinkRegistryModule.forRootFactory({
        imports: [ConfigModule],
        inject: [REGISTRY_CONFIG],
        useFactory: (config) => config
      })
    ]
  })(RegistryModule);

  const app = await NestFactory.createApplicationContext(RegistryModule, { logger: false, abortOnError: false });
  const runtime = app.get(nestjs.ZLINK_REGISTRY_RUNTIME, { strict: false });
  const query = app.get(nestjs.ZLINK_REGISTRY_QUERY, { strict: false });

  assert.equal(runtime instanceof framework.ZLinkRegistryRuntime, true);
  assert.equal(query instanceof framework.DefaultZLinkRegistryQuery, true);
  assert.equal(runtime.isStarted, true);

  await app.close();
  assert.equal(runtime.isStarted, false);
});

test('registry query client module supports NestJS async imports and inject', async () => {
  const QUERY_CONFIG = Symbol('query-config');
  class ConfigModule {}
  Module({
    providers: [{ provide: QUERY_CONFIG, useValue: { endpoint: 'tcp://registry:5551' } }],
    exports: [QUERY_CONFIG]
  })(ConfigModule);

  class QueryClientModule {}
  Module({
    imports: [
      nestjs.ZLinkRegistryQueryClientModule.forRootFactory({
        imports: [ConfigModule],
        inject: [QUERY_CONFIG],
        useFactory: (config) => config
      })
    ]
  })(QueryClientModule);

  const app = await NestFactory.createApplicationContext(QueryClientModule, { logger: false, abortOnError: false });
  const client = app.get(nestjs.ZLINK_REGISTRY_QUERY_CLIENT, { strict: false });

  assert.equal(client instanceof framework.DefaultZLinkRegistryQueryClient, true);

  await app.close();
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

function fakeSpotRouteBackend(calls) {
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
          },
          createDiscovery(_context, autoConnectType, channelName) {
            calls.push(['discovery:create', autoConnectType, channelName]);
            return {
              nativeInstance: {},
              spotOwnerSyncEnabled: false,
              actorRouteSyncEnabled: false,
              connectRegistry(endpoint) {
                calls.push(['discovery:connectRegistry', endpoint]);
              },
              memberPeers() {
                return [];
              },
              resolveSpot(spotRid) {
                calls.push(['discovery:resolveSpot', spotRid]);
                return {
                  spotRid,
                  ownerNodeRid: 'node-a',
                  spotKind: framework.ZLinkSpotKind.User
                };
              },
              resolveActor() {
                throw new Error('not implemented');
              },
              bindRoute() {},
              unbindRoute() {},
              resolveRoute() {
                throw new Error('not implemented');
              },
              async dispose() {
                calls.push(['discovery:dispose']);
              }
            };
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
