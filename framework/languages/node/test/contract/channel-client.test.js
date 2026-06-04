const assert = require('node:assert/strict');
const net = require('node:net');
const test = require('node:test');
const { once } = require('node:events');

const zlink = require('../../../../../bindings/node/dist');
const framework = require('../../packages/framework/dist');
const nestjs = require('../../packages/nestjs/dist');

test('ZLinkChannelClient rejects calls to channels without client capability', async () => {
  const client = new framework.DefaultZLinkChannelClient(framework.createFrameworkRegistration());

  await assert.rejects(
    () => client.sendToChannel('missing', { ok: true }).submit(),
    framework.ZLinkConfigurationException
  );
});

test('ZLinkChannelClient fluent request call passes packet and timeout to transport', async () => {
  const calls = [];
  const registration = framework.createFrameworkRegistration({
    channels: {
      api: { client: { manualConnections: ['inproc://api'] } }
    }
  });
  const client = new framework.DefaultZLinkChannelClient(registration, {
    async send() {},
    async publish() {},
    async request(channelName, packetName, request, timeoutMs) {
      calls.push({ channelName, packetName, request, timeoutMs });
      return { ok: true };
    }
  });

  const reply = await client
    .requestToChannel('api', { id: 7 })
    .packetName('GetProfile')
    .timeout(250)
    .submit();

  assert.deepEqual(reply, { ok: true });
  assert.deepEqual(calls, [
    { channelName: 'api', packetName: 'GetProfile', request: { id: 7 }, timeoutMs: 250 }
  ]);
});

test('ZLinkChannelClient and fanout client reject pre-aborted submit before transport dispatch', async () => {
  const controller = new AbortController();
  controller.abort();
  const calls = [];
  const registration = framework.createFrameworkRegistration({
    channels: {
      api: { client: { manualConnections: ['inproc://api'] } },
      events: { publisher: { bind: 'inproc://events' } }
    }
  });
  const transport = {
    async send() {
      calls.push('send');
    },
    async request() {
      calls.push('request');
      return { ok: true };
    },
    async publish() {
      calls.push('publish');
    }
  };
  const client = new framework.DefaultZLinkChannelClient(registration, transport);
  const fanout = new framework.DefaultZLinkFanoutClient(registration, transport);

  await assertAborted(() => client.sendToChannel('api', 'hello').packetName('Greeting').submit(controller.signal));
  await assertAborted(() => client.requestToChannel('api', 'ping').packetName('Ping').submit(controller.signal));
  await assertAborted(() => fanout.publishToChannel('events', 'topic', 'event').packetName('Event').submit(controller.signal));
  assert.deepEqual(calls, []);
});

test('ZLinkDealerChannelClientTransport rejects pre-aborted signal before creating socket operations', async () => {
  const controller = new AbortController();
  controller.abort();
  const calls = [];
  const transport = new framework.ZLinkDealerChannelClientTransport(
    {
      send() {
        calls.push('dealer.send');
        return createMultipartSubmitOperation();
      },
      request() {
        calls.push('dealer.request');
        return createMultipartRequestOperation();
      }
    },
    {
      publish() {
        calls.push('pub.publish');
        return createMultipartSubmitOperation();
      }
    }
  );

  await assertAborted(() => transport.send('api', 'Greeting', 'hello', controller.signal));
  await assertAborted(() => transport.request('api', 'Ping', 'ping', 250, controller.signal));
  await assertAborted(() => transport.publish('events', 'topic', 'Event', 'event', controller.signal));
  assert.deepEqual(calls, []);
});

test('ZLinkModule.forRoot provides concrete channel and fanout clients', () => {
  const module = nestjs.ZLinkModule.forRoot({
    channels: {
      api: { client: { manualConnections: ['inproc://api'] }, publisher: { bind: 'inproc://pub' } }
    }
  });
  const channelProvider = module.providers.find((provider) => provider.provide === nestjs.ZLINK_CHANNEL_CLIENT);
  const fanoutProvider = module.providers.find((provider) => provider.provide === nestjs.ZLINK_FANOUT_CLIENT);

  assert.deepEqual(channelProvider.inject, [nestjs.ZLINK_FRAMEWORK_RUNTIME]);
  assert.deepEqual(fanoutProvider.inject, [nestjs.ZLINK_FRAMEWORK_RUNTIME]);
  assert.equal(typeof channelProvider.useFactory, 'function');
  assert.equal(typeof fanoutProvider.useFactory, 'function');
});

test('ZLinkChannelClient sends through public dealer/router binding sockets', async () => {
  const ctx = zlink.createContext();
  const router = zlink.createRouterSocket(ctx);
  const dealer = zlink.createDealerSocket(ctx);
  const endpoint = `inproc://framework-channel-send-${process.pid}-${Date.now()}`;

  try {
    router.bind(endpoint);
    dealer.connect(endpoint);

    const registration = framework.createFrameworkRegistration({
      channels: { api: { client: { manualConnections: [endpoint] } } }
    });
    const client = new framework.DefaultZLinkChannelClient(
      registration,
      new framework.ZLinkDealerChannelClientTransport(dealer)
    );

    await client.sendToChannel('api', 'hello').packetName('Greeting').submit();

    const received = new zlink.Received();
    assert.equal(router.recv(received), true);
    const envelope = decodeDotnetEnvelope(received.parts);
    assert.equal(envelope.header.kind, 3);
    assert.equal(envelope.header.channelName, 'api');
    assert.equal(envelope.header.messageName, 'Greeting');
    assert.equal(envelope.body, 'hello');
  } finally {
    dealer.close();
    router.close();
    ctx.close();
  }
});

test('ZLinkChannelClient request/reply round-trips through public binding sockets', async () => {
  const ctx = zlink.createContext();
  const router = zlink.createRouterSocket(ctx);
  const dealer = zlink.createDealerSocket(ctx);
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;

  try {
    const routerMonitor = router.monitorOpen([zlink.MonitorEventType.ConnectionReady]);
    const dealerMonitor = dealer.monitorOpen([zlink.MonitorEventType.ConnectionReady]);
    router.bind(endpoint);
    dealer.connect(endpoint);
    routerMonitor.recv();
    dealerMonitor.recv();
    routerMonitor.close();
    dealerMonitor.close();

    const registration = framework.createFrameworkRegistration({
      channels: { api: { client: { manualConnections: [endpoint] } } }
    });
    const client = new framework.DefaultZLinkChannelClient(
      registration,
      new framework.ZLinkDealerChannelClientTransport(dealer)
    );

    const replyPromise = client.requestToChannel('api', { value: 'ping' }).packetName('Ping').timeout(1000).submit();
    const request = await recvRouterMessage(router);
    const envelope = decodeDotnetEnvelope(request.parts);
    assert.equal(envelope.header.kind, 1);
    assert.equal(envelope.header.channelName, 'api');
    assert.equal(envelope.header.messageName, 'Ping');
    assert.deepEqual(envelope.body, { value: 'ping' });
    assert.equal(typeof request.requestSeq, 'bigint');
    submitMultipart(
      router.reply(request.routingId, request.requestSeq),
      encodeDotnetEnvelope({
        ...envelope.header,
        kind: 2,
        deadline: null,
        topic: null,
        errorCode: null,
        errorMessage: null
      }, { value: 'pong' })
    );

    const reply = await withTimeout(replyPromise, 1000, 'framework channel request reply');
    assert.deepEqual(reply, { value: 'pong' });
    request.close();
  } finally {
    dealer.close();
    router.close();
    ctx.close();
  }
});

test('ZLinkModule channel client uses runtime host channel transport after bootstrap', async () => {
  const ctx = zlink.createContext();
  const router = zlink.createRouterSocket(ctx);
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const module = nestjs.ZLinkModule.forRoot({
    channels: { api: { client: { manualConnections: [endpoint] } } }
  });
  const container = await resolveModuleProviders(module, [
    nestjs.ZLINK_FRAMEWORK_RUNTIME,
    nestjs.ZLINK_CHANNEL_CLIENT
  ]);
  const runtime = container.get(nestjs.ZLINK_FRAMEWORK_RUNTIME);
  const client = container.get(nestjs.ZLINK_CHANNEL_CLIENT);

  try {
    router.bind(endpoint);
    await runtime.start();

    const replyPromise = client.requestToChannel('api', { value: 'ping' }).packetName('Ping').timeout(1000).submit();
    const request = await recvRouterMessage(router);
    const envelope = decodeDotnetEnvelope(request.parts);
    assert.equal(envelope.header.kind, 1);
    assert.equal(envelope.header.channelName, 'api');
    assert.equal(envelope.header.messageName, 'Ping');
    assert.deepEqual(envelope.body, { value: 'ping' });

    submitMultipart(
      router.reply(request.routingId, request.requestSeq),
      encodeDotnetEnvelope({
        ...envelope.header,
        kind: 2,
        deadline: null,
        topic: null,
        errorCode: null,
        errorMessage: null
      }, { value: 'pong' })
    );

    const reply = await withTimeout(replyPromise, 1000, 'DI framework channel request reply');
    assert.deepEqual(reply, { value: 'pong' });
    request.close();
  } finally {
    await runtime.stop();
    router.close();
    ctx.close();
  }
});

test('ZLinkFrameworkRuntimeHost dispatches client-server channel request handlers', async () => {
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const calls = [];
  const serverRegistration = framework.createFrameworkRegistration({
    channels: {
      play: {
        server: { bind: endpoint },
        requestHandlers: [{
          packetName: 'CreateGame',
          handler: {
            handle(payload, context) {
              assert.equal(context.channelName, 'play');
              assert.equal(context.packetName, 'CreateGame');
              const reply = { created: JSON.parse(payload.toString()).gameName };
              calls.push(reply);
              return reply;
            }
          }
        }]
      }
    }
  });
  const clientRegistration = framework.createFrameworkRegistration({
    channels: {
      play: { client: { manualConnections: [endpoint] } }
    }
  });
  const serverRuntime = new framework.ZLinkFrameworkRuntimeHost({ registration: serverRegistration });
  const clientRuntime = new framework.ZLinkFrameworkRuntimeHost({ registration: clientRegistration });

  try {
    await serverRuntime.start();
    await clientRuntime.start();
    const client = new framework.DefaultZLinkChannelClient(clientRegistration, clientRuntime.channelTransport);
    const reply = await submitWhenReachable(() =>
      client.requestToChannel('play', { gameName: 'sample' }).packetName('CreateGame').timeout(1000).submit()
    );

    assert.deepEqual(calls, [{ created: 'sample' }]);
    assert.deepEqual(reply, { created: 'sample' });
  } finally {
    await clientRuntime.stop();
    await serverRuntime.stop();
  }
});

test('ZLinkFrameworkRuntimeHost dispatches fanout subscriber publish handlers', async () => {
  const ctx = zlink.createContext();
  const publisher = zlink.createPubSocket(ctx);
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const topic = 'profile.updated';
  const calls = [];
  const publisherMonitor = publisher.monitorOpen([zlink.MonitorEventType.ConnectionReady]);
  publisher.bind(endpoint);
  const subscriberRegistration = framework.createFrameworkRegistration({
    channels: {
      events: {
        subscriber: { manualConnections: [endpoint] },
        publishHandlers: [{
          packetName: 'ProfileChanged',
          handler: {
            handle(payload, context) {
              calls.push({
                payload: JSON.parse(payload.toString()),
                channelName: context.channelName,
                packetName: context.packetName,
                topic: context.topic,
                contentType: context.contentType
              });
            }
          }
        }]
      }
    }
  });
  const subscriberRuntime = new framework.ZLinkFrameworkRuntimeHost({ registration: subscriberRegistration });

  try {
    await subscriberRuntime.start();
    publisherMonitor.recv();
    publisherMonitor.close();

    submitMultipart(publisher.publish(topic), encodeDotnetEnvelope({
      kind: 4,
      channelName: 'events',
      messageName: 'ProfileChanged',
      contentType: 'application/json',
      correlationId: null,
      deadline: null,
      topic,
      errorCode: null,
      errorMessage: null,
      source: 'publisher-node'
    }, { profileId: 'p1' }));

    await waitFor(() => calls.length === 1, 'fanout subscriber publish handler');
    assert.deepEqual(calls, [{
      payload: { profileId: 'p1' },
      channelName: 'events',
      packetName: 'ProfileChanged',
      topic,
      contentType: 'application/json'
    }]);
  } finally {
    await subscriberRuntime.stop();
    publisher.close();
    ctx.close();
  }
});

test('ZLinkModule route client uses runtime host route transport after bootstrap', async () => {
  const ctx = zlink.createContext();
  const remoteRouter = zlink.createRouterSocket(ctx);
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const module = nestjs.ZLinkModule.forRoot({
    routeChannels: [{
      routerChannelId: 'mesh',
      bind: endpoint,
      routingId: 'node-a'
    }]
  });
  const container = await resolveModuleProviders(module, [
    nestjs.ZLINK_FRAMEWORK_RUNTIME,
    nestjs.ZLINK_ROUTE_CLIENT
  ]);
  const runtime = container.get(nestjs.ZLINK_FRAMEWORK_RUNTIME);
  const routeClient = container.get(nestjs.ZLINK_ROUTE_CLIENT);

  try {
    await runtime.start();
    remoteRouter.setRoutingId(zlink.RoutingId.from('node-b'));
    remoteRouter.connect(endpoint);

    await submitWhenRouteReachable(() =>
      routeClient.send('mesh', 'node-b', { value: 'one-way' }).packetName('RouteNotice').submit()
    );
    const sent = await recvRoutedEnvelopeMessage(remoteRouter);
    const sentEnvelope = decodeDotnetEnvelope(sent.parts);
    assert.equal(sentEnvelope.header.kind, 3);
    assert.equal(sentEnvelope.header.channelName, 'mesh');
    assert.equal(sentEnvelope.header.messageName, 'RouteNotice');
    assert.deepEqual(sentEnvelope.body, { value: 'one-way' });
    sent.close();

    const replyPromise = routeClient.request('mesh', 'node-b', { value: 'ping' }).packetName('RoutePing').timeout(1000).submit();
    const request = await recvRoutedEnvelopeMessage(remoteRouter);
    const envelope = decodeDotnetEnvelope(request.parts);
    assert.equal(envelope.header.kind, 1);
    assert.equal(envelope.header.channelName, 'mesh');
    assert.equal(envelope.header.messageName, 'RoutePing');
    assert.deepEqual(envelope.body, { value: 'ping' });

    submitMultipart(
      remoteRouter.reply(request.routingId, request.requestSeq),
      encodeDotnetEnvelope({
        ...envelope.header,
        kind: 2,
        deadline: null,
        topic: null,
        errorCode: null,
        errorMessage: null
      }, { value: 'pong' })
    );

    const reply = await withTimeout(replyPromise, 1000, 'DI framework route request reply');
    assert.deepEqual(reply, { value: 'pong' });
    request.close();
  } finally {
    await runtime.stop();
    remoteRouter.close();
    ctx.close();
  }
});

test('ZLinkRoutePacketDispatcher invokes routed send and request handlers', async () => {
  const ctx = zlink.createContext();
  const localRouter = zlink.createRouterSocket(ctx);
  const remoteDealer = zlink.createDealerSocket(ctx);
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const events = [];

  try {
    localRouter.setRoutingId(zlink.RoutingId.from('node-a'));
    remoteDealer.setRoutingId(zlink.RoutingId.from('node-b'));
    localRouter.options.probe = true;
    remoteDealer.options.probe = true;
    localRouter.bind(endpoint);
    remoteDealer.connect(endpoint);
    const probe = await recvRouterMessage(localRouter);
    probe.close();
    const dispatcher = new framework.ZLinkRoutePacketDispatcher({
      routerChannelId: 'mesh',
      handlers: [
        {
          kind: 'send',
          packetName: 'RouteNotice',
          handler: {
            async handle(payload, context) {
              events.push(`send:${context.channelName}:${context.packetName}:${JSON.parse(payload.toString()).value}`);
            }
          }
        },
        {
          kind: 'request',
          packetName: 'RoutePing',
          handler: {
            async handle(payload, context) {
              events.push(`request:${context.channelName}:${context.packetName}:${context.requestSeq}:${JSON.parse(payload.toString()).value}`);
              return { value: 'pong' };
            }
          }
        }
      ]
    });

    submitMultipart(
      remoteDealer.send(),
      encodeDotnetEnvelope({
        kind: 3,
        channelName: 'mesh',
        messageName: 'RouteNotice',
        contentType: 'application/json',
        correlationId: null,
        deadline: null,
        topic: null,
        errorCode: null,
        errorMessage: null
      }, { value: 'one-way' })
    );
    const sent = await recvRouterMessage(localRouter);
    await dispatcher.dispatch(sent, localRouter);
    sent.close();

    const replyPromise = submitRequestMultipart(
      remoteDealer.request(),
      encodeDotnetEnvelope({
        kind: 1,
        channelName: 'mesh',
        messageName: 'RoutePing',
        contentType: 'application/json',
        correlationId: null,
        deadline: null,
        topic: null,
        errorCode: null,
        errorMessage: null
      }, { value: 'ping' })
    );
    const request = await recvRouterMessage(localRouter);
    await dispatcher.dispatch(request, localRouter);
    request.close();

    const reply = await withTimeout(replyPromise, 1000, 'route dispatcher reply');
    const envelope = decodeDotnetEnvelope(reply);
    assert.equal(envelope.header.kind, 2);
    assert.deepEqual(envelope.body, { value: 'pong' });
    assert.equal(events[0], 'send:mesh:RouteNotice:one-way');
    assert.match(events[1], /^request:mesh:RoutePing:\d+:ping$/);
    reply.forEach((part) => part.close());
  } finally {
    remoteDealer.close();
    localRouter.close();
    ctx.close();
  }
});

test('ZLinkModule route channel dispatches inbound routed handlers after bootstrap', async () => {
  const ctx = zlink.createContext();
  const remoteDealer = zlink.createDealerSocket(ctx);
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const events = [];
  const module = nestjs.ZLinkModule.forRoot({
    routeChannels: [{
      routerChannelId: 'mesh',
      bind: endpoint,
      routingId: 'node-a',
      sendHandlers: [
        {
          packetName: 'RouteNotice',
          handler: {
            async handle(payload, context) {
              events.push(`send:${context.channelName}:${context.packetName}:${JSON.parse(payload.toString()).value}`);
            }
          }
        }
      ],
      requestHandlers: [
        {
          packetName: 'RoutePing',
          handler: {
            async handle(payload, context) {
              events.push(`request:${context.channelName}:${context.packetName}:${context.requestSeq}:${JSON.parse(payload.toString()).value}`);
              return { value: 'pong' };
            }
          }
        }
      ]
    }]
  });
  const container = await resolveModuleProviders(module, [nestjs.ZLINK_FRAMEWORK_RUNTIME]);
  const runtime = container.get(nestjs.ZLINK_FRAMEWORK_RUNTIME);

  try {
    await runtime.start();
    remoteDealer.setRoutingId(zlink.RoutingId.from('node-b'));
    remoteDealer.options.probe = true;
    remoteDealer.connect(endpoint);
    const readyReply = await withTimeout(
      submitRequestMultipart(
        remoteDealer.request(),
        encodeDotnetEnvelope({
          kind: 1,
          channelName: 'mesh',
          messageName: 'RouteReadyProbe',
          contentType: 'application/json',
          correlationId: null,
          deadline: null,
          topic: null,
          errorCode: null,
          errorMessage: null
        }, { value: 'ready' })
      ),
      1000,
      'route runtime readiness probe'
    );
    assert.equal(decodeDotnetEnvelope(readyReply).header.kind, 5);
    readyReply.forEach((part) => part.close());

    submitMultipart(
      remoteDealer.send(),
      encodeDotnetEnvelope({
        kind: 3,
        channelName: 'mesh',
        messageName: 'RouteNotice',
        contentType: 'application/json',
        correlationId: null,
        deadline: null,
        topic: null,
        errorCode: null,
        errorMessage: null
      }, { value: 'one-way' })
    );
    await waitFor(() => events.length >= 1, 'route send handler');

    const replyPromise = submitRequestMultipart(
      remoteDealer.request(),
      encodeDotnetEnvelope({
        kind: 1,
        channelName: 'mesh',
        messageName: 'RoutePing',
        contentType: 'application/json',
        correlationId: null,
        deadline: null,
        topic: null,
        errorCode: null,
        errorMessage: null
      }, { value: 'ping' })
    );

    const reply = await withTimeout(replyPromise, 1000, 'runtime route handler reply');
    const envelope = decodeDotnetEnvelope(reply);
    assert.equal(envelope.header.kind, 2);
    assert.deepEqual(envelope.body, { value: 'pong' });
    assert.equal(events[0], 'send:mesh:RouteNotice:one-way');
    assert.match(events[1], /^request:mesh:RoutePing:\d+:ping$/);
    reply.forEach((part) => part.close());
  } finally {
    remoteDealer.close();
    await runtime.stop();
    ctx.close();
  }
});

test('ZLinkModule routeMesh channel option dispatches inbound routed handlers after bootstrap', async () => {
  const ctx = zlink.createContext();
  const remoteDealer = zlink.createDealerSocket(ctx);
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const events = [];
  const module = nestjs.ZLinkModule.forRoot({
    channels: {
      mesh: {
        routeMesh: {
          bind: endpoint,
          routingId: 'node-a',
          requestHandlers: [
            {
              packetName: 'RoutePing',
              handler: {
                async handle(payload, context) {
                  events.push(`request:${context.channelName}:${context.packetName}:${context.requestSeq}:${JSON.parse(payload.toString()).value}`);
                  return { value: 'pong' };
                }
              }
            }
          ]
        }
      }
    }
  });
  const container = await resolveModuleProviders(module, [nestjs.ZLINK_FRAMEWORK_RUNTIME]);
  const runtime = container.get(nestjs.ZLINK_FRAMEWORK_RUNTIME);

  try {
    await runtime.start();
    remoteDealer.setRoutingId(zlink.RoutingId.from('node-b'));
    remoteDealer.options.probe = true;
    remoteDealer.connect(endpoint);

    const reply = await withTimeout(
      submitRequestMultipart(
        remoteDealer.request(),
        encodeDotnetEnvelope({
          kind: 1,
          channelName: 'mesh',
          messageName: 'RoutePing',
          contentType: 'application/json',
          correlationId: null,
          deadline: null,
          topic: null,
          errorCode: null,
          errorMessage: null
        }, { value: 'ping' })
      ),
      1000,
      'routeMesh option runtime handler reply'
    );
    const envelope = decodeDotnetEnvelope(reply);
    assert.equal(envelope.header.kind, 2);
    assert.deepEqual(envelope.body, { value: 'pong' });
    assert.match(events[0], /^request:mesh:RoutePing:\d+:ping$/);
    reply.forEach((part) => part.close());
  } finally {
    remoteDealer.close();
    await runtime.stop();
    ctx.close();
  }
});

test('channel runtime waits for send-ready instead of failing backpressured sends', async () => {
  const socket = fakeBackpressuredDealer();
  const manager = new framework.ZLinkChannelRuntimeManager(
    framework.createFrameworkRegistration({
      channels: { api: { client: { manualConnections: ['tcp://peer:7101'] } } }
    }),
    fakeChannelAdapter({ dealer: socket }),
    fakeContext()
  );

  const pending = manager.send('api', 'Notice', { ready: true });
  await Promise.resolve();

  assert.equal(socket.sendAttempts, 2);
  assert.equal(socket.sentParts, undefined);

  socket.writable = true;
  socket.ready();
  await pending;

  assert.equal(socket.sendAttempts, 3);
  assert.equal(decodeDotnetEnvelope(socket.sentParts).header.messageName, 'Notice');
  await manager.dispose();
});

test('channel runtime uses dealer mesh client capability for channel sends', async () => {
  const socket = fakeBackpressuredDealer();
  socket.writable = true;
  const manager = new framework.ZLinkChannelRuntimeManager(
    framework.createFrameworkRegistration({
      channels: {
        mesh: {
          dealerMesh: {
            client: { manualConnections: ['tcp://peer:7109'] }
          }
        }
      }
    }),
    fakeChannelAdapter({ dealer: socket }),
    fakeContext()
  );

  await manager.send('mesh', 'MeshNotice', { id: 'mesh-send' });

  assert.equal(socket.endpoint, 'tcp://peer:7109');
  assert.equal(decodeDotnetEnvelope(socket.sentParts).header.channelName, 'mesh');
  assert.equal(decodeDotnetEnvelope(socket.sentParts).header.messageName, 'MeshNotice');
  await manager.dispose();
});

test('channel runtime drains backpressured requests from send-ready callback', async () => {
  const socket = fakeBackpressuredDealer();
  const manager = new framework.ZLinkChannelRuntimeManager(
    framework.createFrameworkRegistration({
      channels: { api: { client: { manualConnections: ['tcp://peer:7101'] } } }
    }),
    fakeChannelAdapter({ dealer: socket }),
    fakeContext()
  );

  const pending = manager.request('api', 'Ping', { value: 'ping' }, 1000);
  await Promise.resolve();

  assert.equal(socket.requestAttempts, 2);

  socket.writable = true;
  socket.replyParts = encodeDotnetEnvelope({
    kind: 2,
    channelName: 'api',
    messageName: 'Ping',
    contentType: 'application/json',
    correlationId: null,
    deadline: null,
    topic: null,
    errorCode: null,
    errorMessage: null
  }, { value: 'pong' }).map(fakeMessagePart);
  socket.ready();

  assert.deepEqual(await pending, { value: 'pong' });
  assert.equal(socket.requestAttempts, 3);
  await manager.dispose();
});

test('ZLinkFanoutClient publishes through public pub/sub binding sockets', async () => {
  const ctx = zlink.createContext();
  const pub = zlink.createPubSocket(ctx);
  const sub = zlink.createSubSocket(ctx);
  const dealer = zlink.createDealerSocket(ctx);
  const endpoint = `inproc://framework-channel-publish-${process.pid}-${Date.now()}`;
  const topic = 'events';

  try {
    pub.bind(endpoint);
    sub.setSubscription(topic);
    sub.connect(endpoint);

    const registration = framework.createFrameworkRegistration({
      channels: { events: { publisher: { bind: endpoint } } }
    });
    const fanout = new framework.DefaultZLinkFanoutClient(
      registration,
      new framework.ZLinkDealerChannelClientTransport(dealer, pub)
    );

    const received = new zlink.TopicMessage();
    await fanout.publishToChannel('events', topic, 'published').packetName('Event').submit();
    assert.equal(sub.subscribe(received), true);
    assert.equal(received.topic, topic);
    const envelope = decodeDotnetEnvelope(received.parts);
    assert.equal(envelope.header.kind, 4);
    assert.equal(envelope.header.channelName, 'events');
    assert.equal(envelope.header.messageName, 'Event');
    assert.equal(envelope.header.topic, topic);
    assert.equal(envelope.body, 'published');
  } finally {
    dealer.close();
    sub.close();
    pub.close();
    ctx.close();
  }
});

test('ZLinkChannelRequestDispatcher invokes request handler and replies through router', async () => {
  const ctx = zlink.createContext();
  const router = zlink.createRouterSocket(ctx);
  const dealer = zlink.createDealerSocket(ctx);
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const filterEvents = [];

  try {
    const routerMonitor = router.monitorOpen([zlink.MonitorEventType.ConnectionReady]);
    const dealerMonitor = dealer.monitorOpen([zlink.MonitorEventType.ConnectionReady]);
    router.bind(endpoint);
    dealer.connect(endpoint);
    routerMonitor.recv();
    dealerMonitor.recv();
    routerMonitor.close();
    dealerMonitor.close();

    const registration = framework.createFrameworkRegistration({
      channels: { api: { client: { manualConnections: [endpoint] } } }
    });
    const client = new framework.DefaultZLinkChannelClient(
      registration,
      new framework.ZLinkDealerChannelClientTransport(dealer)
    );
    const dispatcher = new framework.ZLinkChannelRequestDispatcher({
      handlers: new Map([
        ['Ping', {
          async handle(payload) {
            assert.equal(JSON.parse(payload.toString()), 'ping');
            return 'pong';
          }
        }]
      ]),
      filters: [{
        async invoke(_invocation, next) {
          filterEvents.push('before');
          const result = await next();
          filterEvents.push('after');
          return result;
        }
      }]
    });

    const replyPromise = client.requestToChannel('api', 'ping').packetName('Ping').timeout(1000).submit();
    const received = await recvRouterMessage(router);
    await dispatcher.dispatch(received, router);

    const reply = await withTimeout(replyPromise, 1000, 'framework dispatcher reply');
    assert.equal(reply, 'pong');
    assert.deepEqual(filterEvents, ['before', 'after']);
    received.close();
  } finally {
    dealer.close();
    router.close();
    ctx.close();
  }
});

async function recvRouterMessage(router) {
  const deadline = Date.now() + 1000;
  while (Date.now() < deadline) {
    const received = new zlink.Received();
    if (router.recv(received, zlink.RecvFlags.DontWait)) {
      return received;
    }
    await new Promise((resolve) => setImmediate(resolve));
  }
  assert.fail('router did not receive request');
}

async function recvRoutedEnvelopeMessage(router) {
  const deadline = Date.now() + 1000;
  while (Date.now() < deadline) {
    const received = new zlink.Received();
    if (router.recv(received, zlink.RecvFlags.DontWait)) {
      if (received.parts.length >= 2 && received.parts[0].data().length > 0) {
        return received;
      }
      received.close();
    }
    await new Promise((resolve) => setImmediate(resolve));
  }
  assert.fail('router did not receive routed envelope');
}

async function submitWhenRouteReachable(submit) {
  const deadline = Date.now() + 1000;
  let lastError;
  while (Date.now() < deadline) {
    try {
      await submit();
      return;
    } catch (error) {
      if (!isHostUnreachable(error)) {
        throw error;
      }
      lastError = error;
      await new Promise((resolve) => setImmediate(resolve));
    }
  }
  throw lastError;
}

async function submitWhenReachable(submit) {
  const deadline = Date.now() + 1000;
  let lastError;
  while (Date.now() < deadline) {
    try {
      return await submit();
    } catch (error) {
      if (!isHostUnreachable(error)) {
        throw error;
      }
      lastError = error;
      await new Promise((resolve) => setImmediate(resolve));
    }
  }
  throw lastError;
}

function isHostUnreachable(error) {
  return error instanceof Error && error.code === 2 && /Host unreachable/.test(error.message);
}

async function waitFor(predicate, label) {
  const deadline = Date.now() + 1000;
  while (Date.now() < deadline) {
    if (predicate()) {
      return;
    }
    await new Promise((resolve) => setImmediate(resolve));
  }
  assert.fail(`${label} did not complete`);
}

async function reservePort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const { port } = server.address();
  await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return port;
}

function subscribeMaybe(socket, received) {
  try {
    return socket.subscribe(received, zlink.RecvFlags.DontWait);
  } catch (error) {
    if (error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData) {
      return false;
    }
    throw error;
  }
}

function decodeDotnetEnvelope(parts) {
  assert.equal(parts.length, 2);
  return {
    header: JSON.parse(parts[0].data().toString()),
    body: JSON.parse(parts[1].data().toString())
  };
}

function encodeDotnetEnvelope(header, body) {
  return [
    Buffer.from(JSON.stringify(header)),
    Buffer.from(JSON.stringify(body))
  ];
}

function submitMultipart(operation, parts) {
  let current = operation.message(parts[0]);
  for (let index = 1; index < parts.length; index++) {
    current = current.message(parts[index]);
  }
  current.submit();
}

function submitRequestMultipart(operation, parts) {
  let current = operation.message(parts[0]);
  for (let index = 1; index < parts.length; index++) {
    current = current.message(parts[index]);
  }
  return current.submitAsync();
}

function withTimeout(promise, timeoutMs, label) {
  let timeout;
  const guard = new Promise((_, reject) => {
    timeout = setTimeout(() => reject(new Error(`${label} timed out`)), timeoutMs);
  });
  return Promise.race([promise, guard]).finally(() => clearTimeout(timeout));
}

async function assertAborted(action) {
  await assert.rejects(
    action,
    (error) => error instanceof Error && error.message === 'The operation was aborted.'
  );
}

function createMultipartSubmitOperation() {
  return {
    message() {
      return this;
    },
    submit() {}
  };
}

function createMultipartRequestOperation() {
  return {
    message() {
      return this;
    },
    timeout() {
      return this;
    },
    async submitAsync() {
      return [];
    }
  };
}

function fakeContext() {
  return {
    nativeInstance: {},
    shutdown() {},
    async dispose() {}
  };
}

function fakeChannelAdapter({ dealer }) {
  return {
    createDealerSocket() {
      return dealer;
    },
    createPublisherSocket() {
      throw new Error('publisher not used');
    },
    createRouterSocket() {
      throw new Error('router not used');
    }
  };
}

function fakeBackpressuredDealer() {
  let readyHandler = () => undefined;
  return {
    nativeInstance: {},
    writable: false,
    sendAttempts: 0,
    requestAttempts: 0,
    sentParts: undefined,
    replyParts: undefined,
    setChannelName(channelName) {
      this.channelName = channelName;
    },
    connect(endpoint) {
      this.endpoint = endpoint;
    },
    onSendReady(handler) {
      readyHandler = handler;
    },
    ready() {
      readyHandler();
    },
    send(parts) {
      this.sendAttempts++;
      if (!this.writable) {
        return false;
      }
      this.sentParts = parts.map(fakeMessagePart);
      return true;
    },
    request(parts, callback) {
      this.requestAttempts++;
      if (!this.writable) {
        return false;
      }
      callback(0, this.replyParts);
      return true;
    },
    async dispose() {}
  };
}

function fakeMessagePart(part) {
  const payload = Buffer.from(part);
  return {
    data() {
      return payload;
    },
    close() {}
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

    throw new Error(`Provider ${String(token)} has no supported value or factory.`);
  }
}
