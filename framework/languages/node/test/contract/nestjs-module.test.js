const assert = require('node:assert/strict');
const test = require('node:test');

const framework = require('../../packages/framework/dist');
const nestjs = require('../../packages/nestjs/dist');

test('ZLinkModule.forRoot registers always-available providers for empty options', () => {
  const module = nestjs.ZLinkModule.forRoot();
  const tokens = providerTokens(module);

  assert.equal(tokens.has(nestjs.ZLINK_FRAMEWORK_RUNTIME), true);
  assert.equal(tokens.has(nestjs.ZLINK_CHANNEL_CLIENT), true);
  assert.equal(tokens.has(nestjs.ZLINK_ROUTE_CLIENT), true);
  assert.equal(tokens.has(nestjs.ZLINK_FANOUT_CLIENT), true);
  assert.equal(tokens.has(nestjs.ZLINK_BOUND_SESSION_FACTORY), true);
  assert.equal(tokens.has(nestjs.ZLINK_MESSAGE_METADATA_POLICY), true);
  assert.equal(tokens.has(nestjs.ZLINK_SPOT_MANAGER), false);
  assert.equal(tokens.has(nestjs.ZLINK_ACTOR_MANAGER), false);
});

test('ZLinkModule.forRoot exposes capability providers only when registration enables them', () => {
  class ActorFactory {}
  class StageSpot {
    constructor(context) {
      this.context = context;
    }
  }
  const module = nestjs.ZLinkModule.forRoot({
    spotNodes: ['game'],
    spotFactories: [StageSpot],
    actorFactories: { player: ActorFactory },
    spotPublisherClients: ['events']
  });
  const tokens = providerTokens(module);

  assert.equal(tokens.has(nestjs.ZLINK_SPOT_MANAGER), true);
  assert.equal(tokens.has(nestjs.ZLINK_SPOT_OUTBOUND), true);
  assert.equal(tokens.has(nestjs.ZLINK_SPOT_PUBLISHER_CLIENT), true);
  assert.equal(tokens.has(nestjs.ZLINK_ACTOR_MANAGER), true);
  assert.equal(
    module.providers.find((provider) => provider.provide === nestjs.ZLINK_ACTOR_MANAGER).useValue
      instanceof framework.DefaultZLinkActorManager,
    true
  );
  assert.equal(
    module.providers.find((provider) => provider.provide === nestjs.ZLINK_SPOT_MANAGER).useValue
      instanceof framework.DefaultZLinkSpotManager,
    true
  );
  assert.equal(
    module.providers.find((provider) => provider.provide === nestjs.ZLINK_ROUTE_CLIENT).useValue
      instanceof framework.DefaultZLinkRouteClient,
    true
  );
  assert.deepEqual(
    module.providers.find((provider) => provider.provide === nestjs.ZLINK_BOUND_SESSION_FACTORY).inject,
    [nestjs.ZLINK_FRAMEWORK_RUNTIME]
  );
  assert.equal(
    module.providers.find((provider) => provider.provide === nestjs.ZLINK_SPOT_OUTBOUND).useValue
      instanceof framework.DefaultZLinkSpotOutbound,
    true
  );
  assert.equal(
    module.providers.find((provider) => provider.provide === nestjs.ZLINK_SPOT_PUBLISHER_CLIENT).useValue
      instanceof framework.DefaultZLinkSpotPublisherClient,
    true
  );
});

test('ZLinkModule.forRoot public DI clients expose callable framework contracts', async () => {
  const module = nestjs.ZLinkModule.forRoot({
    routeChannels: ['mesh'],
    spotPublisherClients: ['spot-events']
  });
  const container = await resolveModuleProviders(module, [
    nestjs.ZLINK_FRAMEWORK_RUNTIME,
    nestjs.ZLINK_ROUTE_CLIENT,
    nestjs.ZLINK_BOUND_SESSION_FACTORY,
    nestjs.ZLINK_SPOT_PUBLISHER_CLIENT
  ]);

  const runtime = container.get(nestjs.ZLINK_FRAMEWORK_RUNTIME);
  const routeClient = container.get(nestjs.ZLINK_ROUTE_CLIENT);
  const boundSessionFactory = container.get(nestjs.ZLINK_BOUND_SESSION_FACTORY);
  const spotPublisher = container.get(nestjs.ZLINK_SPOT_PUBLISHER_CLIENT);

  assert.equal(typeof routeClient.send, 'function');
  assert.equal(typeof routeClient.request, 'function');
  assert.equal(typeof boundSessionFactory.create, 'function');
  assert.equal(typeof spotPublisher.publishSpot, 'function');
  assert.equal(boundSessionFactory, runtime.boundSessionFactory);

  await assert.rejects(
    () => routeClient.send('missing', 'node-a', { ok: true }).packetName('Ping').submit(),
    framework.ZLinkConfigurationException
  );
  await assert.rejects(
    () => routeClient.send('mesh', 'node-a', { ok: true }).packetName('Ping').submit(),
    /Route channel runtime is not started/
  );
  await assert.rejects(
    () => spotPublisher.publishSpot('missing', 'topic', { ok: true }).packetName('Event').submit(),
    framework.ZLinkConfigurationException
  );
  await assert.rejects(
    () => spotPublisher.publishSpot('spot-events', 'topic', { ok: true }).packetName('Event').submit(),
    /SPOT publisher runtime is not started/
  );
  await assert.rejects(
    () => boundSessionFactory.create('actor-1').send({ ok: true }).packetName('Push').submit(),
    (error) => error instanceof framework.ZLinkFrameworkException
      && error.kind === framework.ZLinkFrameworkErrorKind.ActorSessionNotBound
  );
});

test('ZLinkModule.forRoot passes registered spot factories to the spot manager', async () => {
  class StageSpot {
    constructor(context) {
      this.context = context;
    }
  }
  const module = nestjs.ZLinkModule.forRoot({
    spotNodes: ['game'],
    spotFactories: [StageSpot]
  });
  const spotManager = module.providers.find((provider) => provider.provide === nestjs.ZLINK_SPOT_MANAGER).useValue;

  const created = await spotManager.create(StageSpot);

  assert.equal(created.created, true);
});

test('ZLinkModule.forRoot validates actor factory without spot node at registration time', () => {
  class ActorFactory {}

  assert.throws(
    () => nestjs.ZLinkModule.forRoot({ actorFactories: { player: ActorFactory } }),
    framework.ZLinkConfigurationException
  );
});

test('ZLinkModule.forRoot validates channel capability endpoints and peer acquisition', () => {
  assert.throws(
    () => nestjs.ZLinkModule.forRoot({ channels: { api: { server: {} } } }),
    /server must define a bind endpoint/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot({ channels: { events: { publisher: {} } } }),
    /publisher must define a bind endpoint/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot({ channels: { api: { client: {} } } }),
    /client requires discovery or manual connections/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot({ channels: { events: { subscriber: {} } } }),
    /subscriber requires discovery or manual connections/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot({ channels: { api: { client: { manualConnections: [''] } } } }),
    /manual connection endpoint must not be empty/
  );

  assert.doesNotThrow(() => nestjs.ZLinkModule.forRoot({
    channels: {
      api: { client: {} },
      events: { subscriber: {} }
    },
    discovery: { registries: ['tcp://127.0.0.1:5551'] }
  }));
  assert.doesNotThrow(() => nestjs.ZLinkModule.forRoot({
    channels: {
      api: { client: { manualConnections: ['tcp://127.0.0.1:7001'] } },
      events: { subscriber: { manualConnections: ['tcp://127.0.0.1:7002'] } }
    }
  }));
});

test('ZLinkModule.forRootAsync exposes capability providers after async registration resolves', async () => {
  class AsyncSpot {
    constructor(context) {
      this.context = context;
    }
  }
  class ActorFactory {
    async create(actorId, context) {
      return { actorId, context };
    }
  }
  const module = nestjs.ZLinkModule.forRootAsync({
    async useFactory() {
      return {
        spotNodes: ['game'],
        spotFactories: [AsyncSpot],
        actorFactories: { player: ActorFactory },
        spotPublisherClients: ['game-events']
      };
    }
  });
  const container = await resolveModuleProviders(module, [
    nestjs.ZLINK_SPOT_MANAGER,
    nestjs.ZLINK_ACTOR_MANAGER,
    nestjs.ZLINK_SPOT_PUBLISHER_CLIENT
  ]);
  const actorManager = container.get(nestjs.ZLINK_ACTOR_MANAGER);

  assert.equal(container.get(nestjs.ZLINK_SPOT_MANAGER) instanceof framework.DefaultZLinkSpotManager, true);
  assert.equal(actorManager instanceof framework.DefaultZLinkActorManager, true);
  assert.equal((await container.get(nestjs.ZLINK_SPOT_MANAGER).create(AsyncSpot)).created, true);
  assert.equal((await actorManager.getOrCreate('p1', 'player')).actorId, 'p1');
  assert.equal(container.has(nestjs.ZLINK_SPOT_PUBLISHER_CLIENT), true);
});

test('ZLinkModule.forRootAsync rejects capability provider resolution when capability is absent', async () => {
  const module = nestjs.ZLinkModule.forRootAsync({
    async useFactory() {
      return {};
    }
  });

  await assert.rejects(
    () => resolveModuleProviders(module, [nestjs.ZLINK_SPOT_MANAGER]),
    framework.ZLinkConfigurationException
  );
});

test('framework runtime host start and stop are idempotent and ordered', async () => {
  const lifecycle = [];
  let contextClosed = 0;
  let contextCreated = 0;
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration(),
    lifecycleSink: lifecycle
  }, {
    backendAdapterFactory: {
      createChannelAdapter() {
        return {
          createContext() {
            contextCreated += 1;
            return {
              nativeInstance: {},
              shutdown() {},
              async dispose() {
                contextClosed += 1;
              }
            };
          }
        };
      }
    }
  });

  await runtime.onApplicationBootstrap();
  await runtime.onApplicationBootstrap();
  assert.equal(runtime.isStarted, true);
  assert.equal(contextCreated, 1);

  await runtime.onApplicationShutdown();
  await runtime.onApplicationShutdown();
  assert.equal(runtime.isStarted, false);
  assert.equal(contextClosed, 1);
  assert.deepEqual(lifecycle, ['framework:start', 'framework:started', 'framework:stop', 'framework:stopped']);
});

function providerTokens(module) {
  return new Set(module.providers.map((provider) => provider.provide));
}

async function resolveModuleProviders(module, requestedTokens) {
  const values = new Map();
  const providers = new Map(module.providers.map((provider) => [provider.provide, provider]));

  for (const token of requestedTokens) {
    await resolveToken(token);
  }

  return values;

  async function resolveToken(token) {
    if (values.has(token)) {
      return values.get(token);
    }
    const provider = providers.get(token);
    if (provider === undefined) {
      throw new Error(`Could not find provider: ${String(token)}`);
    }
    if ('useValue' in provider && provider.useValue !== undefined) {
      values.set(provider.provide, provider.useValue);
      return provider.useValue;
    }

    if ('useFactory' in provider && provider.useFactory !== undefined) {
      const dependencies = [];
      for (const dependency of provider.inject ?? []) {
        dependencies.push(await resolveToken(dependency));
      }
      const value = await provider.useFactory(...dependencies);
      values.set(provider.provide, value);
      return value;
    }

    throw new Error(`Provider ${String(token)} has no supported value or factory.`);
  }
}
