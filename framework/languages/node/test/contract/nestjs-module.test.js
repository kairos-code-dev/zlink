const assert = require('node:assert/strict');
const test = require('node:test');
const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');

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

test('ZLinkModule.forRoot exposes capability providers only when registration enables them', async () => {
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
  const container = await resolveModuleProviders(module, [
    nestjs.ZLINK_ROUTE_CLIENT,
    nestjs.ZLINK_SPOT_OUTBOUND,
    nestjs.ZLINK_SPOT_PUBLISHER_CLIENT,
    nestjs.ZLINK_BOUND_SESSION_FACTORY
  ]);
  assert.equal(container.get(nestjs.ZLINK_ROUTE_CLIENT) instanceof framework.DefaultZLinkRouteClient, true);
  assert.deepEqual(
    module.providers.find((provider) => provider.provide === nestjs.ZLINK_BOUND_SESSION_FACTORY).inject,
    [nestjs.ZLINK_FRAMEWORK_RUNTIME]
  );
  assert.equal(
    container.get(nestjs.ZLINK_SPOT_OUTBOUND) instanceof framework.DefaultZLinkSpotOutbound,
    true
  );
  assert.equal(container.get(nestjs.ZLINK_SPOT_PUBLISHER_CLIENT) instanceof framework.DefaultZLinkSpotPublisherClient, true);
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

test('ZLinkModule.forRoot discovers annotated request handlers from NestJS providers', async () => {
  class ProfileHandler {
    async handle(request) {
      return { profileId: request.profileId, source: 'annotation' };
    }
  }
  framework.ZLinkHandlerGroup('api')(ProfileHandler);
  framework.ZLinkRequest('GetProfile')(ProfileHandler.prototype, 'handle', descriptor());

  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot({
      channels: {
        api: {
          server: { bind: 'tcp://127.0.0.1:7955' },
          handlerGroups: ['api']
        }
      }
    })],
    providers: [ProfileHandler]
  })(HandlerModule);

  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const registration = app.get(nestjs.ZLINK_FRAMEWORK_REGISTRATION);
  const handlers = registration.channels.get('api').requestHandlers;

  assert.equal(handlers.length, 1);
  assert.equal(handlers[0].packetName, 'GetProfile');
  assert.deepEqual(
    await handlers[0].handler.handle(Buffer.from(JSON.stringify({ profileId: 'p1' })), {}),
    { profileId: 'p1', source: 'annotation' }
  );

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
  const module = nestjs.ZLinkModule.forRoot({
    spotNodes: {
      game: {
        spotFactories: [LocalStageSpot]
      }
    },
    spotFactories: [StageSpot]
  });
  const spotManager = module.providers.find((provider) => provider.provide === nestjs.ZLINK_SPOT_MANAGER).useValue;

  const created = await spotManager.create(StageSpot);
  const localCreated = await spotManager.create(LocalStageSpot);

  assert.equal(created.created, true);
  assert.equal(localCreated.created, true);
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

test('ZLinkModule.forRoot maps dealer and route mesh channel options into runtime registration', () => {
  const module = nestjs.ZLinkModule.forRoot({
    channels: {
      mesh: {
        dealerMesh: {
          client: { manualConnections: ['tcp://127.0.0.1:7011'] }
        }
      },
      route: {
        routeMesh: {
          bind: 'tcp://0.0.0.0:7012',
          routingId: 'node-a',
          manualConnections: ['tcp://127.0.0.1:7013']
        }
      }
    }
  });
  const registration = module.providers.find((provider) => provider.provide === nestjs.ZLINK_FRAMEWORK_REGISTRATION).useValue;

  assert.equal(registration.channelClients.has('mesh'), true);
  assert.equal(registration.routeChannels.has('route'), true);
  assert.equal(registration.routeChannelOptions.get('route').bind, 'tcp://0.0.0.0:7012');
  assert.deepEqual(registration.routeChannelOptions.get('route').manualConnections, ['tcp://127.0.0.1:7013']);

  assert.throws(
    () => nestjs.ZLinkModule.forRoot({
      channels: {
        mesh: {
          client: { manualConnections: ['tcp://127.0.0.1:7011'] },
          dealerMesh: { client: { manualConnections: ['tcp://127.0.0.1:7012'] } }
        }
      }
    }),
    /cannot define both client and dealerMesh/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot({
      routeChannels: ['route'],
      channels: {
        route: { routeMesh: { bind: 'tcp://0.0.0.0:7012' } }
      }
    }),
    /already registered/
  );
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

  const options = framework.createFrameworkOptions((builder) => {
    builder.useDiscovery().connectRegistry('tcp://127.0.0.1:9400');
    builder.clientServerChannel('api').server().bind('tcp://0.0.0.0:9401');
    builder.clientServerChannel('api').client().connect('tcp://127.0.0.1:9401');
    builder.fanoutChannel('events').publisher().bind('tcp://0.0.0.0:9402');
    builder.fanoutChannel('events').subscriber().connect('tcp://127.0.0.1:9402');
    builder.routeMeshChannel('route').router().bind('tcp://0.0.0.0:9403');
    builder.routeMeshChannel('route').dealer().connect('tcp://127.0.0.1:9403');
    builder.streamNode('gateway')
      .bind('tcp://0.0.0.0:9404')
      .attachActorGateway('stage-node')
      .registerSession(GatewaySession);
    const spot = builder.addSpotMesh('game.stage').node('stage-node');
    spot.addSpotFactory(StageSpot)
      .addSpotFactory(LocalStageSpot)
      .addEntrySpot(StageEntrySpot)
      .configureEntrySpot({ routingId: 'entry-stage' });
    spot.router()
      .bind('tcp://0.0.0.0:9405')
      .routingId('stage-node')
      .connect('tcp://127.0.0.1:9406');
    spot.pubSub()
      .bind('tcp://0.0.0.0:9407')
      .routingId('stage-node')
      .connect('tcp://127.0.0.1:9408');
    spot.attachChannelClient('api').connect('tcp://127.0.0.1:9401');
    spot.attachSpotPublisherClient('game.stage').connect('tcp://127.0.0.1:9407');
    spot.acceptSpotRoutesFromChannel('route').connect('tcp://127.0.0.1:9403');
  });

  const registration = framework.createFrameworkRegistration(options);
  const spotNode = registration.spotNodes.get('stage-node');
  const streamNode = registration.streamNodes.get('gateway');
  const route = registration.routeChannelOptions.get('route');

  assert.deepEqual(registration.discovery.registries, ['tcp://127.0.0.1:9400']);
  assert.equal(registration.channels.get('api').server.bind, 'tcp://0.0.0.0:9401');
  assert.deepEqual(registration.channels.get('api').client.manualConnections, ['tcp://127.0.0.1:9401']);
  assert.equal(registration.channels.get('events').publisher.bind, 'tcp://0.0.0.0:9402');
  assert.deepEqual(registration.channels.get('events').subscriber.manualConnections, ['tcp://127.0.0.1:9402']);
  assert.equal(route.bind, 'tcp://0.0.0.0:9403');
  assert.deepEqual(route.manualConnections, ['tcp://127.0.0.1:9403']);
  assert.equal(streamNode.bind, 'tcp://0.0.0.0:9404');
  assert.equal(streamNode.attachActorGateway, 'stage-node');
  assert.equal(streamNode.session, GatewaySession);
  assert.equal(registration.spotFactories.has(StageSpot), true);
  assert.equal(registration.spotFactories.has(LocalStageSpot), true);
  assert.equal(spotNode.entrySpotType, StageEntrySpot);
  assert.deepEqual(spotNode.entrySpot, { routingId: 'entry-stage' });
  assert.deepEqual(spotNode.spotFactories, [StageSpot, LocalStageSpot]);
  assert.equal(spotNode.router.bind, 'tcp://0.0.0.0:9405');
  assert.deepEqual(spotNode.router.manualConnections, ['tcp://127.0.0.1:9406']);
  assert.equal(spotNode.pubSub.bind, 'tcp://0.0.0.0:9407');
  assert.deepEqual(spotNode.pubSub.manualConnections, ['tcp://127.0.0.1:9408']);
  assert.deepEqual(spotNode.attachedChannelClients.api.manualConnections, ['tcp://127.0.0.1:9401']);
  assert.deepEqual(spotNode.attachedSpotPublisherClients['game.stage'].manualConnections, ['tcp://127.0.0.1:9407']);
  assert.deepEqual(spotNode.acceptedSpotRouteChannels.route.manualConnections, ['tcp://127.0.0.1:9403']);

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
      const node = builder.addSpotMesh('game.stage').node('stage-node');
      node.addEntrySpot(StageEntrySpot);
      node.addEntrySpot(StageEntrySpot);
    }),
    /Duplicate Entry Spot registration/
  );
  assert.throws(
    () => framework.createFrameworkOptions((builder) => {
      const node = builder.addSpotMesh('game.stage').node('stage-node');
      node.addSpotFactory(StageSpot);
      node.addSpotFactory(StageSpot);
    }),
    /Duplicate SPOT factory registration/
  );
});

test('ZLinkModule.forRoot maps stream node options into runtime registration', () => {
  class ClientHeaderSession {
    constructor(context) {
      this.context = context;
    }
  }
  const module = nestjs.ZLinkModule.forRoot({
    spotNodes: {
      'game.spot': {
        router: { bind: 'tcp://0.0.0.0:9110' }
      }
    },
    streamNodes: {
      'client.stream': {
        bind: 'tcp://0.0.0.0:9100',
        attachActorGateway: 'game.spot',
        session: ClientHeaderSession
      }
    }
  });
  const registration = module.providers.find((provider) => provider.provide === nestjs.ZLINK_FRAMEWORK_REGISTRATION).useValue;
  const streamNode = registration.streamNodes.get('client.stream');

  assert.equal(streamNode.bind, 'tcp://0.0.0.0:9100');
  assert.equal(streamNode.attachActorGateway, 'game.spot');
  assert.equal(streamNode.session, ClientHeaderSession);
  assert.equal(registration.spotNodes.get('game.spot').router.bind, 'tcp://0.0.0.0:9110');

  assert.throws(
    () => framework.createFrameworkOptions((builder) => {
      builder.streamNode('client.stream')
        .bind('tcp://0.0.0.0:9100')
        .registerSession(ClientHeaderSession)
        .registerSession(ClientHeaderSession);
    }),
    /STREAM node cannot register more than one header stream session/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot({
      streamNodes: { 'missing-bind': { session: ClientHeaderSession } }
    }),
    /STREAM node 'missing-bind' must define a bind endpoint/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot({
      streamNodes: { 'missing-session': { bind: 'tcp://0.0.0.0:9101' } }
    }),
    /STREAM node 'missing-session' must register a header stream session/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot({
      spotNodes: ['game.spot'],
      streamNodes: {
        'client.stream': {
          bind: 'tcp://0.0.0.0:9100',
          attachActorGateway: 'game.spot',
          session: ClientHeaderSession
        }
      }
    }),
    /does not enable router capability/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot({
      streamNodes: {
        'client.stream': {
          bind: 'tcp://0.0.0.0:9100',
          attachActorGateway: 'unknown.spot',
          session: ClientHeaderSession
        }
      }
    }),
    /references unknown ActorGateway target SpotNode 'unknown.spot'/
  );
});

test('ZLinkModule.forRoot validates and maps SpotNode router and pubSub capability options', () => {
  const module = nestjs.ZLinkModule.forRoot({
    spotNodes: {
      game: {
        router: {
          bind: 'tcp://0.0.0.0:9201',
          routingId: 'node-a',
          manualConnections: ['tcp://127.0.0.1:9202']
        },
        pubSub: {
          bind: 'tcp://0.0.0.0:9203',
          routingId: 'node-a',
          manualConnections: ['tcp://127.0.0.1:9204']
        },
        attachedChannelClients: {
          api: { manualConnections: ['tcp://127.0.0.1:9205'] }
        },
        attachedSpotPublisherClients: {
          'game.events': { manualConnections: ['tcp://127.0.0.1:9206'] }
        },
        acceptedSpotRouteChannels: {
          route: { manualConnections: ['tcp://127.0.0.1:9207'] }
        }
      }
    },
    channels: {
      api: { server: { bind: 'tcp://0.0.0.0:9208' } }
    },
    routeChannels: [{
      routerChannelId: 'route',
      bind: 'tcp://0.0.0.0:9209'
    }]
  });
  const registration = module.providers.find((provider) => provider.provide === nestjs.ZLINK_FRAMEWORK_REGISTRATION).useValue;
  const spotNode = registration.spotNodes.get('game');

  assert.equal(spotNode.router.bind, 'tcp://0.0.0.0:9201');
  assert.equal(spotNode.router.routingId, 'node-a');
  assert.deepEqual(spotNode.router.manualConnections, ['tcp://127.0.0.1:9202']);
  assert.equal(spotNode.pubSub.bind, 'tcp://0.0.0.0:9203');
  assert.deepEqual(spotNode.pubSub.manualConnections, ['tcp://127.0.0.1:9204']);
  assert.deepEqual(spotNode.attachedChannelClients.api.manualConnections, ['tcp://127.0.0.1:9205']);
  assert.deepEqual(spotNode.attachedSpotPublisherClients['game.events'].manualConnections, ['tcp://127.0.0.1:9206']);
  assert.deepEqual(spotNode.acceptedSpotRouteChannels.route.manualConnections, ['tcp://127.0.0.1:9207']);
  assert.equal(registration.spotPublisherClients.has('game.events'), true);

  assert.throws(
    () => nestjs.ZLinkModule.forRoot({ spotNodes: [{ name: '', router: {} }] }),
    /SpotNode name must not be empty/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot({ spotNodes: { game: { router: { bind: '' } } } }),
    /SpotNode 'game' router must define a bind endpoint/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot({ spotNodes: { game: { pubSub: { manualConnections: [''] } } } }),
    /SpotNode 'game' pubSub manual connection endpoint must not be empty/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot({
      spotNodes: {
        game: {
          router: { routingId: 'node-a' },
          pubSub: { routingId: 'node-b' }
        }
      }
    }),
    /router and pubSub routingId must match/
  );
});

test('ZLinkModule.forRoot validates SpotNode attachment targets', () => {
  assert.throws(
    () => nestjs.ZLinkModule.forRoot({
      spotNodes: {
        game: {
          attachedChannelClients: { missing: {} }
        }
      }
    }),
    /attached channel client 'missing' must reference a client-server channel/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot({
      spotNodes: {
        game: {
          acceptedSpotRouteChannels: { missing: {} }
        }
      }
    }),
    /Accepted SPOT route channel 'missing' is not router-capable/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot({
      spotNodes: {
        game: {
          attachedSpotPublisherClients: {
            'game.events': { manualConnections: [''] }
          }
        }
      }
    }),
    /attached SPOT publisher 'game.events' manual connection endpoint must not be empty/
  );
  assert.doesNotThrow(() => nestjs.ZLinkModule.forRoot({
    spotNodes: {
      game: {
        acceptedSpotRouteChannels: { api: {} }
      }
    },
    channels: {
      api: { server: { bind: 'tcp://0.0.0.0:9210' } }
    }
  }));
});

test('ZLinkModule.forRoot registers registry spot remote address resolver by default', async () => {
  const module = nestjs.ZLinkModule.forRoot({
    discovery: { registries: ['tcp://127.0.0.1:5551'] },
    routeChannels: ['play'],
    registrySpotRemoteAddresses: { namespace: 'bingo' }
  });
  const resolverProvider = module.providers.find((provider) => provider.provide === nestjs.ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER);

  assert.notEqual(resolverProvider, undefined);
  assert.equal(typeof resolverProvider.useFactory, 'function');
});

test('ZLinkModule.forRoot validates registry spot remote address resolver requirements', () => {
  class CustomSpotRemoteAddressResolver {
    async resolve() {
      return {
        routerChannelId: 'play',
        targetNodeRid: 'node-a',
        spotRid: 'spot-a',
        spotKind: framework.ZLinkSpotKind.User
      };
    }
  }

  assert.throws(
    () => nestjs.ZLinkModule.forRoot({
      routeChannels: ['play'],
      registrySpotRemoteAddresses: { namespace: 'bingo' }
    }),
    /requires discovery endpoints/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot({
      discovery: { registries: ['tcp://127.0.0.1:5551'] },
      routeChannels: ['play-a', 'play-b'],
      registrySpotRemoteAddresses: { namespace: 'bingo' }
    }),
    /requires RouterChannelId/
  );
  assert.throws(
    () => nestjs.ZLinkModule.forRoot({
      discovery: { registries: ['tcp://127.0.0.1:5551'] },
      routeChannels: ['play'],
      spotRemoteAddressResolver: CustomSpotRemoteAddressResolver,
      registrySpotRemoteAddresses: { namespace: 'bingo' }
    }),
    /SPOT remote address resolver/
  );
});

test('ZLinkModule.forRoot registers custom spot remote address resolver as concrete provider', async () => {
  class CustomSpotRemoteAddressResolver {
    async resolve() {
      return {
        routerChannelId: 'play',
        targetNodeRid: 'node-a',
        spotRid: 'spot-a',
        spotKind: framework.ZLinkSpotKind.User
      };
    }
  }
  const module = nestjs.ZLinkModule.forRoot({
    spotRemoteAddressResolver: CustomSpotRemoteAddressResolver
  });
  const tokens = providerTokens(module);
  const container = await resolveModuleProviders(module, [
    CustomSpotRemoteAddressResolver,
    nestjs.ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER
  ]);

  assert.equal(tokens.has(CustomSpotRemoteAddressResolver), true);
  assert.equal(
    container.get(nestjs.ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER),
    container.get(CustomSpotRemoteAddressResolver)
  );
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
              attachActorGateway() {},
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

test('framework runtime host attaches stream ActorGateway to registered SpotNode runtime', async () => {
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
    connectRouterChannelPeer() {},
    connectRouterChannelPeerRid() {},
    disconnectRouterChannelPeer() {},
    disconnectRouterChannelPeerRid() {},
    attachSpotRouteChannelDiscovery() {},
    createSpot() {},
    getOrCreateSpot() {},
    status() {},
    peers() { return []; },
    subjects() { return []; },
    attachChannelDealer() {},
    attachChannelDealerManual(channelName) { calls.push(`spot:attachChannelDealerManual:${channelName}`); },
    entrySpot() {
      return {
        setRoutingId(routingId) {
          calls.push(`entrySpot:setRoutingId:${routingId}`);
        }
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
          attachActorGateway: 'game.spot',
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
              attachActorGateway(node) {
                assert.equal(node, spotNode);
                calls.push('stream:attachActorGateway');
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
    'spot:create:3',
    'spot:setRouterBind:tcp://0.0.0.0:9110',
    'stream:attachActorGateway',
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
    setRouterBind(endpoint) { calls.push(`spot:setRouterBind:${endpoint}`); },
    setPubBind(endpoint) { calls.push(`spot:setPubBind:${endpoint}`); },
    attachDiscovery() {},
    connectPeer(endpoint) { calls.push(`spot:connectPeer:${endpoint}`); },
    disconnectPeer() {},
    connectRouterChannelPeer(channelName, endpoint) { calls.push(`spot:connectRouterChannelPeer:${channelName}:${endpoint}`); },
    connectRouterChannelPeerRid() {},
    disconnectRouterChannelPeer() {},
    disconnectRouterChannelPeerRid() {},
    attachSpotRouteChannelDiscovery() {},
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
    attachChannelDealer() {},
    attachChannelDealerManual(channelName) { calls.push(`spot:attachChannelDealerManual:${channelName}`); },
    entrySpot() {
      return {
        setRoutingId(routingId) {
          calls.push(`entrySpot:setRoutingId:${routingId}`);
        }
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
            manualConnections: ['tcp://127.0.0.1:9302']
          },
          pubSub: {
            bind: 'tcp://0.0.0.0:9303',
            routingId: 'node-a',
            manualConnections: ['tcp://127.0.0.1:9304']
          },
          entrySpot: { routingId: 'entry-node-a' },
          attachedChannelClients: {
            api: { manualConnections: ['tcp://127.0.0.1:9305'] }
          },
          attachedSpotPublisherClients: {
            'game.events': { manualConnections: ['tcp://127.0.0.1:9306'] }
          },
          acceptedSpotRouteChannels: {
            api: { manualConnections: ['tcp://127.0.0.1:9307'] }
          }
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
      }
    }
  });

  await runtime.start();
  await runtime.spotPublisherTransport.publishSpot(
    'game.events',
    'room.events',
    'GameEvent',
    { value: 'published' }
  );
  await runtime.stop();

  assert.deepEqual(calls, [
    'spot:create:3',
    'spot:setRoutingId:node-a',
    'entrySpot:setRoutingId:entry-node-a',
    'spot:setRouterBind:tcp://0.0.0.0:9301',
    'spot:setPubBind:tcp://0.0.0.0:9303',
    'spot:connectPeer:tcp://127.0.0.1:9302',
    'spot:connectPeer:tcp://127.0.0.1:9304',
    'dealer:create',
    'dealer:setChannelName:api',
    'dealer:connect:tcp://127.0.0.1:9305',
    'spot:attachChannelDealerManual:api',
    'spot:connectRouterChannelPeer:api:tcp://127.0.0.1:9307',
    'spot:createPublisherSpot',
    'spot:connectPeer:tcp://127.0.0.1:9306',
    'publisherSpot:publish:room.events:GameEvent:published',
    'publisherSpot:dispose',
    'dealer:dispose',
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
  class ActorJoinedHandler {}
  class ActorLeftHandler {}
  class ActorDisconnectedHandler {}
  class ActorJoinHandler {}
  class SpotHandler {}
  class PlayerActor {}
  class EntrySpot {
    constructor(context) {
      this.context = context;
    }
    configure() {
      calls.push(`entry:configure:${this.context.spotRid}:${this.context.nodeRid}`);
      this.context.handlers.addHandler(GenericHandler);
      this.context.handlers.addPacket(PacketHandler, 'entry.packet');
      this.context.handlers.addSubscribe(SubscribeHandler, 'entry.topic');
      this.context.handlers.addActorPacket(ActorPacketHandler, PlayerActor, 'actor.packet');
      this.context.handlers.addPostActorJoined(ActorJoinedHandler, PlayerActor);
      this.context.handlers.addActorLeft(ActorLeftHandler, PlayerActor);
      this.context.handlers.addActorDisconnected(ActorDisconnectedHandler, PlayerActor);
      this.context.handlers.addActorJoin(ActorJoinHandler, PlayerActor);
      this.context.handlers.addSpotHandler(SpotHandler);
      registry = this.context.handlers;
    }
    async onInitialize() {
      calls.push('entry:onInitialize');
    }
    async onClosing() {
      calls.push('entry:onClosing');
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
    connectRouterChannelPeer() {},
    connectRouterChannelPeerRid() {},
    disconnectRouterChannelPeer() {},
    disconnectRouterChannelPeerRid() {},
    attachSpotRouteChannelDiscovery() {},
    createSpot() { throw new Error('not used'); },
    getOrCreateSpot() {},
    status() {},
    peers() { return []; },
    subjects() { return []; },
    attachChannelDealer() {},
    attachChannelDealerManual() {},
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
          entrySpotType: EntrySpot
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
      }
    }
  });

  await runtime.start();
  await runtime.stop();

  assert.deepEqual(registry.snapshot(), [
    { kind: 'handler', handlerType: GenericHandler },
    { kind: 'packet', handlerType: PacketHandler, packetName: 'entry.packet' },
    { kind: 'subscribe', handlerType: SubscribeHandler, topic: 'entry.topic' },
    { kind: 'actorPacket', handlerType: ActorPacketHandler, actorType: PlayerActor, packetName: 'actor.packet' },
    { kind: 'postActorJoined', handlerType: ActorJoinedHandler, actorType: PlayerActor },
    { kind: 'actorLeft', handlerType: ActorLeftHandler, actorType: PlayerActor },
    { kind: 'actorDisconnected', handlerType: ActorDisconnectedHandler, actorType: PlayerActor },
    { kind: 'actorJoin', handlerType: ActorJoinHandler, actorType: PlayerActor },
    { kind: 'spotHandler', handlerType: SpotHandler }
  ]);
  assert.deepEqual(calls, [
    'spot:create',
    'spot:setRouterBind:tcp://0.0.0.0:9501',
    'spot:entrySpot',
    'entry:configure:entry-rid:node-entry',
    'entry:onInitialize',
    'entry:onClosing',
    'entry:dispose',
    'spot:dispose',
    'context:dispose'
  ]);
});

function providerTokens(module) {
  return new Set(module.providers.map((provider) => provider.provide));
}

function descriptor() {
  return {
    configurable: true,
    enumerable: false,
    value() {},
    writable: true
  };
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

    if ('useClass' in provider && provider.useClass !== undefined) {
      const value = new provider.useClass();
      values.set(provider.provide, value);
      return value;
    }

    throw new Error(`Provider ${String(token)} has no supported value or factory.`);
  }
}
