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
      api: { client: {} }
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

test('ZLinkModule.forRoot provides concrete channel and fanout clients', () => {
  const module = nestjs.ZLinkModule.forRoot({
    channels: {
      api: { client: {}, publisher: { bind: 'inproc://pub' } }
    }
  });
  const channelProvider = module.providers.find((provider) => provider.provide === nestjs.ZLINK_CHANNEL_CLIENT);
  const fanoutProvider = module.providers.find((provider) => provider.provide === nestjs.ZLINK_FANOUT_CLIENT);

  assert.equal(channelProvider.useValue instanceof framework.DefaultZLinkChannelClient, true);
  assert.equal(fanoutProvider.useValue instanceof framework.DefaultZLinkFanoutClient, true);
});

test('ZLinkChannelClient sends through public dealer/router binding sockets', async () => {
  const ctx = zlink.createContext();
  const router = zlink.createRouterSocket(ctx);
  const dealer = zlink.createDealerSocket(ctx);
  const endpoint = `inproc://framework-channel-send-${process.pid}-${Date.now()}`;

  try {
    router.bind(endpoint);
    dealer.connect(endpoint);

    const registration = framework.createFrameworkRegistration({ channels: { api: { client: {} } } });
    const client = new framework.DefaultZLinkChannelClient(
      registration,
      new framework.ZLinkDealerChannelClientTransport(dealer)
    );

    await client.sendToChannel('api', 'hello').packetName('Greeting').submit();

    const received = new zlink.Received();
    assert.equal(router.recv(received), true);
    const envelope = JSON.parse(received.singlePartOrThrow().data().toString());
    assert.equal(envelope.packetName, 'Greeting');
    assert.equal(Buffer.from(envelope.payload, 'base64').toString(), 'hello');
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

    const registration = framework.createFrameworkRegistration({ channels: { api: { client: {} } } });
    const client = new framework.DefaultZLinkChannelClient(
      registration,
      new framework.ZLinkDealerChannelClientTransport(dealer)
    );

    const replyPromise = client.requestToChannel('api', 'ping').packetName('Ping').timeout(1000).submit();
    const request = new zlink.Received();
    router.recv(request);
    const envelope = JSON.parse(request.singlePartOrThrow().data().toString());
    assert.equal(envelope.packetName, 'Ping');
    assert.equal(Buffer.from(envelope.payload, 'base64').toString(), 'ping');
    assert.equal(typeof request.requestSeq, 'bigint');
    router.reply(request.routingId, request.requestSeq).message('pong').submit();

    const reply = await withTimeout(replyPromise, 1000, 'framework channel request reply');
    try {
      assert.equal(reply[0].data().toString(), 'pong');
    } finally {
      for (const part of reply) {
        part.close();
      }
      request.close();
    }
  } finally {
    dealer.close();
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
    const deadline = Date.now() + 1000;
    while (Date.now() < deadline) {
      await fanout.publishToChannel('events', topic, 'published').packetName('Event').submit();
      if (subscribeMaybe(sub, received)) {
        assert.equal(received.topic, topic);
        const envelope = JSON.parse(received.singlePartOrThrow().data().toString());
        assert.equal(envelope.packetName, 'Event');
        assert.equal(Buffer.from(envelope.payload, 'base64').toString(), 'published');
        return;
      }
      await new Promise((resolve) => setImmediate(resolve));
    }
    assert.fail('publish did not reach subscriber');
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

    const registration = framework.createFrameworkRegistration({ channels: { api: { client: {} } } });
    const client = new framework.DefaultZLinkChannelClient(
      registration,
      new framework.ZLinkDealerChannelClientTransport(dealer)
    );
    const dispatcher = new framework.ZLinkChannelRequestDispatcher({
      handlers: new Map([
        ['Ping', {
          async handle(payload) {
            assert.equal(payload.toString(), 'ping');
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
    const received = new zlink.Received();
    router.recv(received);
    await dispatcher.dispatch(received, router);

    const reply = await withTimeout(replyPromise, 1000, 'framework dispatcher reply');
    try {
      assert.equal(reply[0].data().toString(), 'pong');
      assert.deepEqual(filterEvents, ['before', 'after']);
    } finally {
      for (const part of reply) {
        part.close();
      }
      received.close();
    }
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

function withTimeout(promise, timeoutMs, label) {
  let timeout;
  const guard = new Promise((_, reject) => {
    timeout = setTimeout(() => reject(new Error(`${label} timed out`)), timeoutMs);
  });
  return Promise.race([promise, guard]).finally(() => clearTimeout(timeout));
}
