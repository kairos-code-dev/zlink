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
  const module = nestjs.ZLinkModule.forRoot({
    spotNodes: ['game'],
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
});

test('ZLinkModule.forRoot validates actor factory without spot node at registration time', () => {
  class ActorFactory {}

  assert.throws(
    () => nestjs.ZLinkModule.forRoot({ actorFactories: { player: ActorFactory } }),
    framework.ZLinkConfigurationException
  );
});

test('framework runtime host start and stop are idempotent and ordered', async () => {
  const lifecycle = [];
  let contextClosed = 0;
  let contextCreated = 0;
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration(),
    lifecycleSink: lifecycle,
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
