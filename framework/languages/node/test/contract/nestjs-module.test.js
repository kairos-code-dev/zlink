const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');
const { Inject, Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');

const framework = require('../../packages/framework/dist/internal');
const nestjs = require('../../packages/nestjs/dist');
const {
  providerTokens,
  reserveTcpEndpoint,
  resolveModuleProviders
} = require('./helpers/nestjs-test-utils');

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
    request(channelName, targetNodeRid, spotRid) {
      calls.push(`bridge:request:${channelName}:${targetNodeRid}:${spotRid}`);
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
  assert.equal(tokens.has(nestjs.ZLINK_RUNTIME_EVENT_PUBLISHER), true);
  assert.equal(tokens.has(nestjs.ZLINK_MESSAGE_METADATA_POLICY), true);
  assert.equal(tokens.has(nestjs.ZLINK_SPOT_MANAGER), false);
  assert.equal(tokens.has(nestjs.ZLINK_ACTOR_MANAGER), false);
});

test('ZLinkMonitoringModule.forRoot exposes runtime event publisher provider path', () => {
  const module = nestjs.ZLinkMonitoringModule.forRoot();
  const tokens = providerTokens(module);

  assert.equal(tokens.has(nestjs.ZLINK_RUNTIME_EVENT_PUBLISHER), true);
});

test('ZLinkMonitoringModule.forRoot resolves publisher from framework runtime provider', () => {
  const module = nestjs.ZLinkMonitoringModule.forRoot();
  const provider = module.providers.find((candidate) => candidate.provide === nestjs.ZLINK_RUNTIME_EVENT_PUBLISHER);
  const runtime = {
    eventPublisher: {
      register() {},
      async publish() {}
    }
  };
  const publisher = provider.useFactory({
    get(token, options) {
      assert.equal(token, nestjs.ZLINK_FRAMEWORK_RUNTIME);
      assert.deepEqual(options, { strict: false });
      return runtime;
    }
  });

  assert.equal(publisher, runtime.eventPublisher);
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
    const publisher = app.get(nestjs.ZLINK_RUNTIME_EVENT_PUBLISHER);
    await publisher.publish({
      sourceName: 'stage-node',
      timestamp: new Date(),
      event: framework.ZLinkSpotEventKind.TimerHandlerFailed,
      timerDiagnostic: {
        spotRid: 'stage-node',
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
    .options({ spotPublisherClients: ['events'] })
    .addSpotMesh('game')
      .enableRouter('tcp://127.0.0.1:0')
      .addSpotFactory(StageSpot)
      .actorFactory('player', ActorFactory)
    .build());
  const tokens = providerTokens(module);

  assert.equal(tokens.has(nestjs.ZLINK_SPOT_MANAGER), true);
  assert.equal(tokens.has(nestjs.ZLINK_SPOT_OUTBOUND), true);
  assert.equal(tokens.has(nestjs.ZLINK_SPOT_PUBLISHER_CLIENT), true);
  assert.equal(tokens.has(nestjs.ZLINK_ACTOR_MANAGER), true);
  const container = await resolveModuleProviders(module, [
    nestjs.ZLINK_ROUTE_CLIENT,
    nestjs.ZLINK_SPOT_OUTBOUND,
    nestjs.ZLINK_SPOT_PUBLISHER_CLIENT,
    nestjs.ZLINK_BOUND_SESSION_FACTORY,
    nestjs.ZLINK_ACTOR_MANAGER,
    nestjs.ZLINK_SPOT_MANAGER
  ]);
  assert.equal(container.get(nestjs.ZLINK_ACTOR_MANAGER) instanceof framework.DefaultZLinkActorManager, true);
  assert.equal(container.get(nestjs.ZLINK_SPOT_MANAGER) instanceof framework.DefaultZLinkSpotManager, true);
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
  builder.configureDispatch().setMessageFlowObserver(DispatchObserver);
  const module = nestjs.ZLinkModule.forRoot(builder
    .addSpotMesh('game')
      .enableRouter('tcp://127.0.0.1:0')
      .addSpotFactory(StageSpot)
      .actorFactory('player', ActorFactory)
    .build());
  const container = await resolveModuleProviders(module, [
    nestjs.ZLINK_FRAMEWORK_RUNTIME,
    nestjs.ZLINK_SPOT_MANAGER
  ]);

  const runtime = container.get(nestjs.ZLINK_FRAMEWORK_RUNTIME);
  const spotManager = container.get(nestjs.ZLINK_SPOT_MANAGER);

  assert.equal(runtime.isStarted, false);
  assert.equal(spotManager instanceof framework.DefaultZLinkSpotManager, true);
});

test('ZLinkModule.forRoot public DI clients expose callable framework contracts', async () => {
  const module = nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
    .useInMemoryLocationStores()
    .options({ spotPublisherClients: ['spot-events'] })
    .addRouteMeshChannel('mesh')
      .enableClient()
    .addSpotMesh('actors')
      .enableRouter('tcp://127.0.0.1:0', 'actor-node')
      .enablePubSub('tcp://127.0.0.1:0', 'actor-node')
    .build());
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

  assert.throws(
    () => routeClient.sendToNode('missing', 'node-a', new Ping(true)).submit(),
    framework.ZLinkConfigurationException
  );
  assert.throws(
    () => routeClient.sendToNode('mesh', 'node-a', new Ping(true)).submit(),
    /runtime is not started/i
  );
  assert.throws(
    () => spotPublisher.publish('missing', 'topic', new Event(true)).submit(),
    framework.ZLinkConfigurationException
  );
  assert.throws(
    () => spotPublisher.publish('spot-events', 'topic', new Event(true)).submit(),
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
  const options = builder
    .addClientServerChannel('api')
      .enableServer('tcp://127.0.0.1:7101')
      .routingId('api-a')
      .addHandlerGroup('api')
    .addClientServerChannel('play')
      .enableClient('tcp://127.0.0.1:7102')
    .addRouteMeshChannel('route')
      .enableRouter('tcp://127.0.0.1:7201')
      .routingId('node-a')
      .connect(['tcp://127.0.0.1:7202'])
      .addHandlerGroup('route-api')
    .build();

  assert.deepEqual(options.clientServerChannels.api, {
    server: { bind: 'tcp://127.0.0.1:7101', routingId: 'api-a' },
    handlerGroups: ['api']
  });
  assert.deepEqual(options.clientServerChannels.play, {
    client: { manualConnections: ['tcp://127.0.0.1:7102'] }
  });
  assert.deepEqual(options.routerMeshes.route, {
    bind: 'tcp://127.0.0.1:7201',
    routingId: 'node-a',
    manualConnections: ['tcp://127.0.0.1:7202'],
    handlerGroups: ['route-api']
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
      .addClientServerChannel('api')
        .enableServer(apiEndpoint)
        .addHandlerGroup('api')
      .build())],
    providers: [ProfileHandler]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const registration = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION);
  const handlers = registration.channels.get('api').requestHandlers;

  assert.equal(handlers.length, 1);
  assert.equal(handlers[0].packetName, 'GetProfile');
  assert.deepEqual(
    await handlers[0].handler.handle(Buffer.from(JSON.stringify({ profileId: 'p1' })), {}),
    { profileId: 'p1', source: 'provider-group' }
  );

  await app.close();
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
      .addClientServerChannel('api')
        .enableServer(apiEndpoint)
        .addHandlerGroup('api')
      .build())],
    providers: [{ provide: PROFILE_HANDLER, useClass: ProfileHandler }]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const handlers = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION).channels.get('api').requestHandlers;

  assert.equal(handlers.length, 1);
  assert.deepEqual(
    await handlers[0].handler.handle(Buffer.from(JSON.stringify({ profileId: 'p1' })), {}),
    { profileId: 'p1', source: 'custom-token' }
  );

  await app.close();
});

test('ZLinkModule.forRoot rejects a server whose only provider is an undecorated value', async () => {
  const apiEndpoint = await reserveTcpEndpoint();
  const PROFILE_HANDLER = Symbol('profile-handler');

  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addClientServerChannel('api')
        .enableServer(apiEndpoint)
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

  await assert.rejects(
    () => NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false }),
    /server must register at least one request or send handler/
  );
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
        .addClientServerChannel('api')
          .enableServer(apiEndpoint)
          .addHandlerGroup('api')
        .build()
    })],
    providers: [ProfileHandler]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const handlers = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION).channels.get('api').requestHandlers;

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
          .addClientServerChannel('api')
            .enableServer(apiEndpoint)
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
  const handlers = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION).channels.get('api').requestHandlers;

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
          .addClientServerChannel('api')
            .enableServer(apiEndpoint)
            .addHandlerGroup('api')
          .build()
      })
    ]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const handlers = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION).channels.get('api').requestHandlers;

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
      .addClientServerChannel('api')
        .enableServer(apiEndpoint)
        .addHandlerGroup('api')
      .build())],
    providers: [
      ProfileHandler,
      { provide: PROFILE_HANDLER, useExisting: ProfileHandler }
    ]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const handlers = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION).channels.get('api').requestHandlers;

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
      .addClientServerChannel('api')
        .enableServer(apiEndpoint)
        .addHandlerGroup('api')
      .build())],
    providers: [FirstProfileHandler, SecondProfileHandler]
  })(HandlerModule);

  await assert.rejects(
    () => NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false }),
    /Duplicate handler 'api:request:GetProfile'/
  );
});

test('ZLinkModule.forRoot maps grouped routeMesh send handlers from NestJS DI', async () => {
  const routeEndpoint = await reserveTcpEndpoint();
  class NoticeHandler {
    constructor() {
      this.notices = [];
    }

    async handle(message, context) {
      this.notices.push({ message, sourceNodeRid: context.sourceNodeRid });
    }
  }
  nestjs.zlinkSendHandler('route-api', 'RouteNotice')(NoticeHandler);

  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addRouteMeshChannel('route')
        .enableRouter(routeEndpoint)
        .routingId('node-a')
        .addHandlerGroup('route-api')
      .build())],
    providers: [NoticeHandler]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const routeMesh = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION).routeChannelOptions.get('route');
  const noticeHandler = app.get(NoticeHandler, { strict: false });

  assert.equal(routeMesh.sendHandlers.length, 1);
  assert.equal(routeMesh.sendHandlers[0].packetName, 'RouteNotice');
  await routeMesh.sendHandlers[0].handler.handle(
    Buffer.from(JSON.stringify({ text: 'hello' })),
    { sourceNodeRid: 'node-b', sourcePeerRid: 'peer-b', channelName: 'route', packetName: 'RouteNotice' }
  );
  assert.deepEqual(noticeHandler.notices, [{ message: { text: 'hello' }, sourceNodeRid: 'node-b' }]);

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
        .addClientServerChannel('api')
          .enableServer(endpoint)
          .addSendHandler('Notice', NoticeHandler)
        .build()
    })],
    providers: [NoticeHandler]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const channel = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION).channels.get('api');
  const noticeHandler = app.get(NoticeHandler, { strict: false });

  assert.equal(channel.sendHandlers.length, 1);
  assert.equal(channel.sendHandlers[0].packetName, 'Notice');
  await channel.sendHandlers[0].handler.handle(
    Buffer.from(JSON.stringify({ text: 'hello' })),
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

  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .options({ spotPublisherClients: ['events'] })
      .addSpotMesh('game')
        .enableRouter(spotEndpoint)
        .useDrainPolicy('ReleaseAndRecreate')
        .addSpotFactory(StageSpot)
        .actorFactory('player', ActorFactory)
      .addClientServerChannel('api')
        .enableServer(apiEndpoint)
        .addHandlerGroup('api')
      .build())],
    providers: [ProfileHandler]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const spotManager = app.get(nestjs.ZLINK_SPOT_MANAGER, { strict: false });
  const actorManager = app.get(nestjs.ZLINK_ACTOR_MANAGER, { strict: false });

  assert.equal(spotManager instanceof framework.DefaultZLinkSpotManager, true);
  assert.equal(actorManager instanceof framework.DefaultZLinkActorManager, true);
  assert.equal((await spotManager.create(StageSpot)).state, framework.ZLinkSpotCreateState.Created);
  assert.equal((await actorManager.getOrCreate('p1', 'player')).actorId, 'p1');
  assert.equal(app.get(nestjs.ZLINK_SPOT_PUBLISHER_CLIENT, { strict: false }) instanceof framework.DefaultZLinkSpotPublisherClient, true);

  await app.close();
});

test('ZLinkModule.forRoot with grouped handlers returns null for absent optional capability providers', async () => {
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
      .addClientServerChannel('api')
        .enableServer(apiEndpoint)
        .addHandlerGroup('api')
      .build())],
    providers: [ProfileHandler]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });

  assert.equal(app.get(nestjs.ZLINK_SPOT_MANAGER, { strict: false }), null);
  assert.equal(app.get(nestjs.ZLINK_ACTOR_MANAGER, { strict: false }), null);
  assert.equal(app.get(nestjs.ZLINK_SPOT_PUBLISHER_CLIENT, { strict: false }), null);

  await app.close();
});

test('ZLinkModule.forRoot passes registered spot factories to the spot manager', async () => {
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
  const module = nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
    .options({ spotFactories: [StageSpot] })
    .addSpotMesh('game')
      .enableRouter('tcp://127.0.0.1:0')
      .addSpotFactory(LocalStageSpot)
    .build());
  const container = await resolveModuleProviders(module, [nestjs.ZLINK_SPOT_MANAGER]);
  const spotManager = container.get(nestjs.ZLINK_SPOT_MANAGER);

  const created = await spotManager.create(StageSpot);
  const localCreated = await spotManager.create(LocalStageSpot);

  assert.equal(created.state, framework.ZLinkSpotCreateState.Created);
  assert.equal(localCreated.state, framework.ZLinkSpotCreateState.Created);
});

test('ZLinkModule.forRoot creates Spot factories through NestJS DI', async () => {
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

  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addSpotMesh('game')
        .enableRouter(spotEndpoint)
        .useDrainPolicy('ReleaseAndRecreate')
        .addSpotFactory(StageSpot)
      .build())],
    providers: [SpotDependency, StageSpot]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const spotManager = app.get(nestjs.ZLINK_SPOT_MANAGER, { strict: false });
  const created = await spotManager.create(StageSpot);
  const marker = await spotManager.executeOnSpot(StageSpot, created.spotRid, (spot) => spot.dependency.marker);

  assert.equal(marker, 'spot-di');

  await app.close();
});

test('ZLinkModule.forRoot creates Entry Spot through NestJS DI', async () => {
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

  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addSpotMesh('game')
        .enableRouter(spotEndpoint)
        .addEntrySpot(StageEntrySpot)
      .build())],
    providers: [EntryDependency, StageEntrySpot]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });

  assert.equal(app.get(EntryDependency, { strict: false }).initialized, true);

  await app.close();
});

test('ZLinkModule.forRoot creates Actor factories through NestJS DI', async () => {
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

  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addSpotMesh('game')
        .enableRouter(spotEndpoint)
        .actorFactory('player', PlayerActorFactory)
      .build())],
    providers: [ActorDependency, PlayerActorFactory]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const actorManager = app.get(nestjs.ZLINK_ACTOR_MANAGER, { strict: false });
  const actorRef = await actorManager.getOrCreate('p1', 'player');
  const actor = await actorManager.getOrCreateActor('p1', 'player');

  assert.equal(actorRef.actorId, 'p1');
  assert.equal(actor.marker, 'actor-di');

  await app.close();
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
    topic: 'entry.topic'
  })(EntrySubscriptionHandler);
  nestjs.zlinkSpotPacketHandler({
    packetName: 'room.packet',
    spot: () => RoomSpot
  })(RoomPacketHandler);
  nestjs.zlinkSpotSubscriptionHandler({
    spot: () => RoomSpot,
    topic: 'room.topic'
  })(RoomSubscriptionHandler);

  class TestModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addSpotMesh('game')
        .enableRouter(spotEndpoint)
        .enablePubSub(spotPubSubEndpoint)
        .addEntrySpot(EntrySpot)
        .addSpotFactory(RoomSpot)
      .build())],
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

  class TestModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addSpotMesh('game')
        .enableRouter('tcp://127.0.0.1:0')
        .addSpotFactory(RoomSpot)
      .build())],
    providers: [FirstHandler, SecondHandler]
  })(TestModule);

  await assert.rejects(
    () => NestFactory.createApplicationContext(TestModule, { logger: false, abortOnError: false }),
    /Duplicate SPOT actor handler/
  );
});

test('ZLinkModule.forRoot validates multiple actor-capable spot nodes at registration time', async () => {
  class ActorFactory {}

  const module = nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addSpotMesh('alpha')
        .actorFactory('player', ActorFactory)
      .addSpotMesh('beta')
        .actorFactory('mage', ActorFactory)
      .build());

  await assert.rejects(
    () => resolveFrameworkRegistration(module),
    framework.ZLinkConfigurationException
  );
});

test('ZLinkModule.forRoot validates channel capability endpoints and peer acquisition', () => {
  assert.throws(
    () => nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addClientServerChannel('api')
        .enableServer(undefined)
      .build()),
    /server must define a bind endpoint/
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
      .addClientServerChannel('api')
        .enableClient()
      .build()),
    /client requires location stores or manual connections/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addFanoutChannel('events')
        .enableSubscriber()
      .build()),
    /subscriber requires location stores or manual connections/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addClientServerChannel('api')
        .enableClient('')
      .build()),
    /manual connection endpoint must not be empty/
  );

  assert.doesNotThrow(() => nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
    .useInMemoryLocationStores()
    .addClientServerChannel('api')
      .enableClient()
    .addFanoutChannel('events')
      .enableSubscriber()
    .build()));
  assert.doesNotThrow(() => nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
    .addClientServerChannel('api')
      .enableClient('tcp://127.0.0.1:7001')
    .addFanoutChannel('events')
      .enableSubscriber('tcp://127.0.0.1:7002')
    .build()));
  assert.throws(
    () => nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addRouteMeshChannel('route')
      .build()),
    /must enable server or client capability/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addRouteMeshChannel('route')
        .connect(undefined)
      .build()),
    /requires location stores or manual connections/
  );
  assert.doesNotThrow(() => nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
    .useInMemoryLocationStores()
    .addRouteMeshChannel('route')
      .connect(undefined)
    .build()));
});

test('ZLinkModule.forRoot maps route mesh channel options into runtime registration', async () => {
  const module = nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
    .addRouteMeshChannel('route')
      .enableRouter('tcp://0.0.0.0:7012')
      .routingId('node-a')
      .connect('tcp://127.0.0.1:7013')
    .build());
  const registration = await resolveFrameworkRegistration(module);

  assert.equal(registration.routeChannels.has('route'), true);
  assert.equal(registration.routeChannelOptions.get('route').bind, 'tcp://0.0.0.0:7012');
  assert.deepEqual(registration.routeChannelOptions.get('route').manualConnections, ['tcp://127.0.0.1:7013']);
});

test('zlinkFramework preserves actor transfer adapters in the runtime registration', async () => {
  class PlayerActor {}
  class PlayerActorTransferAdapter {}
  const builder = nestjs.zlinkFramework();
  builder.addActorTransferAdapter(PlayerActor, PlayerActorTransferAdapter);
  const registration = await resolveFrameworkRegistration(
    nestjs.ZLinkModule.forRoot(builder.build())
  );
  assert.equal(registration.actorTransferAdapters.get(PlayerActor), PlayerActorTransferAdapter);
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

  const entryNode = nestjs.zlinkFramework().addSpotMesh('entry');
  entryNode.addEntrySpot(EntrySpot);
  assert.throws(() => entryNode.addEntrySpot(EntrySpot), /Duplicate Entry Spot registration/);

  const factoryNode = nestjs.zlinkFramework().addSpotMesh('factory');
  factoryNode.addSpotFactory(Spot);
  assert.throws(() => factoryNode.addSpotFactory(Spot), /Duplicate SPOT factory registration/);

  const actorNode = nestjs.zlinkFramework().addSpotMesh('actor');
  actorNode.actorFactory('player', ActorFactory);
  assert.throws(() => actorNode.actorFactory('player', ActorFactory), /Duplicate actor factory 'player'/);
  assert.throws(() => actorNode.actorFactory(' player ', ActorFactory), /must not be empty or padded/);
});

test('framework options builder maps dotnet-shaped registration flow into options', () => {
  class GatewaySession {}
  class StageSpot {
    constructor(context) {
      this.context = context;
    }
  }
  class LocalStageSpot {}
  class StageEntrySpot {}
  class StageActor {}
  class StageActorTransferAdapter {}
  const streamCompressionCodec = {
    compress(payload) {
      return payload;
    },
    decompress(payload) {
      return payload;
    }
  };

  const options = framework.createFrameworkOptions((builder) => {
    builder.useInMemoryLocationStores();
    builder.addActorTransferAdapter(StageActor, StageActorTransferAdapter);
    builder.configureStreamCompression().use(streamCompressionCodec);
    const api = builder.addClientServerChannel('api');
    api.enableServer('tcp://0.0.0.0:9401');
    api.enableClient('tcp://127.0.0.1:9401');
    const events = builder.addFanoutChannel('events');
    events.enablePublisher('tcp://0.0.0.0:9402');
    events.enableSubscriber('tcp://127.0.0.1:9402');
    const route = builder.addRouteMeshChannel('route');
    route.enableServer('tcp://0.0.0.0:9403');
    route.enableClient('tcp://127.0.0.1:9403');
    builder.addStreamNode('gateway')
      .bind('tcp://0.0.0.0:9404')
      .registerSession(GatewaySession);
    const spot = builder.addSpotMesh('game.stage');
    spot.addSpotFactory(StageSpot)
      .addSpotFactory(LocalStageSpot)
      .addEntrySpot(StageEntrySpot)
      .configureEntrySpot({ routingId: 'entry-stage' });
    spot.enableRouter('tcp://0.0.0.0:9405', 'stage-node');
    spot.enablePubSub('tcp://0.0.0.0:9407', 'stage-node');
  });

  const registration = framework.createFrameworkRegistration(options);
  const spotNode = registration.spotNodes.get('game.stage');
  const streamNode = registration.streamNodes.get('gateway');
  const route = registration.routeChannelOptions.get('route');

  assert.equal(registration.locations.useInMemoryStores, true);
  assert.equal(registration.actorTransferAdapters.get(StageActor), StageActorTransferAdapter);
  assert.equal(registration.channels.get('api').server.bind, 'tcp://0.0.0.0:9401');
  assert.deepEqual(registration.channels.get('api').client.manualConnections, ['tcp://127.0.0.1:9401']);
  assert.equal(registration.channels.get('events').publisher.bind, 'tcp://0.0.0.0:9402');
  assert.deepEqual(registration.channels.get('events').subscriber.manualConnections, ['tcp://127.0.0.1:9402']);
  assert.equal(route.bind, 'tcp://0.0.0.0:9403');
  assert.deepEqual(route.manualConnections, ['tcp://127.0.0.1:9403']);
  assert.equal(streamNode.bind, 'tcp://0.0.0.0:9404');
  assert.equal(streamNode.session, GatewaySession);
  assert.equal(registration.streamCompression.codec, streamCompressionCodec);
  assert.equal(registration.streamCompression.disabled, false);
  assert.equal(registration.spotFactories.has(StageSpot), true);
  assert.equal(registration.spotFactories.has(LocalStageSpot), true);
  assert.equal(spotNode.entrySpotType, StageEntrySpot);
  assert.deepEqual(spotNode.entrySpot, { routingId: 'entry-stage' });
  assert.deepEqual(spotNode.spotFactories, [StageSpot, LocalStageSpot]);
  assert.equal(spotNode.router.bind, 'tcp://0.0.0.0:9405');
  assert.deepEqual(spotNode.router.manualConnections, undefined);
  assert.equal(spotNode.pubSub.bind, 'tcp://0.0.0.0:9407');
  assert.deepEqual(spotNode.pubSub.manualConnections, undefined);
  assert.equal(registration.spotPublisherClients.has('game.stage'), true);

  assert.throws(
    () => framework.createFrameworkOptions((builder) => builder.addSpotMesh('')),
    /SPOT mesh channel name must not be empty/
  );
  assert.throws(
    () => framework.createFrameworkOptions((builder) => {
      builder.addSpotMesh('game.stage');
      builder.addSpotMesh('game.stage');
    }),
    /Duplicate SPOT mesh channel 'game.stage'/
  );
  assert.throws(
    () => framework.createFrameworkOptions((builder) => {
      builder.addClientServerChannel('api');
      builder.addClientServerChannel('api');
    }),
    /Duplicate channel 'api'/
  );
  assert.throws(
    () => framework.createFrameworkOptions((builder) => {
      builder.addClientServerChannel('mixed');
      builder.addFanoutChannel('mixed');
    }),
    /Duplicate channel 'mixed'/
  );
  assert.throws(
    () => {
      const builder = nestjs.zlinkFramework();
      builder.addFanoutChannel('events');
      builder.addRouteMeshChannel('events');
    },
    /Duplicate channel 'events'/
  );
  assert.throws(
    () => framework.createFrameworkOptions((builder) => {
      const node = builder.addSpotMesh('game.stage');
      node.addEntrySpot(StageEntrySpot);
      node.addEntrySpot(StageEntrySpot);
    }),
    /Duplicate Entry Spot registration/
  );
  assert.throws(
    () => framework.createFrameworkOptions((builder) => {
      const node = builder.addSpotMesh('game.stage');
      node.addSpotFactory(StageSpot);
      node.addSpotFactory(StageSpot);
    }),
    /Duplicate SPOT factory registration/
  );
  assert.throws(
    () => framework.createFrameworkOptions((builder) => {
      builder.addActorTransferAdapter(StageActor, StageActorTransferAdapter);
      builder.addActorTransferAdapter(StageActor, StageActorTransferAdapter);
    }),
    /Duplicate actor transfer adapter/
  );
});

test('ZLinkModule.forRoot maps stream node options into runtime registration', async () => {
  class ClientHeaderSession {
    constructor(context) {
      this.context = context;
    }
  }
  const module = nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
    .addSpotMesh('game.spot')
      .enableRouter('tcp://0.0.0.0:9110')
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

  const module = nestjs.ZLinkModule.forRoot(
    nestjs.zlinkFramework()
      .addClientServerChannel('api')
        .enableServer('tcp://0.0.0.0:9113')
      .addFanoutChannel('game.events')
        .enablePublisher('tcp://0.0.0.0:9114')
      .addRouteMeshChannel('route')
        .enableRouter('tcp://0.0.0.0:9115')
      .addStreamNode('client.stream')
        .bind('tcp://0.0.0.0:9100')
        .registerSession(ClientHeaderSession)
      .addSpotMesh('game.spot')
        .enableRouter('tcp://0.0.0.0:9110', 'game-node')
        .enablePubSub('tcp://0.0.0.0:9111', 'game-node', ['tcp://127.0.0.1:9112'])
        .configureEntrySpot({ routingId: 'entry-node' })
        .addEntrySpot(StageEntrySpot)
        .actorFactory('player', PlayerActorFactory)
      .build()
  );
  const registration = await resolveFrameworkRegistration(module);
  const streamNode = registration.streamNodes.get('client.stream');
  const spotNode = registration.spotNodes.get('game.spot');

  assert.equal(streamNode.bind, 'tcp://0.0.0.0:9100');
  assert.equal(streamNode.session, ClientHeaderSession);
  assert.equal(registration.actorFactories.get('player'), PlayerActorFactory);
  assert.equal(spotNode.actorFactories.player, PlayerActorFactory);
  assert.equal(spotNode.router.bind, 'tcp://0.0.0.0:9110');
  assert.equal(spotNode.router.routingId, 'game-node');
  assert.equal(spotNode.pubSub.bind, 'tcp://0.0.0.0:9111');
  assert.deepEqual(spotNode.pubSub.manualConnections, ['tcp://127.0.0.1:9112']);
  assert.equal(registration.spotPublisherClients.has('game.spot'), true);
  assert.deepEqual(spotNode.entrySpot, { routingId: 'entry-node' });
  assert.equal(spotNode.entrySpotType, StageEntrySpot);
});

test('ZLinkModule.forRoot validates and maps SpotNode router and pubSub capability options', async () => {
  const module = nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
    .addSpotMesh('game')
      .enableRouter('tcp://0.0.0.0:9201', 'node-a', 'tcp://127.0.0.1:9202')
      .enablePubSub('tcp://0.0.0.0:9203', 'node-a', 'tcp://127.0.0.1:9204')
    .addClientServerChannel('api')
      .enableServer('tcp://0.0.0.0:9208')
    .addRouteMeshChannel('route')
      .enableRouter('tcp://0.0.0.0:9209')
    .build());
  const registration = await resolveFrameworkRegistration(module);
  const spotNode = registration.spotNodes.get('game');

  assert.equal(spotNode.router.bind, 'tcp://0.0.0.0:9201');
  assert.equal(spotNode.router.routingId, 'node-a');
  assert.deepEqual(spotNode.router.manualConnections, ['tcp://127.0.0.1:9202']);
  assert.equal(spotNode.pubSub.bind, 'tcp://0.0.0.0:9203');
  assert.deepEqual(spotNode.pubSub.manualConnections, ['tcp://127.0.0.1:9204']);
  assert.equal(registration.spotPublisherClients.has('game'), true);

  const orderedModule = nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
    .addSpotMesh('ordered')
      .connectRouter('tcp://127.0.0.1:9211')
      .connectRouter('node-b', 'tcp://127.0.0.1:9212')
      .connectPeerPub('tcp://127.0.0.1:9213')
      .enableRouter('tcp://0.0.0.0:9214', 'node-a')
      .enablePubSub('tcp://0.0.0.0:9215', 'node-a')
    .build());
  const orderedRegistration = await resolveFrameworkRegistration(orderedModule);
  const orderedSpotNode = orderedRegistration.spotNodes.get('ordered');
  assert.deepEqual(orderedSpotNode.router.manualConnections, ['tcp://127.0.0.1:9211']);
  assert.deepEqual(orderedSpotNode.router.manualPeerConnections, [{ peerRid: 'node-b', endpoint: 'tcp://127.0.0.1:9212' }]);
  assert.deepEqual(orderedSpotNode.pubSub.manualConnections, ['tcp://127.0.0.1:9213']);

  await assert.rejects(
    async () => resolveFrameworkRegistration(nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addSpotMesh('')
      .build())),
    /SpotNode name must not be empty/
  );
  await assert.rejects(
    async () => resolveFrameworkRegistration(nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addSpotMesh('game')
        .enableRouter('')
      .build())),
    /SpotNode 'game' router must define a bind endpoint/
  );
  await assert.rejects(
    async () => resolveFrameworkRegistration(nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addSpotMesh('game')
        .enablePubSub(undefined, undefined, '')
      .build())),
    /SpotNode 'game' pubSub manual connection endpoint must not be empty/
  );
  await assert.rejects(
    async () => resolveFrameworkRegistration(nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addSpotMesh('game')
        .enableRouter('tcp://127.0.0.1:9218', 'node-a')
        .enablePubSub('tcp://127.0.0.1:9219', 'node-b')
      .build())),
    /router and pubSub routingId must match/
  );
  await assert.rejects(
    async () => resolveFrameworkRegistration(nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addSpotMesh('game')
        .enableRouter('tcp://127.0.0.1:9220')
        .connectRouter('node-a', 'tcp://127.0.0.1:9216')
        .connectRouter('node-a', 'tcp://127.0.0.1:9217')
      .build())),
    /manual peer routing id must be unique/
  );
});

test('ZLinkModule.forRoot derives Spot publisher clients from SpotMesh pubSub capability', async () => {
  const registration = await resolveFrameworkRegistration(nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
    .addSpotMesh('game')
      .enablePubSub('tcp://0.0.0.0:9210')
    .build()));

  assert.equal(registration.spotPublisherClients.has('game'), true);
});

test('ZLinkModule.forRoot maps one location store into runtime registration', async () => {
  const store = new framework.ZLinkInMemoryLocationStore();
  const registration = await resolveFrameworkRegistration(nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
    .addLocationStore(store)
    .build()));

  assert.equal(registration.locations.storeInstance, store);
});

test('ZLinkModule.forRoot exposes SpotHandle resolvers from location stores', async () => {
  const store = new framework.ZLinkInMemoryLocationStore();
  const module = nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
    .addLocationStore(store)
    .build());
  const tokens = providerTokens(module);

  assert.equal(tokens.has(nestjs.ZLINK_SPOT_HANDLE_RESOLVER), true);
  assert.equal(tokens.has(nestjs.ZLINK_ACTOR_SPOT_HANDLE_RESOLVER), true);
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
      return nestjs.zlinkFramework()
        .options({ spotPublisherClients: ['game-events'] })
        .addSpotMesh('game')
          .enableRouter(spotEndpoint)
          .useDrainPolicy('ReleaseAndRecreate')
          .addSpotFactory(AsyncSpot)
          .actorFactory('player', ActorFactory)
        .build();
    }
  });
  class AsyncModule {}
  Module({ imports: [module] })(AsyncModule);
  const app = await NestFactory.createApplicationContext(AsyncModule, { logger: false, abortOnError: false });
  const spotManager = app.get(nestjs.ZLINK_SPOT_MANAGER, { strict: false });
  const actorManager = app.get(nestjs.ZLINK_ACTOR_MANAGER, { strict: false });

  assert.equal(spotManager instanceof framework.DefaultZLinkSpotManager, true);
  assert.equal(actorManager instanceof framework.DefaultZLinkActorManager, true);
  assert.equal((await spotManager.create(AsyncSpot)).state, framework.ZLinkSpotCreateState.Created);
  assert.equal((await actorManager.getOrCreate('p1', 'player')).actorId, 'p1');
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
        .addClientServerChannel(config.channelName)
          .enableServer(config.bind)
          .addRequestHandler('ConfigReq', ConfigHandler)
        .build();
    }
  });
  class AsyncModule {}
  Module({ imports: [module] })(AsyncModule);

  const app = await NestFactory.createApplicationContext(AsyncModule, { logger: false, abortOnError: false });
  const registration = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION, { strict: false });

  assert.equal(registration.channels.get('api').server.bind, apiEndpoint);
  await app.close();
});

test('ZLinkModule.forRootFactory preserves route mesh transport options after dynamic handler merge', async () => {
  const module = nestjs.ZLinkModule.forRootFactory({
    useFactory() {
      return nestjs.zlinkFramework()
        .useInMemoryLocationStores()
        .addRouteMeshChannel('quest.route')
          .enableRouter('tcp://127.0.0.1:7111')
          .routingId('gamequest-api-a')
          .connect('tcp://127.0.0.1:7112')
        .build();
    }
  });
  const registration = await resolveFrameworkRegistration(module);
  const route = registration.routeChannelOptions.get('quest.route');

  assert.equal(route.bind, 'tcp://127.0.0.1:7111');
  assert.equal(route.routingId, 'gamequest-api-a');
  assert.deepEqual(route.manualConnections, ['tcp://127.0.0.1:7112']);
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
    createSpot() {
      return {
        onSendReady() {},
        async dispose() {
          calls.push('publisher:dispose');
        }
      };
    },
    getOrCreateSpot(spotRid) {
      calls.push(`spot:getOrCreateSpot:${spotRid}`);
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
          router: { bind: 'tcp://0.0.0.0:9110' }
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
    'spot:create:2',
    'spot:setRouterBind:tcp://0.0.0.0:9110',
    'stream:bind:tcp://0.0.0.0:9100',
    'monitor:dispose',
    'stream:dispose',
    'spot:dispose',
    'context:dispose'
  ]);
});

test('framework runtime host applies SpotNode router and pubSub capability options', async () => {
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
          pubSub: {
            bind: 'tcp://0.0.0.0:9303',
            routingId: 'node-a',
            manualConnections: ['tcp://127.0.0.1:9304']
          },
          entrySpot: { routingId: 'entry-node-a' }
        }
      },
      channels: {
        api: { server: { bind: 'tcp://0.0.0.0:9308' } }
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
  await runtime.spotPublisherTransport.publish(
    'game',
    'room.events',
    'GameEvent',
    { value: 'published' }
  );
  await runtime.stop();

  assert.deepEqual(calls, [
	    'spot:create:3',
	    'spot:setRoutingId:node-a',
	    'spot:setPublisherRoutingId:node-a',
	    'spot:setSubscriberRoutingId:node-a',
    'entrySpot:setRoutingId:entry-node-a',
    'spot:setRouterBind:tcp://0.0.0.0:9301',
    'spot:connectPeer:tcp://127.0.0.1:9302',
    'spot:setPubBind:tcp://0.0.0.0:9303',
    'spot:connectPeer:tcp://127.0.0.1:9304',
    'spot:createPublisherSpot',
    'spot:connectPeerRid:node-b:tcp://127.0.0.1:9309',
    'publisherSpot:publish:room.events:GameEvent:published',
    'publisherSpot:dispose',
    'spot:dispose',
    'context:dispose'
  ]);
});

test('framework runtime host lets route router own accepted Spot route channel frames when bound', async () => {
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
    createSpot() {
      return {
        onSendReady() {},
        async dispose() {
          calls.push('publisher:dispose');
        }
      };
    },
    getOrCreateSpot(spotRid) {
      calls.push(`spot:getOrCreateSpot:${spotRid}`);
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
        request(channelName, spotRid) {
          calls.push(`bridge:request:${channelName}:${spotRid}`);
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
      .addRouteMeshChannel('room.route')
        .enableRouter('tcp://0.0.0.0:9410')
        .connect('tcp://127.0.0.1:9410')
        .routingId('room-node')
      .addSpotMesh('room')
        .enableRouter('tcp://0.0.0.0:9411', 'room-node')
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
  await waitForCondition(() => calls.includes('route:recv'), 'Spot route channel drain');
  await runtime.stop();

  assert.deepEqual(calls.filter((call) => call !== 'route:recv'), [
    'spot:create:2',
    'spot:setRoutingId:room-node',
    'entrySpot:setRoutingId:room-node',
    'spot:setRouterBind:tcp://0.0.0.0:9411',
    'route:createRouter',
    'route:setChannelName:room.route',
    'route:setRoutingId:room-node',
    'route:connect:tcp://127.0.0.1:9410',
    'route:bind:tcp://0.0.0.0:9410',
    'spot:createRouteBridge',
    'bridge:attachRouter:room.route',
    'spot:dispose',
    'bridge:dispose',
    'route:dispose',
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
        request(channelName, spotRid) {
          calls.push(`bridge:request:${channelName}:${spotRid}`);
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
      .addRouteMeshChannel('room.route')
      .addSpotMesh('session')
        .enableRouter('tcp://0.0.0.0:9412', 'session-node')
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
  await waitForCondition(() => calls.includes('route:recv'), 'accepted Spot route channel drain without route router bind');
  await runtime.stop();

  assert.deepEqual(calls.filter((call) => call !== 'route:recv'), [
    'spot:create:2',
    'spot:setRoutingId:session-node',
    'entrySpot:setRoutingId:session-node',
    'spot:setRouterBind:tcp://0.0.0.0:9412',
    'route:createRouter',
    'route:setChannelName:room.route',
    'spot:createRouteBridge',
    'bridge:attachRouter:room.route',
    'spot:dispose',
    'bridge:dispose',
    'route:dispose',
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
    createSpot() {},
    getOrCreateSpot(spotRid) {
      calls.push(`spot:getOrCreateSpot:${spotRid}`);
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
      .addRouteMeshChannel('room.route')
      .addSpotMesh('session')
        .enableRouter('tcp://0.0.0.0:9413', 'session-node')
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
  const replies = await runtime.routeTransport.requestRawToSpot({
    routerChannelId: 'room.route',
    targetNodeRid: 'play-node',
    spotRid: 'play-node',
    spotKind: framework.ZLinkSpotKind.Entry
  }, { value: 'request', close() {} }, { timeoutMs: 123 });
  await runtime.stop();

  assert.deepEqual(replies, [reply]);
  assert.deepEqual(calls, [
    'spot:create:2',
    'spot:setRoutingId:session-node',
    'entry:setRoutingId:session-node',
    'spot:setRouterBind:tcp://0.0.0.0:9413',
    'route:createRouter',
    'route:setChannelName:room.route',
    'spot:createRouteBridge',
    'bridge:attachRouter:room.route',
    'bridge:request:room.route:play-node:play-node',
    'bridge:message:request',
    'bridge:timeout:123',
    'spot:dispose',
    'bridge:dispose',
    'route:dispose',
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
    createSpot() {},
    getOrCreateSpot(spotRid) {
      calls.push(`spot:getOrCreateSpot:${spotRid}`);
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
      .addRouteMeshChannel('room.route')
        .enableRouter('tcp://0.0.0.0:9414')
        .routingId('session-node')
      .addSpotMesh('session')
        .enableRouter('tcp://0.0.0.0:9415', 'session-node')
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
  const replies = await runtime.routeTransport.requestRawToSpot({
    routerChannelId: 'room.route',
    targetNodeRid: 'play-node',
    spotRid: 'play-node',
    spotKind: framework.ZLinkSpotKind.Entry
  }, { value: 'request', close() {} }, { timeoutMs: 123 });
  await runtime.stop();

  assert.equal(calls.some((call) => call.startsWith('route:requestToSpot:')), false, calls.join('\n'));
  assert.deepEqual(replies, [reply]);
  assert.deepEqual(calls, [
    'spot:create:2',
    'spot:setRoutingId:session-node',
    'entry:setRoutingId:session-node',
    'spot:setRouterBind:tcp://0.0.0.0:9415',
    'route:createRouter',
    'route:setChannelName:room.route',
    'route:setRoutingId:session-node',
    'route:bind:tcp://0.0.0.0:9414',
    'spot:createRouteBridge',
    'bridge:attachRouter:room.route',
    'bridge:request:room.route:play-node:play-node',
    'bridge:message:request',
    'bridge:timeout:123',
    'spot:dispose',
    'bridge:dispose',
    'route:dispose',
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
    createSpot() {},
    getOrCreateSpot(spotRid) {
      calls.push(`spot:getOrCreateSpot:${spotRid}`);
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
          }
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
    'spot:create:2',
    'spot:setRoutingId:session-node',
    'spot:setRouterBind:tcp://0.0.0.0:9391',
    'spot:dispose',
    'context:dispose'
  ]);
});

test('framework runtime host starts router and pubSub SpotNode without Discovery after binds', async () => {
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
          pubSub: {
            bind: 'tcp://0.0.0.0:9397',
            routingId: 'room-node'
          }
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
    'spot:create:3',
    'spot:setRoutingId:room-node',
    'spot:setPublisherRoutingId:room-node',
    'spot:setSubscriberRoutingId:room-node',
    'spot:setRouterBind:tcp://0.0.0.0:9396',
    'spot:setPubBind:tcp://0.0.0.0:9397',
    'publisher:dispose',
    'spot:dispose',
    'context:dispose'
  ]);
});

test('framework runtime host initializes registered Entry Spot lifecycle and handlers', async () => {
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
      calls.push(`entry:configure:${this.context.spotRid}:${this.context.nodeRid}`);
      this.context.handlers.addHandler(GenericHandler);
      this.context.handlers.addPacket(PacketHandler, 'entry.packet');
      this.context.handlers.addSubscribe(SubscribeHandler, 'entry.topic');
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
          router: { bind: 'tcp://0.0.0.0:9501' },
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
  runtime.setActorManager({
    async destroyActor(node, nodeRid, actor) {
      calls.push(`actorManager:destroy:${node.routingId}:${nodeRid}:${actor.actorId}`);
      calls.push(`actorManager:destroyed:${actor.actorId}`);
    }
  });
  await entryContext.destroyActor({ actorId: 'player-1' });
  await runtime.stop();

  assert.deepEqual(registry.snapshot(), [
    { kind: 'actorRequest', handlerType: ActorPacketHandler, actorType: PlayerActor, packetName: 'actor.packet' },
    { kind: 'handler', handlerType: GenericHandler },
    { kind: 'packet', handlerType: PacketHandler, packetName: 'entry.packet' },
    { kind: 'subscribe', handlerType: SubscribeHandler, topic: 'entry.topic' },
    { kind: 'spotHandler', handlerType: SpotHandler }
  ]);
  assert.deepEqual(calls, [
    'spot:create',
    'spot:setRouterBind:tcp://0.0.0.0:9501',
    'spot:entrySpot',
    'entry:configure:entry-rid:node-entry',
    'entry:onInitialize',
    'actorManager:destroy:node-entry:node-entry:player-1',
    'actorManager:destroyed:player-1',
    'entry:onClosing',
    'entry:dispose',
    'spot:dispose',
    'context:dispose'
  ]);
});

async function waitForCondition(predicate, label, timeoutMs = 5000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (predicate()) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 25));
  }
  assert.fail(`${label} timed out`);
}
