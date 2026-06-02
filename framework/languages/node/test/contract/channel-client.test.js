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
