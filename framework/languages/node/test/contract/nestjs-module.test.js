const assert = require('node:assert/strict');
const fs = require('node:fs');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');
const { Inject, Injectable, Module, Scope } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');

const framework = require('../../packages/framework/dist/internal');
const nestjs = require('../../packages/nestjs/dist');
const {
  providerTokens,
  reserveTcpEndpoint,
  resolveModuleProviders
} = require('./helpers/nestjs-test-utils');

class NoopRequestHandler {
  async handle() {}
}

class NoopPublishHandler {
  async handle() {}
}

async function resolveFrameworkRegistration(module) {
  const provider = module.providers.find((candidate) => candidate.provide === nestjs.ZLINK_FRAMEWORK_REGISTRATION);
  if (provider?.useValue !== undefined) {
    return provider.useValue;
  }
  const container = await resolveModuleProviders(module, [nestjs.ZLINK_FRAMEWORK_REGISTRATION]);
  return container.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION);
}

function fakeSpotRouteBridge(calls, reply) {
  return {
    attachRouterChannel(channelName) {
      calls.push(`bridge:attachRouter:${channelName}`);
    },
    send() {
      return { message() { return this; }, submit() { return true; } };
    },
    request(channelName, targetNodeRid, spotId) {
      calls.push(`bridge:request:${channelName}:${targetNodeRid}:${spotId}`);
      return {
        message(part) {
          if (part?.value !== undefined) {
            calls.push(`bridge:message:${part.value}`);
          }
          return this;
        },
        timeout(timeoutMs) {
          calls.push(`bridge:timeout:${timeoutMs}`);
          return this;
        },
        submit(callback) {
          if (reply !== undefined) {
            callback(0, [reply]);
          }
          return true;
        }
      };
    },
    handleRouterReceived(channelName) {
      if (!calls.includes(`bridge:handleRouter:${channelName}`)) {
        calls.push(`bridge:handleRouter:${channelName}`);
      }
      return false;
    },
    async dispose() {
      calls.push('bridge:dispose');
    }
  };
}

function exposeLegacyTestSpotAsMeshNode(spotNode) {
  const connectPeer = spotNode.connectPeer?.bind(spotNode);
  const connectPeerRid = spotNode.connectPeerRid?.bind(spotNode);
  const dispose = spotNode.dispose?.bind(spotNode);
  Object.assign(spotNode, {
    setBind(endpoint) {
      spotNode.setRouterBind(endpoint);
    },
    addChannelName() {},
    setChannelWeight() {},
    setPlacementWeight() {},
    configureObjectPlacement() {},
    selectObjectPlacement() { return undefined; },
    start() {},
    shutdown() {
      return 0;
    },
    close() {
      void dispose?.();
    },
    connectPeer(options) {
      if (options.expectedRid !== undefined) {
        connectPeerRid?.(options.expectedRid, options.endpoint);
      } else {
        connectPeer?.(options.endpoint);
      }
      return 1n;
    },
    createReadyBatch() {
      return { reset() {}, takeClaim() {}, close() {} };
    },
    createReceiveBatch() {
      return { reset() {}, close() {} };
    },
    setReadyHandler() {},
    drainReady() {
      return { ok: false, hasResidue: false, records: [] };
    }
  });
  return spotNode;
}

let ipcEndpointSequence = 0;
function uniqueIpcEndpoint(label) {
  ipcEndpointSequence += 1;
  return `ipc://${path.join(os.tmpdir(), `zlink-node-nest-${process.pid}-${ipcEndpointSequence}-${label}.sock`)}`;
}

function createNoopMonitoringAdapter() {
  return {
    openSocketMonitor() {
      return {
        nativeInstance: {},
        onEvent() {},
        recv() { return {}; },
        status() { return {}; },
        async dispose() {}
      };
    }
  };
}

test('ZLinkModule.forRoot registers always-available providers for empty options', () => {
  const module = nestjs.ZLinkModule.forRoot();
  const tokens = providerTokens(module);

  assert.equal(tokens.has(nestjs.ZLINK_FRAMEWORK_RUNTIME), true);
  assert.equal(tokens.has(nestjs.ZLINK_CHANNEL_CLIENT), true);
  assert.equal(tokens.has(nestjs.ZLINK_ROUTE_CLIENT), true);
  assert.equal(tokens.has(nestjs.ZLINK_FANOUT_CLIENT), true);
  assert.equal(tokens.has(nestjs.ZLINK_ACTOR_CLIENT), false);
  assert.equal(tokens.has(nestjs.ZLINK_BOUND_SESSION_FACTORY), true);
  assert.equal(Object.hasOwn(nestjs, 'ZLINK_RUNTIME_EVENT_PUBLISHER'), false);
  assert.equal(tokens.has(nestjs.ZLINK_ROUTE_MESH_RUNTIME), false);
  assert.equal(tokens.has(nestjs.ZLINK_MESSAGE_METADATA_POLICY), true);
  assert.equal(tokens.has(nestjs.ZLINK_SPOT_MANAGER), false);
  assert.equal(tokens.has(nestjs.ZLINK_ACTOR_MANAGER), false);
});

test('ZLinkHttpClientModule registers named server clients through Nest DI', async () => {
  const server = http.createServer((_req, res) => {
    res.setHeader('content-type', 'application/json');
    res.end(JSON.stringify({ source: 'profiles' }));
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const baseUrl = `http://127.0.0.1:${server.address().port}`;
  const profilesToken = nestjs.zlinkHttpClientToken('profiles');

  class AppModule {}
  Module({
    imports: [nestjs.ZLinkHttpClientModule.forRoot({
      imports: [nestjs.ZLinkModule.forRoot()],
      clients: [{
        name: 'profiles',
        baseUrl,
        configure: (builder) => builder.timeout(500)
      }]
    })]
  })(AppModule);

  const app = await NestFactory.createApplicationContext(AppModule, {
    abortOnError: false,
    logger: false
  });
  try {
    const client = app.get(profilesToken);
    const response = await client.get('/profile').async();
    assert.equal(response.body.source, 'profiles');
    assert.equal(typeof client.get('/profile').yield, 'function');
  } finally {
    await app.close();
    await new Promise((resolve) => server.close(resolve));
  }

  assert.throws(
    () => nestjs.ZLinkHttpClientModule.forRoot({
      imports: [],
      clients: [
        { name: 'profiles', baseUrl },
        { name: 'profiles', baseUrl }
      ]
    }),
    /already registered/
  );
});

test('ZLinkModule lifecycle registers decorated runtime event handlers with the runtime publisher', async () => {
  const events = [];
  class TimerFailureMonitor {
    async handle(event) {
      events.push(event);
    }
  }
  nestjs.zlinkRuntimeEventHandler()(TimerFailureMonitor);

  class AppModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot()],
    providers: [TimerFailureMonitor]
  })(AppModule);

  const app = await NestFactory.createApplicationContext(AppModule, { logger: false });
  try {
    const publisher = app.get(nestjs.ZLINK_FRAMEWORK_RUNTIME).eventPublisher;
    await publisher.publish({
      sourceName: 'stage-node',
      timestamp: new Date(),
      event: framework.ZLinkSpotEventKind.TimerHandlerFailed,
      timerDiagnostic: {
        spotId: 'stage-node',
        isEntrySpot: true,
        timerName: 'idle',
        handlerType: 'IdleTimerHandler',
        deliveryIndex: 1n,
        scheduledIndex: 1n,
        exceptionType: 'Error',
        exceptionMessage: 'timer failed'
      }
    });
  } finally {
    await app.close();
  }

  const timerEvent = events.find((event) => event.event === framework.ZLinkSpotEventKind.TimerHandlerFailed);
  assert.notEqual(timerEvent, undefined);
  assert.equal(timerEvent.timerDiagnostic.timerName, 'idle');
});

test('ZLinkModule.forRoot exposes capability providers only when registration enables them', async () => {
  class ActorFactory {}
  class StageSpot {
    constructor(context) {
      this.context = context;
    }
  }
  const module = nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
    .addLocationStore(new framework.ZLinkInMemoryProviderLocationStore())
    .options({ spotPublisherClients: ['events'] })
    .addRouteMesh('game')
      .listen('tcp://127.0.0.1:0')
      .objects().server()
      .addSpotFactory(StageSpot.name, StageSpot, (factory) => factory.disableRelocation())
      .addActorFactory('player', ActorFactory, (factory) => factory.disableRelocation())
    .build());
  const tokens = providerTokens(module);

  assert.equal(tokens.has(nestjs.ZLINK_SPOT_MANAGER), true);
  assert.equal(tokens.has(nestjs.ZLINK_SPOT_OUTBOUND), true);
  assert.equal(tokens.has(nestjs.ZLINK_SPOT_PUBLISHER_CLIENT), true);
  assert.equal(tokens.has(nestjs.ZLINK_ACTOR_MANAGER), true);
  assert.equal(tokens.has(nestjs.ZLINK_ROUTE_MESH_RUNTIME), true);
  const container = await resolveModuleProviders(module, [
    nestjs.ZLINK_FRAMEWORK_RUNTIME,
    nestjs.ZLINK_ROUTE_MESH_RUNTIME,
    nestjs.ZLINK_ROUTE_CLIENT,
    nestjs.ZLINK_SPOT_OUTBOUND,
    nestjs.ZLINK_SPOT_PUBLISHER_CLIENT,
    nestjs.ZLINK_BOUND_SESSION_FACTORY,
    nestjs.ZLINK_ACTOR_MANAGER,
    nestjs.ZLINK_SPOT_MANAGER
  ]);
  assert.equal(container.get(nestjs.ZLINK_ACTOR_MANAGER) instanceof framework.DefaultZLinkActorManager, true);
  assert.equal(typeof container.get(nestjs.ZLINK_SPOT_MANAGER).create, 'function');
  assert.equal(
    container.get(nestjs.ZLINK_ROUTE_MESH_RUNTIME),
    container.get(nestjs.ZLINK_FRAMEWORK_RUNTIME).routeMeshRuntime
  );
  assert.equal(container.get(nestjs.ZLINK_ROUTE_CLIENT) instanceof framework.DefaultZLinkRouteClient, true);
  assert.deepEqual(
    module.providers.find((provider) => provider.provide === nestjs.ZLINK_BOUND_SESSION_FACTORY).inject,
    [nestjs.ZLINK_FRAMEWORK_REGISTRATION, nestjs.ZLINK_FRAMEWORK_RUNTIME]
  );
  assert.equal(
    container.get(nestjs.ZLINK_SPOT_OUTBOUND) instanceof framework.DefaultZLinkSpotOutbound,
    true
  );
  assert.equal(container.get(nestjs.ZLINK_SPOT_PUBLISHER_CLIENT) instanceof framework.DefaultZLinkSpotPublisherClient, true);
});

test('ZLinkModule.forRoot creates Spot manager before runtime bootstrap', async () => {
  class ActorFactory {}
  class StageSpot {
    constructor(context) {
      this.context = context;
    }
  }
  class DispatchObserver {
    onMessageFlow() {}
  }
  const builder = nestjs.zlinkFramework();
  builder.addLocationStore(new framework.ZLinkInMemoryProviderLocationStore());
  builder.configureDispatch().setMessageFlowObserver(DispatchObserver);
  const module = nestjs.ZLinkModule.forRoot(builder
    .addRouteMesh('game')
      .listen('tcp://127.0.0.1:0')
      .objects().server()
      .addSpotFactory(StageSpot.name, StageSpot, (factory) => factory.disableRelocation())
      .addActorFactory('player', ActorFactory, (factory) => factory.disableRelocation())
    .build());
  const container = await resolveModuleProviders(module, [
    nestjs.ZLINK_FRAMEWORK_RUNTIME,
    nestjs.ZLINK_SPOT_MANAGER
  ]);

  const runtime = container.get(nestjs.ZLINK_FRAMEWORK_RUNTIME);
  const spotManager = container.get(nestjs.ZLINK_SPOT_MANAGER);

  assert.equal(runtime.isStarted, false);
  assert.equal(typeof spotManager.create, 'function');
});

test('ZLinkModule.forRoot public DI clients expose callable framework contracts', async () => {
  const builder = nestjs.zlinkFramework()
    .addLocationStore(new framework.ZLinkInMemoryProviderLocationStore())
    .options({ spotPublisherClients: ['spot-events'] });
  const mesh = builder.addRouteMesh('actors')
    .listen('tcp://127.0.0.1:0')
    .routingId('actor-node');
  mesh.channelName('mesh');
  mesh.channelName('actors');
  const module = nestjs.ZLinkModule.forRoot(builder.build());
  const container = await resolveModuleProviders(module, [
    nestjs.ZLINK_FRAMEWORK_RUNTIME,
    nestjs.ZLINK_ROUTE_CLIENT,
    nestjs.ZLINK_ACTOR_CLIENT,
    nestjs.ZLINK_BOUND_SESSION_FACTORY,
    nestjs.ZLINK_SPOT_PUBLISHER_CLIENT
  ]);

  const runtime = container.get(nestjs.ZLINK_FRAMEWORK_RUNTIME);
  const routeClient = container.get(nestjs.ZLINK_ROUTE_CLIENT);
  const actorClient = container.get(nestjs.ZLINK_ACTOR_CLIENT);
  const boundSessionFactory = container.get(nestjs.ZLINK_BOUND_SESSION_FACTORY);
  const spotPublisher = container.get(nestjs.ZLINK_SPOT_PUBLISHER_CLIENT);

  assert.equal(typeof routeClient.sendToNode, 'function');
  assert.equal(typeof routeClient.requestToNode, 'function');
  assert.equal(typeof actorClient.sendToActor, 'function');
  assert.equal(typeof actorClient.requestToActor, 'function');
  assert.equal(typeof boundSessionFactory.create, 'function');
  assert.equal(typeof spotPublisher.publish, 'function');
  assert.equal(boundSessionFactory, runtime.boundSessionFactory);

  class Ping { constructor(ok) { this.ok = ok; } }
  class Event { constructor(ok) { this.ok = ok; } }

  await assert.rejects(
    () => routeClient.sendToNode('missing', 'node-a', new Ping(true)).submit(),
    framework.ZLinkConfigurationException
  );
  await assert.rejects(
    () => routeClient.sendToNode('actors', 'node-a', new Ping(true)).submit(),
    /runtime is not started/i
  );
  await assert.rejects(
    () => spotPublisher.publish('missing', 'events', 'topic', new Event(true)).submit(),
    framework.ZLinkConfigurationException
  );
  await assert.rejects(
    () => spotPublisher.publish('actors', 'actors', 'topic', new Event(true)).submit(),
    /runtime is not started/i
  );
  await assert.rejects(
    () => boundSessionFactory.create('actor-1').send({ ok: true }).packetName('Push').submit()
  );
});

test('zlinkFramework builder maps channel and route mesh options', () => {
  class DispatchObserver {
    onMessageFlow() {}
  }
  const builder = nestjs.zlinkFramework();
  builder.configureDispatch().setMessageFlowObserver(DispatchObserver);
  builder.addRouteMesh('api')
      .listen('tcp://127.0.0.1:7101')
      .routingId('api-a')
      .channelName('api')
      .addHandlerGroup('api');
  builder.addRouteMesh('play').peerConnections().connect('tcp://127.0.0.1:7102');
  const mesh = builder.addRouteMesh('route')
    .listen('tcp://127.0.0.1:7201')
    .routingId('node-a');
  mesh.channelName('route').addHandlerGroup('route-api');
  mesh.peerConnections().connect('tcp://127.0.0.1:7202');
  const clientOnly = builder.addRouteMesh('client-only').channel('outbound');
  clientOnly.client();
  const bothRoles = builder.addRouteMesh('both-roles').channel('shared');
  bothRoles.client();
  bothRoles.server().setWeight(0);
  const options = builder.build();

  assert.deepEqual(options.spotNodes.api, {
    router: { bind: 'tcp://127.0.0.1:7101', routingId: 'api-a' },
    routingId: 'api-a',
    meshChannels: { api: { server: true, handlerGroups: ['api'] } }
  });
  assert.deepEqual(options.spotNodes.play, {
    router: { manualConnections: ['tcp://127.0.0.1:7102'] }
  });
  assert.deepEqual(options.spotNodes.route, {
    router: {
      bind: 'tcp://127.0.0.1:7201',
      routingId: 'node-a',
      manualConnections: ['tcp://127.0.0.1:7202']
    },
    routingId: 'node-a',
    meshChannels: {
      route: { server: true, handlerGroups: ['route-api'] }
    }
  });
  assert.deepEqual(options.spotNodes['client-only'].meshChannels, {
    outbound: { client: true }
  });
  assert.deepEqual(options.spotNodes['both-roles'].meshChannels, {
    shared: { client: true, server: true, weight: 0 }
  });
  assert.equal(framework.getDispatchObserverType(options.dispatch), DispatchObserver);
});

test('ZLinkModule.forRoot boots through the real NestJS DI container and lifecycle', async () => {
  const moduleDefinition = nestjs.ZLinkModule.forRoot();
  class ConsumerModule {}
  Module({
    imports: [moduleDefinition],
    providers: [{
      provide: 'consumer',
      inject: [nestjs.ZLINK_CHANNEL_CLIENT, nestjs.ZLINK_FRAMEWORK_RUNTIME],
      useFactory: (channelClient, runtime) => ({ channelClient, runtime })
    }]
  })(ConsumerModule);

  const app = await NestFactory.createApplicationContext(ConsumerModule, { logger: false });
  const runtime = app.get(nestjs.ZLINK_FRAMEWORK_RUNTIME);
  const channelClient = app.get(nestjs.ZLINK_CHANNEL_CLIENT);
  const consumer = app.get('consumer');

  assert.equal(runtime instanceof framework.ZLinkFrameworkRuntimeHost, true);
  assert.equal(runtime.isStarted, true);
  assert.equal(channelClient instanceof framework.DefaultZLinkChannelClient, true);
  assert.equal(consumer.channelClient, channelClient);
  assert.equal(consumer.runtime, runtime);

  await app.close();
  assert.equal(runtime.isStarted, false);
});

test('ZLinkModule.forRoot maps zlinkRequestHandler providers from NestJS DI', async () => {
  const apiEndpoint = await reserveTcpEndpoint();
  class ProfileHandler {
    async handle(request) {
      return { profileId: request.profileId, source: 'provider-group' };
    }
  }
  nestjs.zlinkRequestHandler('api', 'GetProfile')(ProfileHandler);

  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addRouteMesh('api')
        .listen(apiEndpoint)
        .routingId('api-node')
        .channelName('api')
        .addHandlerGroup('api')
      .build())],
    providers: [ProfileHandler]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const registration = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION);
  const handlers = registration.spotNodes.get('api').meshChannels.api.requestHandlers;

  assert.equal(handlers.length, 1);
  assert.equal(handlers[0].packetName, 'GetProfile');
  assert.deepEqual(
    await handlers[0].handler.handle(Buffer.from(JSON.stringify({ profileId: 'p1' })), {}),
    { profileId: 'p1', source: 'provider-group' }
  );

  await app.close();
});

test('request-scoped handler filters share the channel dispatch scope with the handler', async () => {
  let dispatchSequence = 0;

  class DispatchState {
    constructor() {
      this.id = ++dispatchSequence;
    }
  }
  Injectable({ scope: Scope.REQUEST })(DispatchState);

  class RequestScopeFilter {
    constructor(state) {
      this.state = state;
    }

    async invoke(_invocation, next) {
      this.state.filtered = true;
      return next();
    }
  }
  Inject(DispatchState)(RequestScopeFilter, undefined, 0);
  Injectable({ scope: Scope.REQUEST })(RequestScopeFilter);

  class ScopedProfileHandler {
    constructor(state) {
      this.state = state;
    }

    async handle(request) {
      return {
        profileId: request.profileId,
        dispatchId: this.state.id,
        filtered: this.state.filtered === true
      };
    }
  }
  Inject(DispatchState)(ScopedProfileHandler, undefined, 0);
  Injectable({ scope: Scope.REQUEST })(ScopedProfileHandler);
  nestjs.zlinkRequestHandler('api', 'GetProfile')(ScopedProfileHandler);

  const frameworkOptions = nestjs.zlinkFramework()
    .options({ filters: [RequestScopeFilter] });
  const apiChannel = frameworkOptions.addClientServerChannel('api');
  apiChannel.client();
  apiChannel.server().listen().addHandlerGroup('api');

  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(frameworkOptions.build())],
    providers: [DispatchState, RequestScopeFilter, ScopedProfileHandler]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  try {
    const client = app.get(nestjs.ZLINK_CHANNEL_CLIENT);
    class GetProfile {
      constructor(profileId) {
        this.profileId = profileId;
      }
    }
    const first = await client.requestToChannel('api', new GetProfile('p1'))
      .timeout(1000)
      .submit();
    const second = await client.requestToChannel('api', new GetProfile('p2'))
      .timeout(1000)
      .submit();

    assert.deepEqual(first, { profileId: 'p1', dispatchId: 1, filtered: true });
    assert.deepEqual(second, { profileId: 'p2', dispatchId: 2, filtered: true });
  } finally {
    await app.close();
  }
});

test('ZLinkModule.forRoot maps decorated custom NestJS provider objects', async () => {
  const apiEndpoint = await reserveTcpEndpoint();
  const PROFILE_HANDLER = Symbol('profile-handler');
  class ProfileHandler {
    async handle(request) {
      return { profileId: request.profileId, source: 'custom-token' };
    }
  }
  nestjs.zlinkRequestHandler('api', 'GetProfile')(ProfileHandler);

  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addRouteMesh('api')
        .listen(apiEndpoint)
        .routingId('api-node')
        .channelName('api')
        .addHandlerGroup('api')
      .build())],
    providers: [{ provide: PROFILE_HANDLER, useClass: ProfileHandler }]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const handlers = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION).spotNodes.get('api').meshChannels.api.requestHandlers;

  assert.equal(handlers.length, 1);
  assert.deepEqual(
    await handlers[0].handler.handle(Buffer.from(JSON.stringify({ profileId: 'p1' })), {}),
    { profileId: 'p1', source: 'custom-token' }
  );

  await app.close();
});

test('ZLinkModule.forRoot ignores undecorated providers for an outbound-capable mesh channel', async () => {
  const apiEndpoint = await reserveTcpEndpoint();
  const PROFILE_HANDLER = Symbol('profile-handler');

  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addRouteMesh('api')
        .listen(apiEndpoint)
        .routingId('api-node')
        .channelName('api')
        .addHandlerGroup('api')
      .build())],
    providers: [{
      provide: PROFILE_HANDLER,
      useValue: {
        async handle(request) {
          return { profileId: request.profileId, source: 'value-provider' };
        }
      }
    }]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const channel = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION).spotNodes.get('api').meshChannels.api;
  assert.deepEqual(channel.requestHandlers, []);
  assert.deepEqual(channel.sendHandlers, []);
  await app.close();
});

test('ZLinkModule.forRootFactory maps zlinkRequestHandler providers from NestJS DI', async () => {
  const apiEndpoint = await reserveTcpEndpoint();
  class ProfileHandler {
    async handle(request) {
      return { profileId: request.profileId, source: 'async-provider-group' };
    }
  }
  nestjs.zlinkRequestHandler('api', 'GetProfile')(ProfileHandler);

  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRootFactory({
      useFactory: () => nestjs.zlinkFramework()
        .addRouteMesh('api')
          .listen(apiEndpoint)
          .routingId('api-node')
          .channelName('api')
          .addHandlerGroup('api')
        .build()
    })],
    providers: [ProfileHandler]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const handlers = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION).spotNodes.get('api').meshChannels.api.requestHandlers;

  assert.equal(handlers.length, 1);
  assert.deepEqual(
    await handlers[0].handler.handle(Buffer.from(JSON.stringify({ profileId: 'p1' })), {}),
    { profileId: 'p1', source: 'async-provider-group' }
  );

  await app.close();
});

test('zlinkModule registers discovered handler providers in the application module context', async () => {
  const apiEndpoint = await reserveTcpEndpoint();
  const roleRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'zlink-nest-provider-discovery-'));
  const discoveryRoot = path.join(roleRoot, 'Handlers');
  fs.mkdirSync(discoveryRoot);
  const commonPackage = require.resolve('@nestjs/common');
  const nestPackage = path.resolve(__dirname, '../../packages/nestjs/dist');
  fs.writeFileSync(path.join(discoveryRoot, 'profile-handler.js'), `
const { Inject } = require(${JSON.stringify(commonPackage)});
const nestjs = require(${JSON.stringify(nestPackage)});

class ProfileHandler {
  constructor(store) {
    this.store = store;
  }

  async handle(request) {
    return { profileId: request.profileId, source: this.store.source };
  }
}

Inject('PROFILE_STORE')(ProfileHandler, undefined, 0);
nestjs.zlinkRequestHandler('api', 'GetProfile')(ProfileHandler);

module.exports = { ProfileHandler };
`);

  class HandlerModule {}
  nestjs.zlinkModule({
    imports: [
      nestjs.ZLinkModule.forRootFactory({
        useFactory: () => nestjs.zlinkFramework()
          .addRouteMesh('api')
            .listen(apiEndpoint)
            .routingId('api-node')
            .channelName('api')
            .addHandlerGroup('api')
          .build()
      })
    ],
    providerDiscovery: [discoveryRoot],
    providers: [
      { provide: 'PROFILE_STORE', useValue: { source: 'application-module-provider-discovery' } }
    ]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const handlers = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION).spotNodes.get('api').meshChannels.api.requestHandlers;

  assert.equal(handlers.length, 1);
  assert.deepEqual(
    await handlers[0].handler.handle(Buffer.from(JSON.stringify({ profileId: 'p1' })), {}),
    { profileId: 'p1', source: 'application-module-provider-discovery' }
  );

  await app.close();
});

test('zlinkModule role root discovers conventional handler directories', async () => {
  const apiEndpoint = await reserveTcpEndpoint();
  const roleRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'zlink-nest-role-root-'));
  const discoveryRoot = path.join(roleRoot, 'Handlers');
  fs.mkdirSync(discoveryRoot);
  const nestPackage = path.resolve(__dirname, '../../packages/nestjs/dist');
  fs.writeFileSync(path.join(discoveryRoot, 'profile-handler.js'), `
const nestjs = require(${JSON.stringify(nestPackage)});

class ProfileHandler {
  async handle(request) {
    return { profileId: request.profileId, source: 'role-root-discovery' };
  }
}

nestjs.zlinkRequestHandler('api', 'GetProfile')(ProfileHandler);

module.exports = { ProfileHandler };
`);

  class HandlerModule {}
  nestjs.zlinkModule(roleRoot, {
    imports: [
      nestjs.ZLinkModule.forRootFactory({
        useFactory: () => nestjs.zlinkFramework()
          .addRouteMesh('api')
            .listen(apiEndpoint)
            .routingId('api-node')
            .channelName('api')
            .addHandlerGroup('api')
          .build()
      })
    ]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const handlers = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION).spotNodes.get('api').meshChannels.api.requestHandlers;

  assert.equal(handlers.length, 1);
  assert.deepEqual(
    await handlers[0].handler.handle(Buffer.from(JSON.stringify({ profileId: 'p1' })), {}),
    { profileId: 'p1', source: 'role-root-discovery' }
  );

  await app.close();
});

test('ZLinkModule.forRoot deduplicates grouped useExisting handler aliases', async () => {
  const apiEndpoint = await reserveTcpEndpoint();
  const PROFILE_HANDLER = Symbol('profile-handler');
  class ProfileHandler {
    async handle(request) {
      return { profileId: request.profileId };
    }
  }
  nestjs.zlinkRequestHandler('api', 'GetProfile')(ProfileHandler);

  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addRouteMesh('api')
        .listen(apiEndpoint)
        .routingId('api-node')
        .channelName('api')
        .addHandlerGroup('api')
      .build())],
    providers: [
      ProfileHandler,
      { provide: PROFILE_HANDLER, useExisting: ProfileHandler }
    ]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const handlers = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION).spotNodes.get('api').meshChannels.api.requestHandlers;

  assert.equal(handlers.length, 1);

  await app.close();
});

test('ZLinkModule.forRoot rejects duplicate grouped packet handlers for one channel', async () => {
  const apiEndpoint = await reserveTcpEndpoint();
  class FirstProfileHandler {
    async handle() {
      return {};
    }
  }
  class SecondProfileHandler {
    async handle() {
      return {};
    }
  }
  nestjs.zlinkRequestHandler('api', 'GetProfile')(FirstProfileHandler);
  nestjs.zlinkRequestHandler('api', 'GetProfile')(SecondProfileHandler);

  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addRouteMesh('api')
        .listen(apiEndpoint)
        .routingId('api-node')
        .channelName('api')
        .addHandlerGroup('api')
      .build())],
    providers: [FirstProfileHandler, SecondProfileHandler]
  })(HandlerModule);

  await assert.rejects(
    () => NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false }),
    /Duplicate handler 'api:request:GetProfile'/
  );
});

test('ZLinkModule.forRoot maps explicit RouteMesh send handlers from NestJS DI', async () => {
  const routeEndpoint = await reserveTcpEndpoint();
  class NoticeHandler {
    constructor() {
      this.notices = [];
    }

    async handle(message, context) {
      this.notices.push({ message, sourceNodeRid: context.sourceNodeRid });
    }
  }
  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addRouteMesh('route')
        .listen(routeEndpoint)
        .routingId('node-a')
        .addSendHandler('RouteNotice', NoticeHandler)
        .channelName('route')
      .build())],
    providers: [NoticeHandler]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const routeMesh = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION).spotNodes.get('route');
  const noticeHandler = app.get(NoticeHandler, { strict: false });

  assert.equal(routeMesh.routeSendHandlers.length, 1);
  assert.equal(routeMesh.routeSendHandlers[0].packetName, 'RouteNotice');
  assert.equal(routeMesh.routeSendHandlers[0].handlerType, NoticeHandler);
  assert.deepEqual(noticeHandler.notices, []);

  await app.close();
});

test('ZLinkModule.forRoot maps manual client-server send handlers from NestJS DI', async () => {
  const endpoint = 'tcp://127.0.0.1:9409';
  class NoticeHandler {
    constructor() {
      this.notices = [];
    }

    async handle(message, context) {
      this.notices.push({ message, packetName: context.packetName });
    }
  }

  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRootFactory({
      useFactory: () => nestjs.zlinkFramework()
        .addRouteMesh('api')
          .listen(endpoint)
          .routingId('api-node')
          .channelName('api')
          .addSendHandler('Notice', NoticeHandler)
        .build()
    })],
    providers: [NoticeHandler]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const channel = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION).spotNodes.get('api').meshChannels.api;
  const noticeHandler = app.get(NoticeHandler, { strict: false });

  assert.equal(channel.sendHandlers.length, 1);
  assert.equal(channel.sendHandlers[0].packetName, 'Notice');
  await app.get(channel.sendHandlers[0].handlerType, { strict: false }).handle(
    { text: 'hello' },
    { channelName: 'api', packetName: 'Notice' }
  );
  assert.deepEqual(noticeHandler.notices, [{ message: { text: 'hello' }, packetName: 'Notice' }]);

  await app.close();
});

test('ZLinkModule.forRoot maps grouped fanout publish handlers from NestJS DI', async () => {
  const subscriberEndpoint = await reserveTcpEndpoint();
  const PROFILE_EVENTS = Symbol('profile-events');
  class ProfileEventHandler {
    constructor() {
      this.events = [];
    }

    async handle(message, context) {
      this.events.push({ message, topic: context.topic, packetName: context.packetName });
    }
  }
  nestjs.zlinkPublishHandler('events', 'ProfileChanged')(ProfileEventHandler);

  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addFanoutChannel('events')
        .enableSubscriber(subscriberEndpoint)
        .addHandlerGroup('events')
      .build())],
    providers: [{ provide: PROFILE_EVENTS, useClass: ProfileEventHandler }]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const registration = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION);
  const publishHandlers = registration.channels.get('events').publishHandlers;
  const profileEvents = app.get(PROFILE_EVENTS, { strict: false });

  assert.equal(publishHandlers.length, 1);
  assert.equal(publishHandlers[0].packetName, 'ProfileChanged');
  await publishHandlers[0].handler.handle(
    Buffer.from(JSON.stringify({ profileId: 'p1' })),
    { channelName: 'events', packetName: 'ProfileChanged', topic: 'profile.updated' }
  );
  assert.deepEqual(profileEvents.events, [{
    message: { profileId: 'p1' },
    topic: 'profile.updated',
    packetName: 'ProfileChanged'
  }]);

  await app.close();
});

test('ZLinkModule.forRoot rejects duplicate grouped publish handlers for one channel', async () => {
  const subscriberEndpoint = await reserveTcpEndpoint();
  class FirstEventHandler {
    async handle() {}
  }
  class SecondEventHandler {
    async handle() {}
  }
  nestjs.zlinkPublishHandler('events', 'ProfileChanged')(FirstEventHandler);
  nestjs.zlinkPublishHandler('events', 'ProfileChanged')(SecondEventHandler);

  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addFanoutChannel('events')
        .enableSubscriber(subscriberEndpoint)
        .addHandlerGroup('events')
      .build())],
    providers: [FirstEventHandler, SecondEventHandler]
  })(HandlerModule);

  await assert.rejects(
    () => NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false }),
    /Duplicate handler 'events:publish:ProfileChanged'/
  );
});

test('ZLinkModule.forRoot with grouped handlers exposes capability providers through NestJS context', async () => {
  const apiEndpoint = await reserveTcpEndpoint();
  const spotEndpoint = await reserveTcpEndpoint();
  class ActorFactory {
    async create(actorId, context) {
      return { actorId, context };
    }
  }
  class StageSpot {
    constructor(context) {
      this.context = context;
    }
  }
  class ProfileHandler {
    async handle(request) {
      return { profileId: request.profileId };
    }
  }
  nestjs.zlinkRequestHandler('api', 'GetProfile')(ProfileHandler);

  const options = nestjs.zlinkFramework()
    .addLocationStore(new framework.ZLinkInMemoryProviderLocationStore())
    .options({ spotPublisherClients: ['events'] });
  const gameMesh = options.addRouteMesh('game')
    .listen(spotEndpoint)
    .routingId('game-node');
  gameMesh.objects().server()
    .addSpotFactory(StageSpot.name, StageSpot, (factory) => factory.disableRelocation())
    .addActorFactory('player', ActorFactory, (factory) => factory.disableRelocation());
  gameMesh.channelName('game');
  options.addRouteMesh('api')
    .listen(apiEndpoint)
    .routingId('api-node')
    .channelName('api')
    .addHandlerGroup('api');

  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(options.build())],
    providers: [ProfileHandler]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const spotManager = app.get(nestjs.ZLINK_SPOT_MANAGER, { strict: false });
  const actorManager = app.get(nestjs.ZLINK_ACTOR_MANAGER, { strict: false });

  assert.equal(typeof spotManager.create, 'function');
  assert.equal(actorManager instanceof framework.DefaultZLinkActorManager, true);
  assert.equal(app.get(nestjs.ZLINK_SPOT_PUBLISHER_CLIENT, { strict: false }) instanceof framework.DefaultZLinkSpotPublisherClient, true);

  await app.close();
});

test('ZLinkModule.forRoot with grouped handlers omits only capabilities not implied by RouteMesh', async () => {
  const apiEndpoint = await reserveTcpEndpoint();
  class ProfileHandler {
    async handle(request) {
      return { profileId: request.profileId };
    }
  }
  nestjs.zlinkRequestHandler('api', 'GetProfile')(ProfileHandler);

  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addRouteMesh('api')
        .listen(apiEndpoint)
        .routingId('api-node')
        .channelName('api')
        .addHandlerGroup('api')
      .build())],
    providers: [ProfileHandler]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });

  assert.equal(app.get(nestjs.ZLINK_SPOT_MANAGER, { strict: false }), null);
  assert.equal(app.get(nestjs.ZLINK_ACTOR_MANAGER, { strict: false }), null);
  assert.equal(app.get(nestjs.ZLINK_SPOT_PUBLISHER_CLIENT, { strict: false }) instanceof framework.DefaultZLinkSpotPublisherClient, true);

  await app.close();
});

test('ZLinkModule.forRoot exposes exact create calls for registered Spot factories', async () => {
  class StageSpot {
    constructor(context) {
      this.context = context;
    }
  }
  class LocalStageSpot {
    constructor(context) {
      this.context = context;
    }
  }
  const options = nestjs.zlinkFramework()
    .addLocationStore(new framework.ZLinkInMemoryProviderLocationStore())
    .options({ spotFactories: [StageSpot] });
  const mesh = options.addRouteMesh('game')
    .listen('tcp://127.0.0.1:0');
  mesh.objects().server().addSpotFactory(
    LocalStageSpot.name,
    LocalStageSpot,
    (factory) => factory.disableRelocation()
  );
  const module = nestjs.ZLinkModule.forRoot(options.build());
  const container = await resolveModuleProviders(module, [nestjs.ZLINK_SPOT_MANAGER]);
  const spotManager = container.get(nestjs.ZLINK_SPOT_MANAGER);

  const configured = spotManager.create(StageSpot.name).inMesh('game');
  const localConfigured = spotManager.create(LocalStageSpot.name).inMesh('game');

  assert.equal(typeof configured.submit, 'function');
  assert.equal(typeof localConfigured.submit, 'function');
});

test('ZLinkModule.forRoot preserves Spot factories in the formal MeshNode registration', async () => {
  const spotEndpoint = uniqueIpcEndpoint('spot-factory');
  class SpotDependency {
    constructor() {
      this.marker = 'spot-di';
    }
  }
  class StageSpot {
    constructor(dependency) {
      this.dependency = dependency;
    }
  }
  Inject(SpotDependency)(StageSpot, undefined, 0);

  const options = nestjs.zlinkFramework();
  const mesh = options.addRouteMesh('game')
    .listen(spotEndpoint)
    .routingId('game-node');
  mesh.objects().server().addSpotFactory(
    StageSpot.name,
    StageSpot,
    (factory) => factory.disableRelocation()
  );
  mesh.channelName('game');
  const frameworkModule = nestjs.ZLinkModule.forRoot(options.build());
  class HandlerModule {}
  Module({
    imports: [frameworkModule],
    providers: [SpotDependency, StageSpot]
  })(HandlerModule);

  const registration = await resolveFrameworkRegistration(frameworkModule);
  assert.deepEqual(registration.spotNodes.get('game').spotFactories, [StageSpot]);
});

test('ZLinkModule.forRoot preserves Entry Spot type in the formal MeshNode registration', async () => {
  const spotEndpoint = uniqueIpcEndpoint('entry-spot');
  class EntryDependency {
    constructor() {
      this.initialized = false;
    }
  }
  class StageEntrySpot {
    constructor(dependency) {
      this.dependency = dependency;
    }

    async onInitialize() {
      this.dependency.initialized = true;
    }
  }
  Inject(EntryDependency)(StageEntrySpot, undefined, 0);

  const options = nestjs.zlinkFramework();
  const mesh = options.addRouteMesh('game')
    .listen(spotEndpoint)
    .routingId('game-node');
  mesh.objects().server().addEntrySpot(StageEntrySpot);
  mesh.channelName('game');
  const frameworkModule = nestjs.ZLinkModule.forRoot(options.build());
  class HandlerModule {}
  Module({
    imports: [frameworkModule],
    providers: [EntryDependency, StageEntrySpot]
  })(HandlerModule);

  const registration = await resolveFrameworkRegistration(frameworkModule);
  assert.equal(registration.spotNodes.get('game').entrySpotType, StageEntrySpot);
});

test('ZLinkModule.forRoot preserves Actor factories in the formal MeshNode registration', async () => {
  const spotEndpoint = uniqueIpcEndpoint('actor-factory');
  class ActorDependency {
    constructor() {
      this.marker = 'actor-di';
    }
  }
  class PlayerActorFactory {
    constructor(dependency) {
      this.dependency = dependency;
    }

    async create(actorId, context) {
      return { actorId, context, marker: this.dependency.marker };
    }
  }
  Inject(ActorDependency)(PlayerActorFactory, undefined, 0);

  const options = nestjs.zlinkFramework();
  const mesh = options.addRouteMesh('game')
    .listen(spotEndpoint)
    .routingId('game-node');
  mesh.objects().server().addActorFactory(
    'player',
    PlayerActorFactory,
    (factory) => factory.disableRelocation()
  );
  mesh.channelName('game');
  const frameworkModule = nestjs.ZLinkModule.forRoot(options.build());
  class HandlerModule {}
  Module({
    imports: [frameworkModule],
    providers: [ActorDependency, PlayerActorFactory]
  })(HandlerModule);

  const registration = await resolveFrameworkRegistration(frameworkModule);
  assert.equal(registration.spotNodes.get('game').actorFactories.player, PlayerActorFactory);
});

test('ZLinkModule.forRoot discovers SPOT actor request handler decorators from NestJS providers', async () => {
  const spotEndpoint = uniqueIpcEndpoint('handler-router');
  const spotPubSubEndpoint = uniqueIpcEndpoint('handler-pubsub');
  class PlayerActor {}
  class EntrySpot {}
  class RoomSpot {}
  class EntryPacketHandler {}
  class EntrySubscriptionHandler {}
  class RoomPacketHandler {}
  class RoomSubscriptionHandler {}
  class EntryNoticeHandler {}
  class MatchHandler {}
  class RoomNoticeHandler {}
  class SubmitHandler {}
  class RoomTimerHandler {}
  nestjs.zlinkEntrySpotActorSendHandler({
    actor: () => PlayerActor,
    entrySpot: () => EntrySpot,
    packetName: 'entry.notice'
  })(EntryNoticeHandler);
  nestjs.zlinkEntrySpotActorRequestHandler({
    actor: () => PlayerActor,
    entrySpot: () => EntrySpot,
    packetName: 'match'
  })(MatchHandler);
  nestjs.zlinkSpotActorSendHandler({
    actor: () => PlayerActor,
    packetName: 'room.notice',
    spot: () => RoomSpot
  })(RoomNoticeHandler);
  nestjs.zlinkSpotActorRequestHandler({
    actor: () => PlayerActor,
    packetName: 'submit',
    spot: () => RoomSpot
  })(SubmitHandler);
  nestjs.zlinkSpotTimerHandler({
    name: 'room.tick',
    periodMs: 250,
    spot: () => RoomSpot
  })(RoomTimerHandler);
  nestjs.zlinkEntrySpotPacketHandler({
    entrySpot: () => EntrySpot,
    packetName: 'entry.packet'
  })(EntryPacketHandler);
  nestjs.zlinkEntrySpotSubscriptionHandler({
    entrySpot: () => EntrySpot,
    channelName: 'game',
    topic: 'entry.topic'
  })(EntrySubscriptionHandler);
  nestjs.zlinkSpotPacketHandler({
    packetName: 'room.packet',
    spot: () => RoomSpot
  })(RoomPacketHandler);
  nestjs.zlinkSpotSubscriptionHandler({
    spot: () => RoomSpot,
    channelName: 'game',
    topic: 'room.topic'
  })(RoomSubscriptionHandler);

  class TestModule {}
  const options = nestjs.zlinkFramework();
  const mesh = options.addRouteMesh('game')
    .listen(spotEndpoint)
    .routingId('game-node');
  mesh.objects().server()
    .addEntrySpot(EntrySpot)
    .addSpotFactory(RoomSpot.name, RoomSpot, (factory) => factory.disableRelocation());
  mesh.channelName('game');
  Module({
    imports: [nestjs.ZLinkModule.forRoot(options.build())],
    providers: [
      EntryPacketHandler,
      EntrySubscriptionHandler,
      EntryNoticeHandler,
      MatchHandler,
      RoomPacketHandler,
      RoomSubscriptionHandler,
      RoomNoticeHandler,
      SubmitHandler,
      RoomTimerHandler
    ]
  })(TestModule);

  const app = await NestFactory.createApplicationContext(TestModule, { logger: false, abortOnError: false });
  const registration = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION);
  const spotNode = registration.spotNodes.get('game');

  assert.deepEqual(spotNode.entrySpotActorSendHandlers, [{
    actorType: PlayerActor,
    entrySpotType: EntrySpot,
    handlerType: EntryNoticeHandler,
    packetName: 'entry.notice'
  }]);
  assert.deepEqual(spotNode.entrySpotActorRequestHandlers, [{
    actorType: PlayerActor,
    entrySpotType: EntrySpot,
    handlerType: MatchHandler,
    packetName: 'match'
  }]);
  assert.deepEqual(spotNode.entrySpotPacketHandlers, [{
    entrySpotType: EntrySpot,
    handlerType: EntryPacketHandler,
    packetName: 'entry.packet'
  }]);
  assert.deepEqual(spotNode.entrySpotSubscriptionHandlers, [{
    entrySpotType: EntrySpot,
    handlerType: EntrySubscriptionHandler,
    channelName: 'game',
    topic: 'entry.topic'
  }]);
  assert.deepEqual(spotNode.spotActorSendHandlers, [{
    actorType: PlayerActor,
    handlerType: RoomNoticeHandler,
    packetName: 'room.notice',
    spotType: RoomSpot
  }]);
  assert.deepEqual(spotNode.spotActorRequestHandlers, [{
    actorType: PlayerActor,
    handlerType: SubmitHandler,
    packetName: 'submit',
    spotType: RoomSpot
  }]);
  assert.deepEqual(spotNode.spotPacketHandlers, [{
    handlerType: RoomPacketHandler,
    packetName: 'room.packet',
    spotType: RoomSpot
  }]);
  assert.deepEqual(spotNode.spotSubscriptionHandlers, [{
    channelName: 'game',
    handlerType: RoomSubscriptionHandler,
    spotType: RoomSpot,
    topic: 'room.topic'
  }]);
  assert.deepEqual(spotNode.spotTimerHandlers, [{
    handlerType: RoomTimerHandler,
    name: 'room.tick',
    options: undefined,
    periodMs: 250,
    spotType: RoomSpot
  }]);

  await app.close();
});

test('ZLinkModule.forRoot rejects duplicate SPOT actor request handler decorators', async () => {
  class PlayerActor {}
  class RoomSpot {}
  class FirstHandler {}
  class SecondHandler {}
  nestjs.zlinkSpotActorRequestHandler({
    actor: () => PlayerActor,
    packetName: 'submit',
    spot: () => RoomSpot
  })(FirstHandler);
  nestjs.zlinkSpotActorRequestHandler({
    actor: () => PlayerActor,
    packetName: 'submit',
    spot: () => RoomSpot
  })(SecondHandler);

  const options = nestjs.zlinkFramework();
  const mesh = options.addRouteMesh('game')
    .listen('tcp://127.0.0.1:0');
  mesh.objects().server().addSpotFactory(
    RoomSpot.name,
    RoomSpot,
    (factory) => factory.disableRelocation()
  );
  class TestModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(options.build())],
    providers: [FirstHandler, SecondHandler]
  })(TestModule);

  await assert.rejects(
    () => NestFactory.createApplicationContext(TestModule, { logger: false, abortOnError: false }),
    /Duplicate SPOT actor handler/
  );
});

test('ZLinkModule.forRoot validates multiple actor-capable spot nodes at registration time', async () => {
  class ActorFactory {}

  const options = nestjs.zlinkFramework();
  options.addRouteMesh('alpha').objects().server().addActorFactory(
    'player',
    ActorFactory,
    (factory) => factory.disableRelocation()
  );
  options.addRouteMesh('beta').objects().server().addActorFactory(
    'mage',
    ActorFactory,
    (factory) => factory.disableRelocation()
  );
  const module = nestjs.ZLinkModule.forRoot(options.build());

  await assert.rejects(
    () => resolveFrameworkRegistration(module),
    framework.ZLinkConfigurationException
  );
});

test('ZLinkModule.forRoot validates channel capability endpoints and peer acquisition', async () => {
  await assert.rejects(
    () => resolveFrameworkRegistration(nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addRouteMesh('api')
        .listen('')
      .build())),
    /bind endpoint/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addFanoutChannel('events')
        .enablePublisher(undefined)
      .build()),
    /publisher must define a bind endpoint/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addFanoutChannel('events')
        .enableSubscriber()
      .build()),
    /subscriber requires location stores or manual connections/
  );
  await assert.rejects(
    () => {
      const builder = nestjs.zlinkFramework();
      builder.addRouteMesh('api').peerConnections().connect('');
      return resolveFrameworkRegistration(nestjs.ZLinkModule.forRoot(builder.build()));
    },
    /manual connection endpoint must not be empty/
  );

  assert.doesNotThrow(() => nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
    .addFanoutChannel('events')
      .enableSubscriber('tcp://127.0.0.1:7002')
      .addPublishHandler('NoopEvent', NoopPublishHandler)
    .build()));
});

test('ZLinkModule.forRoot maps route mesh channel options into runtime registration', async () => {
  const builder = nestjs.zlinkFramework();
  const mesh = builder.addRouteMesh('route')
    .listen('tcp://0.0.0.0:7012')
    .routingId('node-a');
  mesh.channelName('route');
  mesh.peerConnections().connect('tcp://127.0.0.1:7013');
  const module = nestjs.ZLinkModule.forRoot(builder.build());
  const registration = await resolveFrameworkRegistration(module);
  const route = registration.spotNodes.get('route');

  assert.equal(route.router.bind, 'tcp://0.0.0.0:7012');
  assert.equal(route.router.routingId, 'node-a');
  assert.deepEqual(route.router.manualConnections, ['tcp://127.0.0.1:7013']);
  assert.deepEqual(Object.keys(route.meshChannels), ['route']);
});

test('zlinkFramework preserves actor relocation adapters in the factory registration', async () => {
  class PlayerActorFactory {}
  class PlayerActorRelocationAdapter {}
  const builder = nestjs.zlinkFramework()
    .addLocationStore(new framework.ZLinkInMemoryProviderLocationStore());
  const mesh = builder.addRouteMesh('game')
    .listen('inproc://actor-transfer')
    .routingId('game-node');
  mesh.objects().server().addActorFactory(
    'player',
    PlayerActorFactory,
    (factory) => factory.preserveStateWith(PlayerActorRelocationAdapter)
  );
  mesh.channelName('game');
  const registration = await resolveFrameworkRegistration(
    nestjs.ZLinkModule.forRoot(builder.build())
  );
  assert.equal(
    registration.spotNodes.get('game')
      .actorFactoryRegistrations.player.relocation.adapterType,
    PlayerActorRelocationAdapter
  );
});

test('zlinkFramework preserves actor transfer timeout and Message Follow duration', async () => {
  const registration = await resolveFrameworkRegistration(
    nestjs.ZLinkModule.forRoot(
      nestjs.zlinkFramework()
        .setActorTransferTimeout(12_000)
        .setMessageFollowDuration(4_000)
        .build()
    )
  );

  assert.equal(registration.actorTransferTimeoutMs, 12_000);
  assert.equal(registration.messageFollowDurationMs, 4_000);
  assert.throws(
    () => nestjs.zlinkFramework().setActorTransferTimeout(0),
    /actor transfer timeout must be a positive safe integer/
  );
});

test('zlinkFramework preserves the metrics provider in the runtime registration', async () => {
  const meterProvider = { getMeter() {} };
  const registration = await resolveFrameworkRegistration(
    nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .options({ metrics: { meterProvider } })
      .build())
  );

  assert.equal(registration.metrics.meterProvider, meterProvider);
});

test('zlinkFramework applies core SPOT registration policy before duplicate state is lost', () => {
  class EntrySpot {}
  class Spot {}
  class ActorFactory {}

  const entryNode = nestjs.zlinkFramework().addRouteMesh('entry');
  const entryServer = entryNode.objects().server();
  entryServer.addEntrySpot(EntrySpot);
  assert.throws(() => entryServer.addEntrySpot(EntrySpot), /Duplicate Entry Spot registration/);

  const factoryNode = nestjs.zlinkFramework().addRouteMesh('factory');
  const factoryServer = factoryNode.objects().server();
  factoryServer.addSpotFactory(Spot.name, Spot, (factory) => factory.disableRelocation());
  assert.throws(
    () => factoryServer.addSpotFactory(Spot.name, Spot, (factory) => factory.disableRelocation()),
    /Duplicate object factory/
  );

  const actorNode = nestjs.zlinkFramework().addRouteMesh('actor');
  const actorServer = actorNode.objects().server();
  actorServer.addActorFactory('player', ActorFactory, (factory) => factory.disableRelocation());
  assert.throws(
    () => actorServer.addActorFactory('player', ActorFactory, (factory) => factory.disableRelocation()),
    /Duplicate actor factory|Duplicate object factory/
  );
  assert.throws(
    () => actorServer.addActorFactory(' player ', ActorFactory, (factory) => factory.disableRelocation()),
    /must not be empty or padded/
  );
});

test('framework options builder maps the formal RouteMesh registration flow into options', () => {
  class GatewaySession {}
  class StageSpot {
    constructor(context) {
      this.context = context;
    }
  }
  class LocalStageSpot {}
  class StageEntrySpot {}
  class StageActor {}
  class StageActorRelocationAdapter {}
  const streamCompressionCodec = {
    compress(payload) {
      return payload;
    },
    decompress(payload) {
      return payload;
    }
  };

  const options = framework.createFrameworkOptions((builder) => {
    builder.addLocationStore(new framework.ZLinkInMemoryProviderLocationStore());
    builder.configureStreamCompression().use(streamCompressionCodec);
    const events = builder.addFanoutChannel('events');
    events.enablePublisher('tcp://0.0.0.0:9402');
    events.enableSubscriber('tcp://127.0.0.1:9402');
    const route = builder.addRouteMesh('route');
    route.listen('tcp://0.0.0.0:9403');
    route.routingId('route-node');
    route.channelName('route');
    route.peerConnections().connect('tcp://127.0.0.1:9403');
    builder.addStreamNode('gateway')
      .bind('tcp://0.0.0.0:9404')
      .registerSession(GatewaySession);
    const spot = builder.addRouteMesh('game.stage');
    const objectServer = spot.objects().server();
    objectServer
      .addSpotFactory(StageSpot.name, StageSpot, (factory) => factory.disableRelocation())
      .addSpotFactory(LocalStageSpot.name, LocalStageSpot, (factory) => factory.disableRelocation())
      .addEntrySpot(StageEntrySpot);
    objectServer.addActorFactory(
      'stage',
      StageActor,
      (factory) => factory.preserveStateWith(StageActorRelocationAdapter)
    );
    spot.listen('tcp://0.0.0.0:9405');
    spot.routingId('stage-node');
    spot.channelName('game.stage');
  });
  options.channels.events.publishHandlers = [{
    packetName: 'NoopEvent',
    handler: new NoopPublishHandler()
  }];

  const registration = framework.createFrameworkRegistration(options);
  const spotNode = registration.spotNodes.get('game.stage');
  const streamNode = registration.streamNodes.get('gateway');
  const route = registration.spotNodes.get('route');

  assert.equal(
    spotNode.actorFactoryRegistrations.stage.relocation.adapterType,
    StageActorRelocationAdapter
  );
  assert.equal(registration.channels.get('events').publisher.bind, 'tcp://0.0.0.0:9402');
  assert.deepEqual(registration.channels.get('events').subscriber.manualConnections, ['tcp://127.0.0.1:9402']);
  assert.equal(route.router.bind, 'tcp://0.0.0.0:9403');
  assert.deepEqual(route.router.manualConnections, ['tcp://127.0.0.1:9403']);
  assert.equal(streamNode.bind, 'tcp://0.0.0.0:9404');
  assert.equal(streamNode.session, GatewaySession);
  assert.equal(registration.streamCompression.codec, streamCompressionCodec);
  assert.equal(registration.streamCompression.disabled, false);
  assert.equal(registration.spotFactories.has(StageSpot), true);
  assert.equal(registration.spotFactories.has(LocalStageSpot), true);
  assert.equal(spotNode.entrySpotType, StageEntrySpot);
  assert.equal(spotNode.entrySpot, undefined);
  assert.deepEqual(spotNode.spotFactories, [StageSpot, LocalStageSpot]);
  assert.equal(spotNode.router.bind, 'tcp://0.0.0.0:9405');
  assert.deepEqual(spotNode.router.manualConnections, undefined);
  assert.deepEqual(Object.keys(spotNode.meshChannels), ['game.stage']);

  assert.throws(
    () => framework.createFrameworkOptions((builder) => builder.addRouteMesh('')),
    /RouteMesh must not be empty/
  );
  assert.throws(
    () => framework.createFrameworkOptions((builder) => {
      builder.addRouteMesh('game.stage');
      builder.addRouteMesh('game.stage');
    }),
    /Duplicate RouteMesh 'game.stage'/
  );
  assert.throws(
    () => framework.createFrameworkOptions((builder) => {
      const node = builder.addRouteMesh('game.stage');
      const server = node.objects().server();
      server.addEntrySpot(StageEntrySpot);
      server.addEntrySpot(StageEntrySpot);
    }),
    /Duplicate Entry Spot registration/
  );
  assert.throws(
    () => framework.createFrameworkOptions((builder) => {
      const node = builder.addRouteMesh('game.stage');
      const server = node.objects().server();
      server.addSpotFactory(StageSpot.name, StageSpot, (factory) => factory.disableRelocation());
      server.addSpotFactory(StageSpot.name, StageSpot, (factory) => factory.disableRelocation());
    }),
    /Duplicate object factory/
  );
  assert.throws(() => framework.createFrameworkOptions((builder) => {
    const node = builder.addRouteMesh('game');
    const server = node.objects().server();
    server.addActorFactory(
      'stage',
      StageActor,
      (factory) => factory.preserveStateWith(StageActorRelocationAdapter)
    );
    server.addActorFactory(
      'stage',
      StageActor,
      (factory) => factory.preserveStateWith(StageActorRelocationAdapter)
    );
  }), /Duplicate actor factory/);
});

test('ZLinkModule.forRoot maps stream node options into runtime registration', async () => {
  class ClientHeaderSession {
    constructor(context) {
      this.context = context;
    }
  }
  const module = nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
    .addRouteMesh('game.spot')
      .listen('tcp://0.0.0.0:9110')
    .addStreamNode('client.stream')
      .bind('tcp://0.0.0.0:9100')
      .registerSession(ClientHeaderSession)
    .build());
  const registration = await resolveFrameworkRegistration(module);
  const streamNode = registration.streamNodes.get('client.stream');

  assert.equal(streamNode.bind, 'tcp://0.0.0.0:9100');
  assert.equal(streamNode.session, ClientHeaderSession);
  assert.equal(registration.spotNodes.get('game.spot').router.bind, 'tcp://0.0.0.0:9110');

  assert.throws(
    () => framework.createFrameworkOptions((builder) => builder.addStreamNode('')),
    /STREAM node name must not be empty or padded/
  );
  assert.throws(
    () => framework.createFrameworkOptions((builder) => {
      builder.addStreamNode('client.stream');
      builder.addStreamNode('client.stream');
    }),
    /Duplicate STREAM node 'client\.stream'/
  );

  assert.throws(
    () => framework.createFrameworkOptions((builder) => {
      builder.addStreamNode('client.stream')
        .bind('tcp://0.0.0.0:9100')
        .registerSession(ClientHeaderSession)
        .registerSession(ClientHeaderSession);
    }),
    /STREAM node cannot register more than one header stream session/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addStreamNode('missing-bind')
        .registerSession(ClientHeaderSession)
      .build()),
    /STREAM node 'missing-bind' must define a bind endpoint/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addStreamNode('missing-session')
        .bind('tcp://0.0.0.0:9101')
      .build()),
    /STREAM node 'missing-session' must register a header stream session/
  );
});

test('zlinkFramework builder maps stream node registration without raw server code', async () => {
  class PlayerActorFactory {}
  class ClientHeaderSession {
    constructor(context) {
      this.context = context;
    }
  }
  class StageEntrySpot {}

  const options = nestjs.zlinkFramework()
    .options({ spotPublisherClients: ['game.spot'] });
  options.addRouteMesh('api')
        .listen('tcp://0.0.0.0:9113')
        .routingId('api-node')
        .channelName('api')
        .addRequestHandler('NoopRequest', NoopRequestHandler);
  options.addFanoutChannel('game.events')
        .enablePublisher('tcp://0.0.0.0:9114');
  options.addRouteMesh('route')
        .listen('tcp://0.0.0.0:9115')
        .routingId('route-node')
        .channelName('route');
  options.addStreamNode('client.stream')
        .bind('tcp://0.0.0.0:9100')
        .registerSession(ClientHeaderSession);
  const spotMesh = options.addRouteMesh('game.spot')
    .listen('tcp://0.0.0.0:9110')
    .routingId('game-node');
  spotMesh.objects().server()
    .addEntrySpot(StageEntrySpot)
    .addActorFactory(
      'player',
      PlayerActorFactory,
      (factory) => factory.disableRelocation()
    );
  spotMesh.channelName('game.spot');
  const module = nestjs.ZLinkModule.forRoot(options.build());
  const registration = await resolveFrameworkRegistration(module);
  const streamNode = registration.streamNodes.get('client.stream');
  const spotNode = registration.spotNodes.get('game.spot');

  assert.equal(streamNode.bind, 'tcp://0.0.0.0:9100');
  assert.equal(streamNode.session, ClientHeaderSession);
  assert.equal(registration.actorFactories.get('player'), PlayerActorFactory);
  assert.equal(spotNode.actorFactories.player, PlayerActorFactory);
  assert.equal(spotNode.router.bind, 'tcp://0.0.0.0:9110');
  assert.equal(spotNode.router.routingId, 'game-node');
  assert.deepEqual(Object.keys(spotNode.meshChannels), ['game.spot']);
  assert.equal(registration.spotPublisherClients.has('game.spot'), true);
  assert.equal(spotNode.entrySpot, undefined);
  assert.equal(spotNode.entrySpotType, StageEntrySpot);
});

test('ZLinkModule.forRoot validates and maps formal MeshNode router and peer options', async () => {
  const builder = nestjs.zlinkFramework();
  const game = builder.addRouteMesh('game')
    .listen('tcp://0.0.0.0:9201')
    .routingId('node-a');
  game.channelName('game');
  game.peerConnections().connect('tcp://127.0.0.1:9202');
  builder.addRouteMesh('api')
      .listen('tcp://0.0.0.0:9208')
      .routingId('api-node')
      .channelName('api')
      .addRequestHandler('NoopRequest', NoopRequestHandler);
  builder.addRouteMesh('route')
      .listen('tcp://0.0.0.0:9209')
      .routingId('route-node')
      .channelName('route');
  const module = nestjs.ZLinkModule.forRoot(builder.build());
  const registration = await resolveFrameworkRegistration(module);
  const spotNode = registration.spotNodes.get('game');

  assert.equal(spotNode.router.bind, 'tcp://0.0.0.0:9201');
  assert.equal(spotNode.router.routingId, 'node-a');
  assert.deepEqual(spotNode.router.manualConnections, ['tcp://127.0.0.1:9202']);

  const orderedBuilder = nestjs.zlinkFramework();
  const ordered = orderedBuilder.addRouteMesh('ordered')
    .listen('tcp://0.0.0.0:9214')
    .routingId('node-a');
  ordered.channelName('ordered');
  ordered.peerConnections().connect('tcp://127.0.0.1:9211');
  ordered.peerConnections().connect('node-b', 'tcp://127.0.0.1:9212');
  const orderedModule = nestjs.ZLinkModule.forRoot(orderedBuilder.build());
  const orderedRegistration = await resolveFrameworkRegistration(orderedModule);
  const orderedSpotNode = orderedRegistration.spotNodes.get('ordered');
  assert.deepEqual(orderedSpotNode.router.manualConnections, ['tcp://127.0.0.1:9211']);
  assert.deepEqual(orderedSpotNode.router.manualPeerConnections, [{ peerRid: 'node-b', endpoint: 'tcp://127.0.0.1:9212' }]);

  await assert.rejects(
    async () => resolveFrameworkRegistration(nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addRouteMesh('')
      .build())),
    /RouteMesh name must not be empty/
  );
  await assert.rejects(
    async () => resolveFrameworkRegistration(nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addRouteMesh('game')
        .listen('')
      .build())),
    /SpotNode 'game' router must define a bind endpoint/
  );
  await assert.rejects(
    async () => {
      const builder = nestjs.zlinkFramework();
      const mesh = builder.addRouteMesh('game')
        .listen('tcp://127.0.0.1:9220')
        .routingId('game-node');
      mesh.channelName('game');
      mesh.peerConnections().connect('node-a', 'tcp://127.0.0.1:9216');
      mesh.peerConnections().connect('node-a', 'tcp://127.0.0.1:9217');
      return resolveFrameworkRegistration(nestjs.ZLinkModule.forRoot(builder.build()));
    },
    /manual peer routing id must be unique/
  );
});

test('ZLinkModule.forRoot registers explicit MeshNode publisher clients', async () => {
  const registration = await resolveFrameworkRegistration(nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
    .options({ spotPublisherClients: ['game'] })
    .addRouteMesh('game')
      .listen('tcp://0.0.0.0:9210')
      .routingId('game-node')
      .channelName('game')
    .build()));

  assert.equal(registration.spotPublisherClients.has('game'), true);
});

test('ZLinkModule.forRoot maps one location store into runtime registration', async () => {
  const store = new framework.ZLinkInMemoryProviderLocationStore();
  const registration = await resolveFrameworkRegistration(nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
    .addLocationStore(store)
    .build()));

  assert.equal(registration.locations.storeInstance, store);
});

test('ZLinkModule.forRoot exposes only the public Spot manager capability', async () => {
  const store = new framework.ZLinkInMemoryProviderLocationStore();
  class ManagedSpot {}
  const options = nestjs.zlinkFramework().addLocationStore(store);
  const mesh = options.addRouteMesh('game')
    .listen(uniqueIpcEndpoint('public-spot-manager'))
    .routingId('game-node');
  mesh.objects().server().addSpotFactory(
    ManagedSpot.name,
    ManagedSpot,
    (factory) => factory.disableRelocation()
  );
  mesh.channelName('game');
  const module = nestjs.ZLinkModule.forRoot(options.build());
  const tokens = providerTokens(module);

  assert.equal(tokens.has(nestjs.ZLINK_SPOT_MANAGER), true);
  assert.equal(Object.hasOwn(nestjs, 'ZLINK_SPOT_HANDLE_RESOLVER'), false);
  assert.equal(Object.hasOwn(nestjs, 'ZLINK_ACTOR_SPOT_HANDLE_RESOLVER'), false);
  assert.equal(Object.hasOwn(nestjs, 'ZLINK_SPOT_' + 'REMOTE_ADDRESS_RESOLVER'), false);
});

test('ZLinkModule.forRootFactory exposes capability providers through the real NestJS app context', async () => {
  const spotEndpoint = uniqueIpcEndpoint('async-capabilities');
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
  const module = nestjs.ZLinkModule.forRootFactory({
    async useFactory() {
      const options = nestjs.zlinkFramework()
        .addLocationStore(new framework.ZLinkInMemoryProviderLocationStore())
        .options({ spotPublisherClients: ['game-events'] });
      const mesh = options.addRouteMesh('game')
        .listen(spotEndpoint)
        .routingId('game-node');
      mesh.objects().server()
        .addSpotFactory(AsyncSpot.name, AsyncSpot, (factory) => factory.disableRelocation())
        .addActorFactory('player', ActorFactory, (factory) => factory.disableRelocation());
      mesh.channelName('game');
      return options.build();
    }
  });
  class AsyncModule {}
  Module({ imports: [module] })(AsyncModule);
  const app = await NestFactory.createApplicationContext(AsyncModule, { logger: false, abortOnError: false });
  const spotManager = app.get(nestjs.ZLINK_SPOT_MANAGER, { strict: false });
  const actorManager = app.get(nestjs.ZLINK_ACTOR_MANAGER, { strict: false });

  assert.equal(typeof spotManager.create, 'function');
  assert.equal(actorManager instanceof framework.DefaultZLinkActorManager, true);
  assert.equal(app.get(nestjs.ZLINK_SPOT_PUBLISHER_CLIENT, { strict: false }) instanceof framework.DefaultZLinkSpotPublisherClient, true);
  await app.close();
});

test('ZLinkModule.forRootFactory resolves factory dependencies from imported NestJS modules', async () => {
  const CONFIG = Symbol('config');
  const apiEndpoint = await reserveTcpEndpoint();
  class ConfigHandler {
    async handle(request) {
      return request;
    }
  }
  class ConfigModule {}
  Module({
    providers: [{ provide: CONFIG, useValue: { channelName: 'api', bind: apiEndpoint } }],
    exports: [CONFIG]
  })(ConfigModule);

  const module = nestjs.ZLinkModule.forRootFactory({
    imports: [ConfigModule],
    inject: [CONFIG],
    async useFactory(config) {
      return nestjs.zlinkFramework()
        .addRouteMesh(config.channelName)
          .listen(config.bind)
          .routingId('config-node')
          .channelName(config.channelName)
          .addRequestHandler('ConfigReq', ConfigHandler)
        .build();
    }
  });
  class AsyncModule {}
  Module({ imports: [module] })(AsyncModule);

  const app = await NestFactory.createApplicationContext(AsyncModule, { logger: false, abortOnError: false });
  const registration = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION, { strict: false });

  assert.equal(registration.spotNodes.get('api').router.bind, apiEndpoint);
  await app.close();
});

test('ZLinkModule.forRootFactory preserves route mesh transport options after dynamic handler merge', async () => {
  const module = nestjs.ZLinkModule.forRootFactory({
    useFactory() {
      const builder = nestjs.zlinkFramework();
      const mesh = builder.addRouteMesh('quest.route')
        .listen('tcp://127.0.0.1:7111')
        .routingId('gamequest-api-a');
      mesh.channelName('quest.route');
      mesh.peerConnections().connect('tcp://127.0.0.1:7112');
      return builder.build();
    }
  });
  const registration = await resolveFrameworkRegistration(module);
  const route = registration.spotNodes.get('quest.route');

  assert.equal(route.router.bind, 'tcp://127.0.0.1:7111');
  assert.equal(route.router.routingId, 'gamequest-api-a');
  assert.deepEqual(route.router.manualConnections, ['tcp://127.0.0.1:7112']);
});

test('ZLinkModule.forRootFactory boots through NestJS when async capability providers are absent', async () => {
  const module = nestjs.ZLinkModule.forRootFactory({
    async useFactory() {
      return nestjs.zlinkFramework().build();
    }
  });
  class AsyncModule {}
  Module({ imports: [module] })(AsyncModule);

  const app = await NestFactory.createApplicationContext(AsyncModule, { logger: false, abortOnError: false });
  assert.equal(app.get(nestjs.ZLINK_SPOT_MANAGER, { strict: false }), null);
  assert.equal(app.get(nestjs.ZLINK_ACTOR_MANAGER, { strict: false }), null);
  assert.equal(app.get(nestjs.ZLINK_SPOT_PUBLISHER_CLIENT, { strict: false }), null);
  await app.close();
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
	      },
	      createMonitoringAdapter() {
	        return createNoopMonitoringAdapter();
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

test('framework runtime host starts registered stream nodes and disposes their resources', async () => {
  class ClientHeaderSession {
    constructor(context) {
      this.context = context;
    }
  }
  const calls = [];
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({
      streamNodes: {
        'client.stream': {
          bind: 'tcp://0.0.0.0:9100',
          session: ClientHeaderSession
        }
      }
    })
  }, {
    backendAdapterFactory: {
      createChannelAdapter() {
        return {
          createContext() {
            calls.push('context:create');
            return {
              nativeInstance: {},
              shutdown() {},
              async dispose() {
                calls.push('context:dispose');
              }
            };
          }
        };
      },
      createStreamAdapter() {
        return {
          createStreamSocket() {
            calls.push('stream:create');
            return {
              nativeInstance: {},
              bind(endpoint) {
                calls.push(`stream:bind:${endpoint}`);
              },
              setChannelName() {},
              onFramedPacket(handler) {
                assert.equal(typeof handler, 'function');
                calls.push('stream:onFramedPacket');
              },
              send() { return true; },
              disconnectPeer() {},
              async bindActor() {},
              async unbindActor() {},
              sendBoundActor() { return true; },
              async dispose() {
                calls.push('stream:dispose');
              }
            };
          }
        };
      },
      createMonitoringAdapter() {
        return {
          openSocketMonitor() {
            calls.push('monitor:open');
            return {
              nativeInstance: {},
              onEvent() {},
              recv() { return {}; },
              async dispose() {
                calls.push('monitor:dispose');
              }
            };
          }
        };
      }
    }
  });

  await runtime.start();
  await runtime.stop();

  assert.deepEqual(calls, [
    'context:create',
    'stream:create',
    'stream:bind:tcp://0.0.0.0:9100',
    'monitor:open',
    'stream:onFramedPacket',
    'monitor:dispose',
    'stream:dispose',
    'context:dispose'
  ]);
});

test('framework runtime host attaches stream SessionRelay to registered SpotNode runtime', async () => {
  class ClientHeaderSession {
    constructor(context) {
      this.context = context;
    }
  }
  const calls = [];
  const spotNode = {
    nativeInstance: {},
    routingId: 'game.spot',
    setRoutingId() {},
    setRouterBind(endpoint) { calls.push(`spot:setRouterBind:${endpoint}`); },
    setPubBind() {},
    attachDiscovery() {},
    connectPeer() {},
    disconnectPeer() {},
    createPublisher() { return { close() {} }; },
    createSpot() {
      return {
        onSendReady() {},
        async dispose() {
          calls.push('publisher:dispose');
        }
      };
    },
    getOrCreateSpot(spotId) {
      calls.push(`spot:getOrCreateSpot:${spotId}`);
      return { spot: routeSourceSpot, created: true };
    },
    status() {},
    peers() { return []; },
    subjects() { return []; },
    createRouteBridge() {
      calls.push('spot:createRouteBridge');
      return fakeSpotRouteBridge(calls);
    },
    entrySpot() {
      return {
        setRoutingId(routingId) {
          calls.push(`entrySpot:setRoutingId:${routingId}`);
        },
        setDispatchHandler() {},
        recvActorJoin() { return null; },
        replyActorJoin() { return { message() { return this; }, submit() {} }; }
      };
    },
    createActor() {},
    actorLookup() {},
    joinActor() { return true; },
    joinActorEntrySpot() { return true; },
    async destroyActor() {},
    sendActorBoundSession() { return true; },
    async closeActorBoundSession() {},
    async dispose() {
      calls.push('spot:dispose');
    }
  };
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({
      spotNodes: {
        'game.spot': {
          router: { bind: 'tcp://0.0.0.0:9110', routingId: 'game-node' },
          meshChannels: { 'game.spot': {} }
        }
      },
      streamNodes: {
        'client.stream': {
          bind: 'tcp://0.0.0.0:9100',
          session: ClientHeaderSession
        }
      }
    })
  }, {
    backendAdapterFactory: {
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
          createDiscovery(_context, autoConnectType, channelName) {
            calls.push(`discovery:create:${channelName}:${autoConnectType}`);
            return {
              nativeInstance: {},
              channelName,
              autoConnectType,
              connectRegistry() {},
              async dispose() {
                calls.push(`discovery:dispose:${channelName}`);
              }
            };
          },
          createRouterSocket() {
            calls.push('route:createRouter');
            return {
              nativeInstance: {},
              setChannelName(channelName) { calls.push(`route:setChannelName:${channelName}`); },
              setRoutingId(routingId) { calls.push(`route:setRoutingId:${routingId}`); },
              bind(endpoint) { calls.push(`route:bind:${endpoint}`); },
              connect() {},
              disconnect() {},
              attachDiscovery() {},
              recv() { return null; },
              reply() { return { message() { return this; }, submit() {} }; },
              async dispose() {
                calls.push('route:dispose');
              }
            };
          }
        };
      },
      createMeshAdapter() {

        return { createMeshNode() { return exposeLegacyTestSpotAsMeshNode(spotNode); } };

      },
      createSpotAdapter() {
        return {
          createSpotNode(_context, mode) {
            calls.push(`spot:create:${mode}`);
            return spotNode;
          }
        };
      },
      createStreamAdapter() {
        return {
          createStreamSocket() {
            return {
              nativeInstance: {},
              bind(endpoint) {
                calls.push(`stream:bind:${endpoint}`);
              },
              setChannelName() {},
              onFramedPacket() {},
              send() { return true; },
              disconnectPeer() {},
              attachSessionRelay(node) {
                assert.equal(node, spotNode);
              },
              async bindActor() {},
              async unbindActor() {},
              sendBoundActor() { return true; },
              async dispose() {
                calls.push('stream:dispose');
              }
            };
          }
        };
      },
      createMonitoringAdapter() {
        return {
          openSocketMonitor() {
            return {
              nativeInstance: {},
              onEvent() {},
              recv() { return {}; },
              async dispose() {
                calls.push('monitor:dispose');
              }
            };
          }
        };
      }
    }
  });

  await runtime.start();
  await runtime.stop();

  assert.deepEqual(calls, [
    'spot:setRouterBind:tcp://0.0.0.0:9110',
    'stream:bind:tcp://0.0.0.0:9100',
    'monitor:dispose',
    'stream:dispose',
    'spot:dispose',
    'context:dispose'
  ]);
});

test('framework runtime host applies formal MeshNode router and peer options', async () => {
  const calls = [];
  const spotNode = {
    nativeInstance: {},
    routingId: 'node-a',
    setRoutingId(routingId) { calls.push(`spot:setRoutingId:${routingId}`); },
    setPublisherRoutingId(routingId) { calls.push(`spot:setPublisherRoutingId:${routingId}`); },
    setSubscriberRoutingId(routingId) { calls.push(`spot:setSubscriberRoutingId:${routingId}`); },
    setRouterBind(endpoint) { calls.push(`spot:setRouterBind:${endpoint}`); },
    setPubBind(endpoint) { calls.push(`spot:setPubBind:${endpoint}`); },
    attachDiscovery() {},
    connectPeer(endpoint) { calls.push(`spot:connectPeer:${endpoint}`); },
    connectPeerRid(peerRid, endpoint) { calls.push(`spot:connectPeerRid:${peerRid}:${endpoint}`); },
    disconnectPeer() {},
    createPublisher() { return { close() {} }; },
    createSpot() {
      calls.push('spot:createPublisherSpot');
      return {
        nativeInstance: {},
        routingId: 'publisher',
        setRoutingId() {},
        setSubscription() {},
        subscribe() { return true; },
        recvRoute() { return true; },
        onDispatchEvent() {},
        onSendReady() {},
        requestToChannel() { return true; },
        sendToChannel() { return true; },
        publish(topic, parts) {
          calls.push(`publisherSpot:publish:${topic}:${JSON.parse(parts[0].toString()).messageName}:${JSON.parse(parts[1].toString()).value}`);
          return true;
        },
        sendToSpot() { return true; },
        requestToSpot() { return true; },
        recvActorJoin() {},
        replyActorJoin() {},
        async dispose() {
          calls.push('publisherSpot:dispose');
        }
      };
    },
    getOrCreateSpot() {},
    status() {},
    peers() { return []; },
    subjects() { return []; },
    createRouteBridge() {
      calls.push('spot:createRouteBridge');
      return fakeSpotRouteBridge(calls);
    },
    entrySpot() {
      return {
        setRoutingId(routingId) {
          calls.push(`entrySpot:setRoutingId:${routingId}`);
        },
        setDispatchHandler() {},
        recvActorJoin() { return null; },
        replyActorJoin() { return { message() { return this; }, submit() {} }; }
      };
    },
    createActor() {},
    actorLookup() {},
    joinActor() { return true; },
    joinActorEntrySpot() { return true; },
    async destroyActor() {},
    sendActorBoundSession() { return true; },
    async closeActorBoundSession() {},
    async dispose() {
      calls.push('spot:dispose');
    }
  };
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({
      spotNodes: {
        game: {
          router: {
            bind: 'tcp://0.0.0.0:9301',
            routingId: 'node-a',
            manualConnections: ['tcp://127.0.0.1:9302'],
            manualPeerConnections: [{ peerRid: 'node-b', endpoint: 'tcp://127.0.0.1:9309' }]
          },
          meshChannels: { game: {} },
        }
      }
    })
  }, {
    backendAdapterFactory: {
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
            calls.push('dealer:create');
            return {
              nativeInstance: {},
              bind() {},
              setChannelName(channelName) { calls.push(`dealer:setChannelName:${channelName}`); },
              connect(endpoint) { calls.push(`dealer:connect:${endpoint}`); },
              disconnect() {},
              attachDiscovery() {},
              onSendReady() {},
              send() { return true; },
              request() { return true; },
              recv() {},
              async dispose() {
                calls.push('dealer:dispose');
              }
            };
          }
        };
      },
	      createMeshAdapter() {

	        return { createMeshNode() { return exposeLegacyTestSpotAsMeshNode(spotNode); } };

	      },
	      createSpotAdapter() {
	        return {
	          createSpotNode(_context, mode) {
	            calls.push(`spot:create:${mode}`);
	            return spotNode;
	          }
	        };
	      },
	      createMonitoringAdapter() {
	        return createNoopMonitoringAdapter();
	      }
	    }
	  });

  await runtime.start();
  await new Promise((resolve) => setImmediate(resolve));
  await runtime.stop();

  assert.deepEqual(calls, [
    'spot:setRouterBind:tcp://0.0.0.0:9301',
    'spot:connectPeer:tcp://127.0.0.1:9302',
    'spot:connectPeerRid:node-b:tcp://127.0.0.1:9309',
    'spot:dispose',
    'context:dispose'
  ]);
});

test('framework runtime host lets the formal MeshNode own its accepted route channel', async () => {
  const calls = [];
  const spotNode = {
    nativeInstance: {},
    routingId: 'room-node',
    setRoutingId(routingId) { calls.push(`spot:setRoutingId:${routingId}`); },
    setPublisherRoutingId(routingId) { calls.push(`spot:setPublisherRoutingId:${routingId}`); },
    setSubscriberRoutingId(routingId) { calls.push(`spot:setSubscriberRoutingId:${routingId}`); },
    setRouterBind(endpoint) { calls.push(`spot:setRouterBind:${endpoint}`); },
    setPubBind() {},
    attachDiscovery() {},
    connectPeer() {},
    disconnectPeer() {},
    createPublisher() { return { close() {} }; },
    createSpot() {
      return {
        onSendReady() {},
        async dispose() {
          calls.push('publisher:dispose');
        }
      };
    },
    getOrCreateSpot(spotId) {
      calls.push(`spot:getOrCreateSpot:${spotId}`);
      return { spot: routeSourceSpot, created: true };
    },
    status() {},
    peers() { return []; },
    subjects() { return []; },
    entrySpot() {
      return {
        setRoutingId(routingId) { calls.push(`entrySpot:setRoutingId:${routingId}`); },
        setDispatchHandler() {},
        recvActorJoin() { return null; },
        replyActorJoin() { return { message() { return this; }, submit() {} }; }
      };
    },
    createActor() {},
    actorLookup() {},
    joinActor() { return true; },
    joinActorEntrySpot() { return true; },
    async destroyActor() {},
    sendActorBoundSession() { return true; },
    async closeActorBoundSession() {},
    createRouteBridge() {
      calls.push('spot:createRouteBridge');
      return {
        ...fakeSpotRouteBridge(calls),
        request(channelName, spotId) {
          calls.push(`bridge:request:${channelName}:${spotId}`);
          return {
            message(part) {
              calls.push(`bridge:message:${part.value}`);
              return this;
            },
            timeout(timeoutMs) {
              calls.push(`bridge:timeout:${timeoutMs}`);
              return this;
            },
            submit(callback) {
              callback(0, [reply]);
              return true;
            }
          };
        }
      };
    },
    async dispose() {
      calls.push('spot:dispose');
    }
  };
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: await resolveFrameworkRegistration(nestjs.ZLinkModule.forRoot((() => {
      const builder = nestjs.zlinkFramework();
      const mesh = builder.addRouteMesh('room')
        .listen('tcp://0.0.0.0:9411')
        .routingId('room-node');
      mesh.channelName('room.route');
      mesh.peerConnections().connect('tcp://127.0.0.1:9410');
      return builder.build();
    })()))
  }, {
    backendAdapterFactory: {
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
          createDiscovery(_context, autoConnectType, channelName) {
            calls.push(`discovery:create:${channelName}:${autoConnectType}`);
            return {
              nativeInstance: {},
              channelName,
              autoConnectType,
              connectRegistry() {},
              async dispose() {
                calls.push(`discovery:dispose:${channelName}`);
              }
            };
          },
          createRouterSocket() {
            calls.push('route:createRouter');
            return {
              nativeInstance: {},
              setChannelName(channelName) { calls.push(`route:setChannelName:${channelName}`); },
              setRoutingId(routingId) { calls.push(`route:setRoutingId:${routingId}`); },
              bind(endpoint) { calls.push(`route:bind:${endpoint}`); },
              connect(endpoint) { calls.push(`route:connect:${endpoint}`); },
              disconnect() {},
              attachDiscovery() {},
              recv() {
                if (!calls.includes('route:recv')) {
                  calls.push('route:recv');
                }
                return undefined;
              },
              reply() { return { message() { return this; }, submit() {} }; },
              async dispose() {
                calls.push('route:dispose');
              }
            };
          }
        };
      },
	      createMeshAdapter() {

	        return { createMeshNode() { return exposeLegacyTestSpotAsMeshNode(spotNode); } };

	      },
	      createSpotAdapter() {
	        return {
	          createSpotNode(_context, mode) {
	            calls.push(`spot:create:${mode}`);
	            return spotNode;
	          }
	        };
	      },
	      createMonitoringAdapter() {
	        return createNoopMonitoringAdapter();
	      }
	    }
	  });

  await runtime.start();
  await runtime.stop();

  assert.deepEqual(calls, [
    'spot:setRouterBind:tcp://0.0.0.0:9411',
    'spot:dispose',
    'context:dispose'
  ]);
});

test('framework runtime host drains accepted Spot route channel without route router bind', async () => {
  const calls = [];
  const spotNode = {
    nativeInstance: {},
    routingId: 'session-node',
    setRoutingId(routingId) { calls.push(`spot:setRoutingId:${routingId}`); },
    setRouterBind(endpoint) { calls.push(`spot:setRouterBind:${endpoint}`); },
    setPubBind() {},
    attachDiscovery() {},
    connectPeer() {},
    disconnectPeer() {},
    createPublisher() { return { close() {} }; },
    createSpot() {},
    getOrCreateSpot() {},
    status() {},
    peers() { return []; },
    subjects() { return []; },
    entrySpot() {
      return {
        setRoutingId(routingId) { calls.push(`entrySpot:setRoutingId:${routingId}`); },
        setDispatchHandler() {},
        recvActorJoin() { return null; },
        replyActorJoin() { return { message() { return this; }, submit() {} }; }
      };
    },
    createActor() {},
    actorLookup() {},
    joinActor() { return true; },
    joinActorEntrySpot() { return true; },
    async destroyActor() {},
    sendActorBoundSession() { return true; },
    async closeActorBoundSession() {},
    createRouteBridge() {
      calls.push('spot:createRouteBridge');
      return {
        ...fakeSpotRouteBridge(calls),
        request(channelName, spotId) {
          calls.push(`bridge:request:${channelName}:${spotId}`);
          return {
            message(part) {
              calls.push(`bridge:message:${part.value}`);
              return this;
            },
            timeout(timeoutMs) {
              calls.push(`bridge:timeout:${timeoutMs}`);
              return this;
            },
            submit(callback) {
              callback(0, [reply]);
              return true;
            }
          };
        }
      };
    },
    async dispose() {
      calls.push('spot:dispose');
    }
  };
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: await resolveFrameworkRegistration(nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addRouteMesh('session')
        .listen('tcp://0.0.0.0:9412')
        .routingId('session-node')
        .channelName('room.route')
      .build()))
  }, {
    backendAdapterFactory: {
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
          createDiscovery(_context, autoConnectType, channelName) {
            calls.push(`discovery:create:${channelName}:${autoConnectType}`);
            return {
              nativeInstance: {},
              channelName,
              autoConnectType,
              connectRegistry() {},
              async dispose() {
                calls.push(`discovery:dispose:${channelName}`);
              }
            };
          },
          createRouterSocket() {
            calls.push('route:createRouter');
            return {
              nativeInstance: {},
              setChannelName(channelName) { calls.push(`route:setChannelName:${channelName}`); },
              setRoutingId() {},
              bind() {},
              connect() {},
              disconnect() {},
              attachDiscovery() {},
              recv() {
                if (!calls.includes('route:recv')) {
                  calls.push('route:recv');
                }
                return undefined;
              },
              reply() { return { message() { return this; }, submit() {} }; },
              async dispose() {
                calls.push('route:dispose');
              }
            };
          }
        };
      },
	      createMeshAdapter() {

	        return { createMeshNode() { return exposeLegacyTestSpotAsMeshNode(spotNode); } };

	      },
	      createSpotAdapter() {
	        return {
	          createSpotNode(_context, mode) {
	            calls.push(`spot:create:${mode}`);
	            return spotNode;
	          }
	        };
	      },
	      createMonitoringAdapter() {
	        return createNoopMonitoringAdapter();
	      }
	    }
	  });

  await runtime.start();
  await runtime.stop();

  assert.deepEqual(calls, [
    'spot:setRouterBind:tcp://0.0.0.0:9412',
    'spot:dispose',
    'context:dispose'
  ]);
});

test('framework route transport sends Spot request through accepted Spot route channel without route router bind', async () => {
  const calls = [];
  const reply = {
    close() {
      calls.push('reply:close');
    }
  };
  const entrySpot = {
    routingId: 'session-node',
    setRoutingId(routingId) { calls.push(`entry:setRoutingId:${routingId}`); },
    setDispatchHandler() {},
    recvActorJoin() { return null; },
    replyActorJoin() { return { message() { return this; }, submit() {} }; },
    requestToSpot(targetNodeRid, targetSpot, request, callback, _flags, timeoutMs) {
      calls.push(`entry:requestToSpot:${targetNodeRid}:${targetSpot}:${timeoutMs}:${request.value}`);
      callback(0, [reply]);
      return true;
    }
  };
  const routeSourceSpot = {
    setDispatchHandler(handler) {
      calls.push('routeSource:setDispatchHandler');
      handler({ event: 4 });
    },
    drainReply() {
      calls.push('routeSource:drainReply');
      return 1;
    },
    drainChannelReply(subjectHandle) {
      calls.push(`routeSource:drainChannelReply:${subjectHandle.toString()}`);
      return 1;
    },
    requestToSpot(targetNodeRid, targetSpot, request, callback, _flags, timeoutMs) {
      calls.push(`routeSource:requestToSpot:${targetNodeRid}:${targetSpot}:${timeoutMs}:${request.value}`);
      callback(0, [reply]);
      return true;
    }
  };
  const spotNode = {
    nativeInstance: {},
    routingId: 'session-node',
    setRoutingId(routingId) { calls.push(`spot:setRoutingId:${routingId}`); },
    setRouterBind(endpoint) { calls.push(`spot:setRouterBind:${endpoint}`); },
    setPubBind() {},
    attachDiscovery() {},
    connectPeer() {},
    disconnectPeer() {},
    createPublisher() { return { close() {} }; },
    createSpot() {},
    getOrCreateSpot(spotId) {
      calls.push(`spot:getOrCreateSpot:${spotId}`);
      return { spot: routeSourceSpot, created: true };
    },
    status() {},
    peers() { return []; },
    subjects() { return []; },
    entrySpot() {
      return entrySpot;
    },
    createActor() {},
    actorLookup() {},
    joinActor() { return true; },
    joinActorEntrySpot() { return true; },
    async destroyActor() {},
    sendActorBoundSession() { return true; },
    async closeActorBoundSession() {},
    createRouteBridge() {
      calls.push('spot:createRouteBridge');
      return fakeSpotRouteBridge(calls, reply);
    },
    async dispose() {
      calls.push('spot:dispose');
    }
  };
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: await resolveFrameworkRegistration(nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addRouteMesh('session')
        .listen('tcp://0.0.0.0:9413')
        .routingId('session-node')
        .channelName('room.route')
      .build()))
  }, {
    backendAdapterFactory: {
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
          createDiscovery(_context, autoConnectType, channelName) {
            calls.push(`discovery:create:${channelName}:${autoConnectType}`);
            return {
              nativeInstance: {},
              connectRegistry() {},
              async dispose() {
                calls.push(`discovery:dispose:${channelName}`);
              }
            };
          },
          createRouterSocket() {
            calls.push('route:createRouter');
            return {
              nativeInstance: {},
              setChannelName(channelName) { calls.push(`route:setChannelName:${channelName}`); },
              setRoutingId() {},
              bind() {},
              connect() {},
              disconnect() {},
              attachDiscovery() {},
              recv() { return undefined; },
              reply() { return { message() { return this; }, submit() {} }; },
              async dispose() {
                calls.push('route:dispose');
              }
            };
          }
        };
      },
	      createMeshAdapter() {

	        return { createMeshNode() { return exposeLegacyTestSpotAsMeshNode(spotNode); } };

	      },
	      createSpotAdapter() {
	        return {
	          createSpotNode(_context, mode) {
	            calls.push(`spot:create:${mode}`);
	            return spotNode;
	          }
	        };
	      },
	      createMonitoringAdapter() {
	        return createNoopMonitoringAdapter();
	      }
	    }
	  });

  await runtime.start();
  await runtime.stop();

  assert.deepEqual(calls, [
    'spot:setRouterBind:tcp://0.0.0.0:9413',
    'spot:dispose',
    'context:dispose'
  ]);
});

test('framework route transport sends Spot request through accepted Spot route channel when route channel has a bind', async () => {
  const calls = [];
  const reply = {
    close() {
      calls.push('reply:close');
    }
  };
  const entrySpot = {
    routingId: 'session-node',
    setRoutingId(routingId) { calls.push(`entry:setRoutingId:${routingId}`); },
    setDispatchHandler() {},
    recvActorJoin() { return null; },
    replyActorJoin() { return { message() { return this; }, submit() {} }; },
    requestToSpot(targetNodeRid, targetSpot, request, callback, _flags, timeoutMs) {
      calls.push(`entry:requestToSpot:${targetNodeRid}:${targetSpot}:${timeoutMs}:${request.value}`);
      callback(0, [reply]);
      return true;
    }
  };
  const routeSourceSpot = {
    setDispatchHandler(handler) {
      calls.push('routeSource:setDispatchHandler');
      handler({ event: 4 });
    },
    drainReply() {
      calls.push('routeSource:drainReply');
      return 1;
    },
    drainChannelReply(subjectHandle) {
      calls.push(`routeSource:drainChannelReply:${subjectHandle.toString()}`);
      return 1;
    },
    requestToSpot(targetNodeRid, targetSpot, request, callback, _flags, timeoutMs) {
      calls.push(`routeSource:requestToSpot:${targetNodeRid}:${targetSpot}:${timeoutMs}:${request.value}`);
      callback(0, [reply]);
      return true;
    }
  };
  const spotNode = {
    nativeInstance: {},
    routingId: 'session-node',
    setRoutingId(routingId) { calls.push(`spot:setRoutingId:${routingId}`); },
    setRouterBind(endpoint) { calls.push(`spot:setRouterBind:${endpoint}`); },
    setPubBind() {},
    attachDiscovery() {},
    connectPeer() {},
    disconnectPeer() {},
    createPublisher() { return { close() {} }; },
    createSpot() {},
    getOrCreateSpot(spotId) {
      calls.push(`spot:getOrCreateSpot:${spotId}`);
      return { spot: routeSourceSpot, created: true };
    },
    status() {},
    peers() { return []; },
    subjects() { return []; },
    entrySpot() {
      return entrySpot;
    },
    createActor() {},
    actorLookup() {},
    joinActor() { return true; },
    joinActorEntrySpot() { return true; },
    async destroyActor() {},
    sendActorBoundSession() { return true; },
    async closeActorBoundSession() {},
    createRouteBridge() {
      calls.push('spot:createRouteBridge');
      return fakeSpotRouteBridge(calls, reply);
    },
    async dispose() {
      calls.push('spot:dispose');
    }
  };
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: await resolveFrameworkRegistration(nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addRouteMesh('session')
        .listen('tcp://0.0.0.0:9415')
        .routingId('session-node')
        .channelName('room.route')
      .build()))
  }, {
    backendAdapterFactory: {
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
          createDiscovery(_context, autoConnectType, channelName) {
            calls.push(`discovery:create:${channelName}:${autoConnectType}`);
            return {
              nativeInstance: {},
              connectRegistry() {},
              async dispose() {
                calls.push(`discovery:dispose:${channelName}`);
              }
            };
          },
          createRouterSocket() {
            calls.push('route:createRouter');
            return {
              nativeInstance: {},
              setChannelName(channelName) { calls.push(`route:setChannelName:${channelName}`); },
              setRoutingId(routingId) { calls.push(`route:setRoutingId:${routingId}`); },
              bind(endpoint) { calls.push(`route:bind:${endpoint}`); },
              connect() {},
              disconnect() {},
              attachDiscovery() {},
              requestToSpot(targetNodeRid, targetSpot, request, callback, _flags, timeoutMs) {
                const raw = Array.isArray(request) ? request[0] : request;
                calls.push(`route:requestToSpot:${targetNodeRid}:${targetSpot}:${timeoutMs}:${raw.value}`);
                callback(0, [reply]);
                return true;
              },
              onSendReady() {},
              async dispose() {
                calls.push('route:dispose');
              }
            };
          }
        };
      },
	      createMeshAdapter() {

	        return { createMeshNode() { return exposeLegacyTestSpotAsMeshNode(spotNode); } };

	      },
	      createSpotAdapter() {
	        return {
	          createSpotNode(_context, mode) {
	            calls.push(`spot:create:${mode}`);
	            return spotNode;
	          }
	        };
	      },
	      createMonitoringAdapter() {
	        return createNoopMonitoringAdapter();
	      }
	    }
	  });

  await runtime.start();
  await runtime.stop();

  assert.deepEqual(calls, [
    'spot:setRouterBind:tcp://0.0.0.0:9415',
    'spot:dispose',
    'context:dispose'
  ]);
});

test('framework runtime host starts router-only SessionRelay SpotNode without Discovery', async () => {
  const calls = [];
  const spotNode = {
    nativeInstance: {},
    routingId: 'session-node',
    setRoutingId(routingId) { calls.push(`spot:setRoutingId:${routingId}`); },
    setRouterBind(endpoint) { calls.push(`spot:setRouterBind:${endpoint}`); },
    setPubBind() {},
    attachDiscovery(discovery) { calls.push(`spot:attachDiscovery:${discovery.channelName}:${discovery.autoConnectType}`); },
    connectPeer() {},
    disconnectPeer() {},
    createPublisher() { return { close() {} }; },
    createSpot() {},
    getOrCreateSpot(spotId) {
      calls.push(`spot:getOrCreateSpot:${spotId}`);
      return { spot: routeSourceSpot, created: true };
    },
    status() {},
    peers() { return []; },
    subjects() { return []; },
    entrySpot() {
      return {
        setRoutingId() {},
        setDispatchHandler() {},
        recvActorJoin() { return null; },
        replyActorJoin() { return { message() { return this; }, submit() {} }; }
      };
    },
    createActor() {},
    actorLookup() {},
    joinActor() { return true; },
    joinActorEntrySpot() { return true; },
    createSpot() {
      return {
        onSendReady() {},
        async dispose() {
          calls.push('publisher:dispose');
        }
      };
    },
    async destroyActor() {},
    sendActorBoundSession() { return true; },
    async closeActorBoundSession() {},
    async dispose() {
      calls.push('spot:dispose');
    }
  };
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({
      discovery: { registries: ['tcp://127.0.0.1:9390'] },
      spotNodes: {
        session: {
          router: {
            bind: 'tcp://0.0.0.0:9391',
            routingId: 'session-node'
          },
          meshChannels: { session: {} }
        }
      }
    })
  }, {
    backendAdapterFactory: {
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
          createDiscovery(_context, autoConnectType, channelName) {
            calls.push(`discovery:create:${channelName}:${autoConnectType}`);
            return {
              nativeInstance: {},
              channelName,
              autoConnectType,
              connectRegistry(endpoint) {
                calls.push(`discovery:connectRegistry:${endpoint}`);
              },
              async dispose() {
                calls.push(`discovery:dispose:${channelName}`);
              }
            };
          }
        };
      },
	      createMeshAdapter() {

	        return { createMeshNode() { return exposeLegacyTestSpotAsMeshNode(spotNode); } };

	      },
	      createSpotAdapter() {
	        return {
	          createSpotNode(_context, mode) {
	            calls.push(`spot:create:${mode}`);
	            return spotNode;
	          }
	        };
	      },
	      createMonitoringAdapter() {
	        return createNoopMonitoringAdapter();
	      }
	    }
	  });

  await runtime.start();
  await runtime.stop();

  assert.deepEqual(calls, [
    'spot:setRouterBind:tcp://0.0.0.0:9391',
    'spot:dispose',
    'context:dispose'
  ]);
});

test('framework runtime host starts a formal MeshNode without Discovery after bind', async () => {
  const calls = [];
  const spotNode = {
    nativeInstance: {},
    routingId: 'room-node',
    setRoutingId(routingId) { calls.push(`spot:setRoutingId:${routingId}`); },
    setPublisherRoutingId(routingId) { calls.push(`spot:setPublisherRoutingId:${routingId}`); },
    setSubscriberRoutingId(routingId) { calls.push(`spot:setSubscriberRoutingId:${routingId}`); },
    setRouterBind(endpoint) { calls.push(`spot:setRouterBind:${endpoint}`); },
    setPubBind(endpoint) { calls.push(`spot:setPubBind:${endpoint}`); },
    attachDiscovery(discovery) { calls.push(`spot:attachDiscovery:${discovery.channelName}:${discovery.autoConnectType}`); },
    connectPeer() {},
    disconnectPeer() {},
    createPublisher() { return { close() {} }; },
    createSpot() {
      return {
        onSendReady() {},
        async dispose() {
          calls.push('publisher:dispose');
        }
      };
    },
    getOrCreateSpot() {},
    status() {},
    peers() { return []; },
    subjects() { return []; },
    entrySpot() {
      return {
        setRoutingId() {},
        setDispatchHandler() {},
        recvActorJoin() { return null; },
        replyActorJoin() { return { message() { return this; }, submit() {} }; }
      };
    },
    createActor() {},
    actorLookup() {},
    joinActor() { return true; },
    joinActorEntrySpot() { return true; },
    async destroyActor() {},
    sendActorBoundSession() { return true; },
    async closeActorBoundSession() {},
    async dispose() {
      calls.push('spot:dispose');
    }
  };
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({
      discovery: { registries: ['tcp://127.0.0.1:9395'] },
      spotNodes: {
        room: {
          router: {
            bind: 'tcp://0.0.0.0:9396',
            routingId: 'room-node'
          },
          meshChannels: { room: {} }
        }
      }
    })
  }, {
    backendAdapterFactory: {
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
          createDiscovery(_context, autoConnectType, channelName) {
            calls.push(`discovery:create:${channelName}:${autoConnectType}`);
            return {
              nativeInstance: {},
              channelName,
              autoConnectType,
              connectRegistry(endpoint) {
                calls.push(`discovery:connectRegistry:${endpoint}`);
              },
              async dispose() {
                calls.push(`discovery:dispose:${channelName}`);
              }
            };
          }
        };
      },
	      createMeshAdapter() {

	        return { createMeshNode() { return exposeLegacyTestSpotAsMeshNode(spotNode); } };

	      },
	      createSpotAdapter() {
	        return {
	          createSpotNode(_context, mode) {
	            calls.push(`spot:create:${mode}`);
	            return spotNode;
	          }
	        };
	      },
	      createMonitoringAdapter() {
	        return createNoopMonitoringAdapter();
	      }
	    }
	  });

  await runtime.start();
  await runtime.stop();

  assert.deepEqual(calls, [
    'spot:setRouterBind:tcp://0.0.0.0:9396',
    'spot:dispose',
    'context:dispose'
  ]);
});

test('framework runtime host defers Entry Spot lifecycle until Core materializes it', async () => {
  const calls = [];
  let registry;
  class GenericHandler {}
  class PacketHandler {}
  class SubscribeHandler {}
  class ActorPacketHandler {}
  class SpotHandler {}
  class PlayerActor {}
  let entryContext;
  class EntrySpot {
    constructor(context) {
      this.context = context;
      entryContext = context;
    }
    configure() {
      calls.push(`entry:configure:${this.context.spotId}:${this.context.nodeRid}`);
      this.context.handlers.addHandler(GenericHandler);
      this.context.handlers.addPacket(PacketHandler, 'entry.packet');
      this.context.handlers.addSubscribe(SubscribeHandler, 'game', 'entry.topic');
      this.context.handlers.addSpotHandler(SpotHandler);
      registry = this.context.handlers;
    }
    async onInitialize() {
      calls.push('entry:onInitialize');
    }
    async onClosing() {
      calls.push('entry:onClosing');
    }
    async onLeaveActor(actor) {
      calls.push(`entry:onLeaveActor:${actor.actorId}`);
    }
  }
  const entrySpotFacade = {
    nativeInstance: {},
    routingId: 'entry-rid',
    setRoutingId() {},
    setSubscription() {},
    subscribe() { return true; },
    recvRoute() { return true; },
    onDispatchEvent() {},
    onSendReady() {},
    requestToChannel() { return true; },
    sendToChannel() { return true; },
    publish() { return true; },
    sendToSpot() { return true; },
    requestToSpot() { return true; },
    recvActorJoin() {},
    replyActorJoin() {},
    async dispose() {
      calls.push('entry:dispose');
    }
  };
  const spotNode = {
    nativeInstance: {},
    routingId: 'node-entry',
    setRoutingId() {},
    setRouterBind(endpoint) { calls.push(`spot:setRouterBind:${endpoint}`); },
    setPubBind() {},
    attachDiscovery() {},
    connectPeer() {},
    disconnectPeer() {},
    createPublisher() { return { close() {} }; },
    createSpot() { throw new Error('not used'); },
    getOrCreateSpot() {},
    status() {},
    peers() { return []; },
    subjects() { return []; },
    entrySpot() {
      calls.push('spot:entrySpot');
      return entrySpotFacade;
    },
    createActor() {},
    actorLookup() {},
    joinActor() { return true; },
    joinActorEntrySpot() { return true; },
    async destroyActor() {},
    sendActorBoundSession() { return true; },
    async closeActorBoundSession() {},
    async dispose() {
      calls.push('spot:dispose');
    }
  };
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({
      spotNodes: {
        entry: {
          router: { bind: 'tcp://0.0.0.0:9501', routingId: 'entry-node' },
          meshChannels: { entry: {} },
          entrySpotType: EntrySpot,
          entrySpotActorRequestHandlers: [{
            actorType: PlayerActor,
            entrySpotType: EntrySpot,
            handlerType: ActorPacketHandler,
            packetName: 'actor.packet'
          }]
        }
      }
    })
  }, {
    backendAdapterFactory: {
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
          }
        };
      },
	      createMeshAdapter() {

	        return { createMeshNode() { return exposeLegacyTestSpotAsMeshNode(spotNode); } };

	      },
	      createSpotAdapter() {
	        return {
	          createSpotNode() {
	            calls.push('spot:create');
	            return spotNode;
	          }
	        };
	      },
	      createMonitoringAdapter() {
	        return createNoopMonitoringAdapter();
	      }
	    }
	  });

  await runtime.start();
  await runtime.stop();

  assert.equal(registry, undefined);
  assert.deepEqual(calls, [
    'spot:setRouterBind:tcp://0.0.0.0:9501',
    'spot:dispose',
    'context:dispose'
  ]);
});

async function waitForCondition(predicate, label, timeoutMs = 3000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (predicate()) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 25));
  }
  assert.fail(`${label} timed out`);
}
