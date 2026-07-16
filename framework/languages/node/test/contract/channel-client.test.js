const assert = require('node:assert/strict');
const fs = require('node:fs');
const net = require('node:net');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');
const { once } = require('node:events');
const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');

const zlink = require('@zlink-systems/zlink');
const framework = require('../../packages/framework/dist/internal');
const { resolveFrameworkPacketName } = require('../../packages/framework/dist/runtime/messaging/packet-name');

function dispatchOptions(observerType) {
  return framework.createFrameworkOptions((options) => {
    options.configureDispatch().setMessageFlowObserver(observerType);
  }).dispatch;
}
const frameworkProtobuf = require('../../packages/framework-codec-protobuf/dist/server/framework.cjs');
const nestjs = require('../../packages/nestjs/dist');
const { resolveModuleProviders } = require('./helpers/nestjs-test-utils');

function typedPacket(packetName, value) {
  const PacketType = typeof value === 'string'
    ? { [packetName]: class extends String {} }[packetName]
    : { [packetName]: class {} }[packetName];
  return typeof value === 'string'
    ? new PacketType(value)
    : Object.assign(new PacketType(), value);
}

test('typed packet identity is owned by the packet type and not by call or payload overrides', () => {
  class ConstructorPacket {}
  class DecoratedPacket {}
  class DecoratedSubclass extends DecoratedPacket {}
  framework.ZLinkPacket('ContractPacket')(DecoratedPacket);

  assert.equal(resolveFrameworkPacketName(new ConstructorPacket(), undefined, 'Channel'), 'ConstructorPacket');
  assert.equal(resolveFrameworkPacketName(new DecoratedPacket(), undefined, 'Channel'), 'ContractPacket');
  assert.equal(resolveFrameworkPacketName(new DecoratedSubclass(), undefined, 'Channel'), 'DecoratedSubclass');
  assert.throws(
    () => resolveFrameworkPacketName({ packetName: () => 'PayloadOverride' }, undefined, 'Channel'),
    framework.ZLinkConfigurationException
  );

  const registration = framework.createFrameworkRegistration({
    channels: { api: { client: { manualConnections: ['inproc://api'] } } }
  });
  const call = new framework.DefaultZLinkChannelClient(registration, {
    async send() {}, async request() {}, async publish() {}
  }).requestToChannel('api', new ConstructorPacket());
  assert.equal('packetName' in call, false);
});

function fakeSpotRouteBridge() {
  return {
    attachRouterChannel() {},
    send() { return { message() { return this; }, submit() { return true; } }; },
    request() { return { message() { return this; }, timeout() { return this; }, submit() { return true; } }; },
    handleRouterReceived() { return false; },
    async dispose() {}
  };
}

test('ZLinkChannelClient rejects calls to channels without client capability', async () => {
  const client = new framework.DefaultZLinkChannelClient(framework.createFrameworkRegistration());

  assert.throws(
    () => client.sendToChannel('missing', { ok: true }).submit(),
    framework.ZLinkConfigurationException
  );
});

test('one-way clients throw synchronously when their registered runtime is not started', () => {
  const registration = framework.createFrameworkRegistration({
    channels: {
      api: { client: { manualConnections: ['inproc://api'] } },
      events: { publisher: { bind: 'inproc://events' } }
    },
    routeChannels: [{ routerChannelId: 'route', manualConnections: ['inproc://route'] }]
  });

  assert.throws(
    () => new framework.DefaultZLinkChannelClient(registration).sendToChannel('api', typedPacket('Ping')).submit(),
    /runtime is not started/i
  );
  assert.throws(
    () => new framework.DefaultZLinkFanoutClient(registration).publish('events', 'topic', typedPacket('Event')).submit(),
    /runtime is not started/i
  );
  assert.throws(
    () => new framework.DefaultZLinkRouteClient(registration).sendToNode('route', 'target', typedPacket('Ping')).submit(),
    /runtime is not started/i
  );
});

test('ZLinkRouteClient one-way calls use the required synchronous transport submit contract', () => {
  const calls = [];
  const registration = framework.createFrameworkRegistration({
    routeChannels: [{ routerChannelId: 'route', manualConnections: ['inproc://route'] }]
  });
  const client = new framework.DefaultZLinkRouteClient(registration, {
    submit(routerChannelId, targetNodeRid, packetName, message) {
      calls.push({ routerChannelId, targetNodeRid, packetName, message });
    },
    async request() { return undefined; }
  });
  const packet = typedPacket('Ping', { value: 1 });

  const result = client.sendToNode('route', 'target', packet).submit();

  assert.equal(result, undefined);
  assert.deepEqual(calls, [{
    routerChannelId: 'route',
    targetNodeRid: 'target',
    packetName: undefined,
    message: packet
  }]);
});

test('ZLinkRouteClient sends through SpotHandle snapshots and refreshes one stale request once', async () => {
  const oldTarget = {
    meshName: 'play',
    nodeRid: 'node-old',
    spotRid: 'spot-1',
    spotKind: framework.ZLinkSpotKind.User
  };
  const newTarget = { ...oldTarget, nodeRid: 'node-new' };
  let refreshCount = 0;
  const handle = framework.createSpotHandle('spot-1', oldTarget, async () => {
    refreshCount += 1;
    return newTarget;
  });
  const sends = [];
  const requests = [];
  const client = new framework.DefaultZLinkRouteClient(
    framework.createFrameworkRegistration({
      routeChannels: [{ routerChannelId: 'play.route', manualConnections: ['inproc://play'] }]
    }),
    {
      submit() {},
      async request() {},
      async sendToSpot(target, message, options) {
        sends.push({ target, message, options });
      },
      async requestToSpot(target, request, options) {
        requests.push({ target, request, options });
        if (requests.length === 1) {
          throw new framework.ZLinkFrameworkException(
            framework.ZLinkFrameworkErrorKind.SpotRouteNotFound,
            'stale spot route'
          );
        }
        return { owner: String(target.targetNodeRid) };
      }
    },
    (meshName) => `${meshName}.route`
  );

  client.sendToSpot(handle, typedPacket('Notice', { id: 1 })).submit();
  await new Promise((resolve) => setImmediate(resolve));
  const reply = await client
    .requestToSpot(handle, typedPacket('Lookup', { id: 2 }))
    .timeout(250)
    .submit();

  assert.equal(sends.length, 1);
  assert.equal(String(sends[0].target.targetNodeRid), 'node-old');
  assert.equal(sends[0].target.routerChannelId, 'play.route');
  assert.equal(sends[0].options.packetName, 'Notice');
  assert.deepEqual(requests.map((entry) => String(entry.target.targetNodeRid)), ['node-old', 'node-new']);
  assert.deepEqual(requests.map((entry) => entry.target.routerChannelId), ['play.route', 'play.route']);
  assert.deepEqual(requests.map((entry) => entry.options.timeoutMs), [250, 250]);
  assert.deepEqual(reply, { owner: 'node-new' });
  assert.equal(refreshCount, 1);
});

test('ZLinkRouteClient does not refresh or retry an uncertain Spot request failure', async () => {
  const target = {
    meshName: 'play.route',
    nodeRid: 'node-a',
    spotRid: 'spot-1',
    spotKind: framework.ZLinkSpotKind.User
  };
  let refreshCount = 0;
  let requestCount = 0;
  const handle = framework.createSpotHandle('spot-1', target, async () => {
    refreshCount += 1;
    return target;
  });
  const client = new framework.DefaultZLinkRouteClient(
    framework.createFrameworkRegistration(),
    {
      submit() {},
      async request() {},
      async sendToSpot() {},
      async requestToSpot() {
        requestCount += 1;
        throw new framework.ZLinkFrameworkException(
          framework.ZLinkFrameworkErrorKind.RouteNotConnected,
          'delivery outcome is uncertain'
        );
      }
    }
  );

  await assert.rejects(
    () => client.requestToSpot(handle, typedPacket('Lookup', { id: 3 })).submit(),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.RouteNotConnected
  );
  assert.equal(requestCount, 1);
  assert.equal(refreshCount, 0);
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
    .timeout(250)
    .submit();

  assert.deepEqual(reply, { ok: true });
  assert.deepEqual(calls, [
    { channelName: 'api', packetName: undefined, request: { id: 7 }, timeoutMs: 250 }
  ]);
});

test('ZLinkChannelClient applies registration request timeout when call timeout is omitted', async () => {
  const calls = [];
  const registration = framework.createFrameworkRegistration({
    requestTimeoutMs: 7000,
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
    .submit();

  assert.deepEqual(reply, { ok: true });
  assert.deepEqual(calls, [
    { channelName: 'api', packetName: undefined, request: { id: 7 }, timeoutMs: 7000 }
  ]);
});

test('ZLinkChannelClient applies channel request timeout before registration default', async () => {
  const calls = [];
  const registration = framework.createFrameworkRegistration({
    requestTimeoutMs: 7000,
    channels: {
      api: {
        requestTimeoutMs: 2000,
        client: { manualConnections: ['inproc://api'] }
      }
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
    .submit();

  assert.deepEqual(reply, { ok: true });
  assert.deepEqual(calls, [
    { channelName: 'api', packetName: undefined, request: { id: 7 }, timeoutMs: 2000 }
  ]);
});

test('ZLinkRouteClient applies route channel request timeout before registration default', async () => {
  const calls = [];
  const registration = framework.createFrameworkRegistration({
    requestTimeoutMs: 7000,
    routeChannels: [
      {
        routerChannelId: 'route',
        requestTimeoutMs: 3000,
        manualConnections: ['inproc://route']
      }
    ]
  });
  const client = new framework.DefaultZLinkRouteClient(registration, {
    submit() {},
    async request(routerChannelId, targetNodeRid, packetName, request, timeoutMs) {
      calls.push({ routerChannelId, targetNodeRid, packetName, request, timeoutMs });
      return { ok: true };
    }
  });

  const reply = await client
    .requestToNode('route', 'target', { id: 7 })
    .submit();

  assert.deepEqual(reply, { ok: true });
  assert.deepEqual(calls, [
    {
      routerChannelId: 'route',
      targetNodeRid: 'target',
      packetName: undefined,
      request: { id: 7 },
      timeoutMs: 3000
    }
  ]);
  assert.equal('yield' in client.requestToNode('route', 'target', { id: 8 }), true);
});

test('route packet dispatcher sends channel envelopes to route handlers before Spot bridge fallback', async () => {
  let bridgeCalls = 0;
  const handledPayloads = [];
  const replyParts = [];
  const dispatcher = new framework.ZLinkRoutePacketDispatcher({
    routerChannelId: 'bingo.play',
    dispatchErrors: noDispatchErrorReporter(),
    filters: [],
    spotRouteBridge: {
      handleRouterReceived() {
        bridgeCalls += 1;
        return true;
      }
    },
    handlers: [{
      kind: 'request',
      packetName: 'AllocateBingoRoomReq',
      handler: {
        handle(payload, context) {
          handledPayloads.push({ payload, sourceNodeRid: String(context.sourceNodeRid) });
          return { roomId: 'room-1' };
        }
      }
    }]
  });

  const parts = [
    zlink.Message.from(Buffer.from(JSON.stringify({
      formatMarker: 0xf2,
      flowId: '018f2b63-9d4a-7abc-8def-0123456789ab',
      flowOrigin: 1,
      kind: 1,
      channelName: 'bingo.play',
      messageName: 'AllocateBingoRoomReq',
      contentType: 'application/json',
      correlationId: 'route-corr',
      deadline: null,
      topic: null,
      errorCode: null,
      errorMessage: null
    }))),
    zlink.Message.from(Buffer.from(JSON.stringify({ mode: 'two-player' })))
  ];
  const router = {
    reply(routingId, requestSeq) {
      assert.equal(String(routingId), 'api-node');
      assert.equal(requestSeq, 7n);
      return {
        message(message) {
          replyParts.push(message);
          return this;
        },
        submit() {}
      };
    }
  };

  try {
    await dispatcher.dispatch({
      parts,
      routingId: 'api-node',
      requestSeq: 7n
    }, router);

    assert.equal(bridgeCalls, 0);
    assert.deepEqual(handledPayloads, [{
      payload: { mode: 'two-player' },
      sourceNodeRid: 'api-node'
    }]);
    assert.equal(replyParts.length, 2);
  } finally {
    parts.forEach((part) => part.close());
    replyParts.forEach((part) => part.close?.());
  }
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

  assertAbortedSync(() => client.sendToChannel('api', 'hello').submit(controller.signal));
  await assertAborted(() => client.requestToChannel('api', typedPacket('Ping', 'ping')).submit(controller.signal));
  assertAbortedSync(() => fanout.publish('events', 'topic', 'event').submit(controller.signal));
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

  assertAbortedSync(() => transport.send('api', 'Greeting', 'hello', controller.signal));
  await assertAborted(() => transport.request('api', 'Ping', 'ping', 250, controller.signal));
  assertAbortedSync(() => transport.publish('events', 'topic', 'Event', 'event', controller.signal));
  assert.deepEqual(calls, []);
});

test('ZLinkDealerChannelClientTransport maps native request connectivity failures to public route error', async () => {
  const nativeError = Object.assign(new Error('dealerRequest failed: Connection refused'), { code: 5 });
  const transport = new framework.ZLinkDealerChannelClientTransport({
    request() {
      return createMultipartRequestOperation({
        submit() {
          throw nativeError;
        }
      });
    }
  });

  await assert.rejects(
    () => transport.request('api', 'Ping', 'ping', 100),
    (error) => error instanceof framework.ZLinkFrameworkException &&
      error.kind === framework.ZLinkFrameworkErrorKind.RouteNotConnected &&
      error.isRetriable === true &&
      error.cause === nativeError
  );
});

test('ZLinkModule.forRoot provides concrete channel and fanout clients', () => {
  const module = nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
    .addClientServerChannel('api')
      .enableClient('inproc://api')
    .addFanoutChannel('events')
      .enablePublisher('inproc://pub')
    .build());
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

    client.sendToChannel('api', typedPacket('Greeting', 'hello')).submit();

    const received = new zlink.Received();
    try {
      assert.equal(router.recv(received), true);
      const envelope = decodeDotnetEnvelope(received.parts);
      assert.equal(envelope.header.kind, 3);
      assert.equal(envelope.header.channelName, 'api');
      assert.equal(envelope.header.messageName, 'Greeting');
      assert.equal(envelope.body, 'hello');
    } finally {
      received.close();
    }
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

    const replyPromise = client.requestToChannel('api', typedPacket('Ping', { value: 'ping' })).timeout(1000).submit();
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

test('route raw SPOT requests through SpotNode router are serialized per route channel', async () => {
  let active = 0;
  let maxActive = 0;
  const calls = [];
  const releases = [];
  const fakeSpot = {
    requestToSpot(targetNodeRid, targetSpot, request, callback) {
      active += 1;
      maxActive = Math.max(maxActive, active);
      calls.push({
        targetNodeRid,
        spotRid: targetSpot,
        request: request.data().toString()
      });
      releases.push(() => {
        active -= 1;
        callback(0, [zlink.Message.from(Buffer.from(`reply-${calls.length}`))]);
      });
      return true;
    }
  };
  const registration = framework.createFrameworkRegistration({
    routeChannels: [{ routerChannelId: 'room.route' }],
    spotNodes: {
      play: {
        router: { bind: 'inproc://play', routingId: 'play-node' }
      }
    }
  });
  const manager = new framework.ZLinkChannelRuntimeManager(
    registration,
    fakeChannelAdapter({ dealer: fakeBackpressuredDealer() }),
    fakeContext()
  );
  manager.setSpotNodes(new Map([
    ['play', {
      entrySpot() {
        return fakeSpot;
      }
    }]
  ]));
  const remoteAddress = {
    routerChannelId: 'room.route',
    targetNodeRid: 'session-node',
    spotRid: 'session-node',
    spotKind: framework.ZLinkSpotKind.Entry
  };

  const first = manager.routeRequestRawToSpot(
    remoteAddress,
    zlink.Message.from(Buffer.from('first')),
    1000
  );
  const second = manager.routeRequestRawToSpot(
    remoteAddress,
    zlink.Message.from(Buffer.from('second')),
    1000
  );

  await waitUntil(() => calls.length === 1);
  assert.equal(active, 1);
  assert.equal(calls[0].request, 'first');
  releases.shift()();
  await waitUntil(() => calls.length === 2);
  assert.equal(active, 1);
  assert.equal(calls[1].request, 'second');
  releases.shift()();

  const [firstReply, secondReply] = await Promise.all([first, second]);
  assert.equal(firstReply[0].data().toString(), 'reply-1');
  assert.equal(secondReply[0].data().toString(), 'reply-2');
  firstReply[0].close();
  secondReply[0].close();
  assert.equal(maxActive, 1);
});

test('aborted raw SPOT request retains the physical route slot until its late reply arrives', async () => {
  const calls = [];
  const releases = [];
  const fakeSpot = {
    requestToSpot(_targetNodeRid, _targetSpot, request, callback) {
      calls.push(request.data().toString());
      releases.push(callback);
      return true;
    }
  };
  const manager = new framework.ZLinkChannelRuntimeManager(
    framework.createFrameworkRegistration({
      routeChannels: [{ routerChannelId: 'room.route' }],
      spotNodes: { play: { router: { bind: 'inproc://play', routingId: 'play-node' } } }
    }),
    fakeChannelAdapter({ dealer: fakeBackpressuredDealer() }),
    fakeContext()
  );
  manager.setSpotNodes(new Map([['play', { entrySpot() { return fakeSpot; } }]]));
  const address = {
    routerChannelId: 'room.route',
    targetNodeRid: 'session-node',
    spotRid: 'session-node',
    spotKind: framework.ZLinkSpotKind.Entry
  };
  const abort = new AbortController();
  const first = manager.routeRequestRawToSpot(
    address,
    zlink.Message.from(Buffer.from('first')),
    1000,
    abort.signal
  );
  const second = manager.routeRequestRawToSpot(
    address,
    zlink.Message.from(Buffer.from('second')),
    1000
  );

  await waitUntil(() => calls.length === 1);
  abort.abort();
  await assert.rejects(first, /aborted/i);
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(calls, ['first']);

  let lateReplyClosed = 0;
  const lateReply = { close() { lateReplyClosed++; } };
  releases.shift()(0, [lateReply]);
  await waitUntil(() => calls.length === 2);
  assert.equal(lateReplyClosed, 1);
  releases.shift()(0, [zlink.Message.from(Buffer.from('second-reply'))]);
  const secondReply = await second;
  assert.equal(secondReply[0].data().toString(), 'second-reply');
  secondReply[0].close();
});

test('timed-out raw SPOT request closes its late reply before releasing the route slot', async () => {
  const calls = [];
  const releases = [];
  const fakeSpot = {
    requestToSpot(_targetNodeRid, _targetSpot, request, callback) {
      calls.push(request.data().toString());
      releases.push(callback);
      return true;
    }
  };
  const manager = new framework.ZLinkChannelRuntimeManager(
    framework.createFrameworkRegistration({
      requestTimeoutMs: 20,
      routeChannels: [{ routerChannelId: 'room.route' }],
      spotNodes: { play: { router: { bind: 'inproc://play', routingId: 'play-node' } } }
    }),
    fakeChannelAdapter({ dealer: fakeBackpressuredDealer() }),
    fakeContext()
  );
  manager.setSpotNodes(new Map([['play', { entrySpot() { return fakeSpot; } }]]));
  const address = {
    routerChannelId: 'room.route',
    targetNodeRid: 'session-node',
    spotRid: 'session-node',
    spotKind: framework.ZLinkSpotKind.Entry
  };
  const first = manager.routeRequestRawToSpot(address, zlink.Message.from(Buffer.from('first')));
  const second = manager.routeRequestRawToSpot(address, zlink.Message.from(Buffer.from('second')), 1000);

  await assert.rejects(first, /not ready|timed out/i);
  assert.deepEqual(calls, ['first']);
  let lateClosed = 0;
  releases.shift()(0, [{ close() { lateClosed++; } }]);
  await waitUntil(() => calls.length === 2);
  assert.equal(lateClosed, 1);
  releases.shift()(0, [zlink.Message.from(Buffer.from('second-reply'))]);
  const reply = await second;
  reply[0].close();
});

test('SpotNode router is not classified as packet route channel', () => {
  const registration = framework.createFrameworkRegistration({
    routeChannels: [{ routerChannelId: 'play-node' }],
    spotNodes: {
      'play-node': {
        router: { bind: 'inproc://play-node', routingId: 'play-node' }
      }
    }
  });
  const manager = new framework.ZLinkChannelRuntimeManager(
    registration,
    fakeChannelAdapter({ dealer: fakeBackpressuredDealer() }),
    fakeContext()
  );
  manager.setSpotNodes(new Map([
    ['play-node', {
      entrySpot() {
        return { routingId: 'play-node' };
      }
    }]
  ]));

  assert.equal(manager.canRouteChannel('play-node'), true);
  assert.equal(manager.canRoutePacketChannel('play-node'), false);
});

test('route raw SPOT request through SpotNode router retries until route is ready', async () => {
  let attempts = 0;
  const fakeSpot = {
    requestToSpot(_targetNodeRid, _targetSpot, _request, callback) {
      attempts += 1;
      if (attempts === 1) {
        return false;
      }
      callback(0, [zlink.Message.from(Buffer.from('ready-reply'))]);
      return true;
    }
  };
  const manager = new framework.ZLinkChannelRuntimeManager(
    framework.createFrameworkRegistration({
      requestTimeoutMs: 1000,
      routeChannels: [{ routerChannelId: 'room.route' }],
      spotNodes: {
        play: {
          router: { bind: 'inproc://play', routingId: 'play-node' }
        }
      }
    }),
    fakeChannelAdapter({ dealer: fakeBackpressuredDealer() }),
    fakeContext()
  );
  manager.setSpotNodes(new Map([
    ['play', {
      entrySpot() {
        return fakeSpot;
      }
    }]
  ]));

  const reply = await manager.routeRequestRawToSpot(
    {
      routerChannelId: 'room.route',
      targetNodeRid: 'session-node',
      spotRid: 'session-node',
      spotKind: framework.ZLinkSpotKind.Entry
    },
    zlink.Message.from(Buffer.from('request')),
    1000
  );

  assert.equal(attempts, 2);
  assert.equal(reply[0].data().toString(), 'ready-reply');
  reply[0].close();
});

test('route raw SPOT request from a user spot waits for replacement route readiness', async () => {
  let attempts = 0;
  const sourceSpot = {
    requestToSpot(_targetNodeRid, _targetSpot, _request, callback) {
      attempts += 1;
      if (attempts === 1) return false;
      callback(0, [zlink.Message.from(Buffer.from('replacement-ready'))]);
      return true;
    }
  };
  const manager = new framework.ZLinkChannelRuntimeManager(
    framework.createFrameworkRegistration({ requestTimeoutMs: 1000 }),
    fakeChannelAdapter({ dealer: fakeBackpressuredDealer() }),
    fakeContext()
  );

  const reply = await manager.routeRequestRawFromSpotToSpot(
    sourceSpot,
    {
      routerChannelId: 'room.route',
      targetNodeRid: 'replacement-node',
      spotRid: 'replacement-room',
      spotKind: framework.ZLinkSpotKind.User
    },
    zlink.Message.from(Buffer.from('request')),
    1000
  );

  assert.equal(attempts, 2);
  assert.equal(reply[0].data().toString(), 'replacement-ready');
  reply[0].close();
});

test('route raw SPOT request stops SpotNode readiness retries when aborted', async () => {
  let attempts = 0;
  const manager = new framework.ZLinkChannelRuntimeManager(
    framework.createFrameworkRegistration({
      requestTimeoutMs: 1000,
      routeChannels: [{ routerChannelId: 'room.route' }],
      spotNodes: {
        play: { router: { bind: 'inproc://play', routingId: 'play-node' } }
      }
    }),
    fakeChannelAdapter({ dealer: fakeBackpressuredDealer() }),
    fakeContext()
  );
  manager.setSpotNodes(new Map([['play', {
    entrySpot() {
      return {
        requestToSpot() {
          attempts += 1;
          return false;
        }
      };
    }
  }]]));
  const abort = new AbortController();
  const request = zlink.Message.from(Buffer.from('request'));
  const pending = manager.routeRequestRawToSpot({
    routerChannelId: 'room.route',
    targetNodeRid: 'session-node',
    spotRid: 'session-node',
    spotKind: framework.ZLinkSpotKind.Entry
  }, request, 1000, abort.signal);

  await waitUntil(() => attempts > 0);
  abort.abort();
  await assert.rejects(pending, /The operation was aborted/);
  const attemptsAtAbort = attempts;
  await new Promise((resolve) => setTimeout(resolve, 30));
  assert.equal(attempts, attemptsAtAbort);
  request.close();
});

test('source Spot raw request aborts after native submission and closes a late reply', async () => {
  let complete;
  const sourceSpot = {
    requestToSpot(_targetNodeRid, _spotRid, _request, callback) {
      complete = callback;
      return true;
    }
  };
  const manager = new framework.ZLinkChannelRuntimeManager(
    framework.createFrameworkRegistration(),
    fakeChannelAdapter({ dealer: fakeBackpressuredDealer() }),
    fakeContext()
  );
  const abort = new AbortController();
  const request = zlink.Message.from(Buffer.from('request'));
  const pending = manager.routeRequestRawFromSpotToSpot(sourceSpot, {
    routerChannelId: 'room.route',
    targetNodeRid: 'play-node',
    spotRid: 'room-1'
  }, request, 1000, abort.signal);

  abort.abort();
  await assert.rejects(pending, /The operation was aborted/);
  let closeCount = 0;
  const lateReply = { close() { closeCount += 1; } };
  complete(0, [lateReply]);
  assert.equal(closeCount, 1);
  request.close();
});

test('route channel request rejects when native router does not accept submit', async () => {
  const router = fakeRouteRouter();
  const manager = new framework.ZLinkChannelRuntimeManager(
    framework.createFrameworkRegistration({
      routeChannels: [{
        routerChannelId: 'room.route',
        bind: 'inproc://session-route',
        routingId: 'session-node'
      }]
    }),
    fakeChannelAdapter({ dealer: fakeBackpressuredDealer(), router }),
    fakeContext()
  );

  await assert.rejects(
    () => manager.routeRequest('room.route', 'play-node', 'EnsurePlayerActorReq', { actorId: 'player-1' }, 100),
    /Route channel 'room\.route' is not ready for request/
  );
  assert.equal(router.requestAttempts, 1);
});

test('route channel request uses call timeout after native router accepts submit', async () => {
  const router = fakeRouteRouter({ acceptWithoutReply: true });
  const manager = new framework.ZLinkChannelRuntimeManager(
    framework.createFrameworkRegistration({
      routeChannels: [{
        routerChannelId: 'room.route',
        bind: 'inproc://session-route',
        routingId: 'session-node'
      }]
    }),
    fakeChannelAdapter({ dealer: fakeBackpressuredDealer(), router }),
    fakeContext()
  );

  const startedAt = Date.now();
  await assert.rejects(
    () => manager.routeRequest('room.route', 'play-node', 'EnsurePlayerActorReq', { actorId: 'player-1' }, 30),
    (error) => error instanceof framework.ZLinkFrameworkException
      && error.kind === framework.ZLinkFrameworkErrorKind.RequestFailed
      && /ZLink async submit timed out/.test(error.message)
  );
  assert.equal(router.requestAttempts, 1);
  assert.ok(Date.now() - startedAt < 1000);
});

test('route channel with SPOT acceptance starts one route receive loop for shared router frames', async () => {
  const router = fakeRouteRouter();
  const taskNames = [];
  const manager = new framework.ZLinkChannelRuntimeManager(
    framework.createFrameworkRegistration({
      routeChannels: [{
        routerChannelId: 'room.route',
        bind: 'inproc://play-route',
        routingId: 'play-node'
      }],
      spotNodes: {
        play: {
          router: { bind: 'inproc://play-spot', routingId: 'play-node' }
        }
      }
    }),
    fakeChannelAdapter({ dealer: fakeBackpressuredDealer(), router }),
    fakeContext()
  );
  const spotNode = {
    createRouteBridge() {
      return fakeSpotRouteBridge();
    },
    entrySpot() {
      return {
        requestToSpot() {
          throw new Error('spot request not used');
        }
      };
    }
  };
  manager.setSpotNodes(new Map([['play', spotNode]]));

  manager.start({
    errorSink: { reportRuntimeTaskException() {} },
    run(name) {
      taskNames.push(name);
      return Promise.resolve();
    }
  });

  assert.deepEqual(taskNames, ['route:room.route']);
  await manager.dispose();
});

test('route bridge raw request completes when shared route receive loop observes raw reply', async () => {
  const router = fakeRouteRouter();
  const bridge = {
    attachRouterChannel() {},
    request() {
      return {
        message() {
          return this;
        },
        timeout() {
          return this;
        },
        submit() {
          return true;
        }
      };
    },
    handleRouterReceived() {
      return false;
    },
    async dispose() {}
  };
  const manager = new framework.ZLinkChannelRuntimeManager(
    framework.createFrameworkRegistration({
      routeChannels: [{
        routerChannelId: 'room.route',
        bind: 'inproc://play-route',
        routingId: 'play-node'
      }],
      spotNodes: {
        play: {
          router: { bind: 'inproc://play-spot', routingId: 'play-node' }
        }
      }
    }),
    fakeChannelAdapter({ dealer: fakeBackpressuredDealer(), router }),
    fakeContext()
  );
  manager.setSpotNodes(new Map([['play', {
    createRouteBridge() {
      return bridge;
    },
    entrySpot() {
      return {
        requestToSpot() {
          throw new Error('spot request not used');
        }
      };
    }
  }]]));

  let stopLoop = false;
  manager.start({
    errorSink: { reportRuntimeTaskException() {} },
    run(_name, task) {
      void task({
        get aborted() {
          return stopLoop;
        }
      });
      return Promise.resolve();
    }
  });

  const request = manager.routeRequestRawToSpot({
    routerChannelId: 'room.route',
    targetNodeRid: 'play-node',
    spotRid: 'room-1'
  }, zlink.Message.from(Buffer.from('request')), 100);
  await new Promise((resolve) => setImmediate(resolve));
  router.recvQueue.push({
    parts: [zlink.Message.from(Buffer.from(JSON.stringify({ ok: true, response: { value: 'reply' } })))],
    routingId: 'play-node',
    spotRid: null,
    requestSeq: null,
    close() {
      this.parts.forEach((part) => part.close());
    }
  });
  await new Promise((resolve) => setImmediate(resolve));

  const reply = await request;
  assert.deepEqual(JSON.parse(reply[0].data().toString()), { ok: true, response: { value: 'reply' } });
  reply[0].close();
  stopLoop = true;
  await manager.dispose();
});

test('channel runtime dispose rejects pending raw Spot route bridge requests', async () => {
  const bridge = {
    attachRouterChannel() {},
    request() {
      return {
        message() { return this; },
        timeout() { return this; },
        submit() { return true; }
      };
    },
    handleRouterReceived() { return false; },
    async dispose() {}
  };
  const manager = new framework.ZLinkChannelRuntimeManager(
    framework.createFrameworkRegistration({
      routeChannels: [{ routerChannelId: 'room.route' }],
      spotNodes: { play: { router: { bind: 'inproc://play', routingId: 'play-node' } } }
    }),
    fakeChannelAdapter({ dealer: fakeBackpressuredDealer(), router: fakeRouteRouter() }),
    fakeContext()
  );
  manager.setSpotNodes(new Map([['play', {
    createRouteBridge() { return bridge; },
    entrySpot() { return { requestToSpot() { return false; } }; }
  }]]));
  manager.start({
    errorSink: { reportRuntimeTaskException() {} },
    run() { return Promise.resolve(); }
  });
  const request = zlink.Message.from(Buffer.from('request'));
  const pending = manager.routeRequestRawToSpot({
    routerChannelId: 'room.route',
    targetNodeRid: 'play-node',
    spotRid: 'room-1'
  }, request, undefined);

  await new Promise((resolve) => setImmediate(resolve));
  const rejected = assert.rejects(pending, /Channel runtime disposed/);
  await manager.dispose();
  await rejected;
  request.close();
});

test('route bridge queued sends keep each target on its submitted operation', async () => {
  const router = fakeRouteRouter();
  let bridgeWritable = false;
  const submittedTargets = [];
  const bridge = {
    attachRouterChannel() {},
    send(_channelName, targetNodeRid) {
      return {
        message() {
          return this;
        },
        flags() {
          return this;
        },
        submit() {
          if (!bridgeWritable) {
            return false;
          }
          submittedTargets.push(String(targetNodeRid));
          return true;
        }
      };
    },
    request() {
      throw new Error('request not used');
    },
    handleRouterReceived() {
      return false;
    },
    async dispose() {}
  };
  const manager = new framework.ZLinkChannelRuntimeManager(
    framework.createFrameworkRegistration({
      routeChannels: [{
        routerChannelId: 'room.route',
        bind: 'inproc://play-route',
        routingId: 'play-node'
      }],
      spotNodes: {
        play: {
          router: { bind: 'inproc://play-spot', routingId: 'play-node' }
        }
      }
    }),
    fakeChannelAdapter({ dealer: fakeBackpressuredDealer(), router }),
    fakeContext()
  );
  manager.setSpotNodes(new Map([['play', {
    createRouteBridge() {
      return bridge;
    },
    entrySpot() {
      return {
        requestToSpot() {
          throw new Error('spot request not used');
        }
      };
    }
  }]]));

  manager.start({
    errorSink: { reportRuntimeTaskException() {} },
    run() {
      return Promise.resolve();
    }
  });

  const first = manager.routeSendToSpot({
    routerChannelId: 'room.route',
    targetNodeRid: 'session-node-a',
    spotRid: 'session-node-a'
  }, 'Notify', { value: 1 });
  const second = manager.routeSendToSpot({
    routerChannelId: 'room.route',
    targetNodeRid: 'session-node-b',
    spotRid: 'session-node-b'
  }, 'Notify', { value: 2 });

  await new Promise((resolve) => setImmediate(resolve));
  bridgeWritable = true;
  router.ready();
  await Promise.all([first, second]);

  assert.deepEqual(submittedTargets, ['session-node-a', 'session-node-b']);
  await manager.dispose();
});

test('route raw SPOT request uses route bridge before SpotNode router fallback', async () => {
  const router = fakeRouteRouter();
  const bridgeCalls = [];
  let spotNodeRouterUsed = false;
  const bridge = {
    attachRouterChannel(channelName, socket, options) {
      bridgeCalls.push({ kind: 'attach', channelName, socket, options });
    },
    request(channelName, targetNodeRid, spotRid) {
      const call = { kind: 'request', channelName, targetNodeRid, spotRid };
      bridgeCalls.push(call);
      return {
        message(message) {
          call.message = message.data().toString();
          return this;
        },
        timeout(timeoutMs) {
          call.timeoutMs = timeoutMs;
          return this;
        },
        submit(callback) {
          call.submitted = true;
          callback(0, [zlink.Message.from(Buffer.from('bridge-reply'))]);
          return true;
        }
      };
    },
    handleRouterReceived() {
      return false;
    },
    async dispose() {}
  };
  const manager = new framework.ZLinkChannelRuntimeManager(
    framework.createFrameworkRegistration({
      routeChannels: [{
        routerChannelId: 'room.route',
        bind: 'inproc://play-route',
        routingId: 'play-node'
      }],
      spotNodes: {
        play: {
          router: { bind: 'inproc://play-spot', routingId: 'play-node' }
        }
      }
    }),
    fakeChannelAdapter({ dealer: fakeBackpressuredDealer(), router }),
    fakeContext()
  );
  const spotNode = {
    routingId: 'play-node',
    createRouteBridge() {
      return bridge;
    },
    entrySpot() {
      return {
        requestToSpot() {
          spotNodeRouterUsed = true;
          throw new Error('SpotNode router fallback must not be used when bridge is available');
        }
      };
    }
  };
  manager.setSpotNodes(new Map([['play', spotNode]]));

  manager.start({
    errorSink: { reportRuntimeTaskException() {} },
    run() {
      return Promise.resolve();
    }
  });
  const reply = await manager.routeRequestRawToSpot(
    {
      routerChannelId: 'room.route',
      targetNodeRid: 'session-node',
      spotRid: 'session-node',
      spotKind: framework.ZLinkSpotKind.Entry
    },
    zlink.Message.from(Buffer.from('bridge-request')),
    700
  );

  assert.equal(spotNodeRouterUsed, false);
  assert.equal(router.requestAttempts, 0);
  assert.equal(reply[0].data().toString(), 'bridge-reply');
  reply[0].close();
  assert.equal(bridgeCalls[0].kind, 'attach');
  assert.equal(bridgeCalls[0].channelName, 'room.route');
  assert.deepEqual(bridgeCalls[1], {
    kind: 'request',
    channelName: 'room.route',
    targetNodeRid: 'session-node',
    spotRid: 'session-node',
    message: 'bridge-request',
    timeoutMs: 700,
    submitted: true
  });
  await manager.dispose();
});

test('route raw SPOT request prefers the named Spot mesh when route and Spot mesh names match', async () => {
  let spotRequests = 0;
  const manager = new framework.ZLinkChannelRuntimeManager(
    framework.createFrameworkRegistration({
      routeChannels: [{
        routerChannelId: 'delivery-couriers',
        bind: 'inproc://courier-route',
        routingId: 'courier-node-1'
      }],
      spotNodes: {
        'delivery-couriers': {
          router: { bind: 'inproc://courier-spot', routingId: 'courier-node-1' }
        }
      }
    }),
    fakeChannelAdapter({ dealer: fakeBackpressuredDealer(), router: fakeRouteRouter() }),
    fakeContext()
  );
  manager.setSpotNodes(new Map([['delivery-couriers', {
    routingId: 'courier-node-1',
    createRouteBridge() {
      return {
        ...fakeSpotRouteBridge(),
        request() {
          throw new Error('same-name actor/session traffic must not use the external route bridge');
        }
      };
    },
    entrySpot() {
      return {
        requestToSpot(_targetNodeRid, _spotRid, _request, callback) {
          spotRequests += 1;
          callback(0, [zlink.Message.from(Buffer.from('spot-reply'))]);
          return true;
        }
      };
    }
  }]]));
  manager.start({
    errorSink: { reportRuntimeTaskException() {} },
    run() { return Promise.resolve(); }
  });

  const request = zlink.Message.from(Buffer.from('spot-request'));
  const reply = await manager.routeRequestRawToSpot({
    routerChannelId: 'delivery-couriers',
    targetNodeRid: 'courier-session',
    spotRid: 'courier-session',
    spotKind: framework.ZLinkSpotKind.Entry
  }, request, 700);

  assert.equal(spotRequests, 1);
  assert.equal(reply[0].data().toString(), 'spot-reply');
  reply[0].close();
  request.close();
  await manager.dispose();
});

test('ZLinkChannelClient rejects malformed reply envelope json', async () => {
  const ctx = zlink.createContext();
  const dealer = zlink.createDealerSocket(ctx);
  const router = zlink.createRouterSocket(ctx);
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;

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

    const invalidKindReply = client.requestToChannel('api', typedPacket('Ping', { value: 'ping' })).timeout(1000).submit();
    const invalidKindRequest = await recvRouterMessage(router);
    submitMultipart(
      router.reply(invalidKindRequest.routingId, invalidKindRequest.requestSeq),
      encodeDotnetEnvelope({
        kind: 'response',
        channelName: 'api',
        messageName: 'Ping',
        contentType: 'application/json',
        correlationId: null,
        deadline: null,
        topic: null,
        errorCode: null,
        errorMessage: null
      }, { value: 'pong' })
    );
    await assert.rejects(() => withTimeout(invalidKindReply, 1000, 'invalid reply kind'), /kind is not supported/);
    invalidKindRequest.close();

    const unsafeBodyReply = client.requestToChannel('api', typedPacket('Ping', { value: 'ping' })).timeout(1000).submit();
    const unsafeBodyRequest = await recvRouterMessage(router);
    submitMultipart(
      router.reply(unsafeBodyRequest.routingId, unsafeBodyRequest.requestSeq),
      [
        Buffer.from(JSON.stringify({
          formatMarker: 0xf2,
          flowId: '018f2b63-9d4a-7abc-8def-0123456789ab',
          flowOrigin: 1,
          kind: 2,
          channelName: 'api',
          messageName: 'Ping',
          contentType: 'application/json',
          correlationId: null,
          deadline: null,
          topic: null,
          errorCode: null,
          errorMessage: null
        })),
        Buffer.from('{"__proto__":{"polluted":true}}')
      ]
    );
    await assert.rejects(() => withTimeout(unsafeBodyReply, 1000, 'unsafe reply body'), /not allowed/);
    assert.equal({}.polluted, undefined);
    unsafeBodyRequest.close();
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
  const module = nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
    .addClientServerChannel('api')
      .enableClient(endpoint)
    .build());
  const container = await resolveModuleProviders(module, [
    nestjs.ZLINK_FRAMEWORK_RUNTIME,
    nestjs.ZLINK_CHANNEL_CLIENT
  ]);
  const runtime = container.get(nestjs.ZLINK_FRAMEWORK_RUNTIME);
  const client = container.get(nestjs.ZLINK_CHANNEL_CLIENT);

  try {
    router.bind(endpoint);
    await runtime.start();

    const replyPromise = client.requestToChannel('api', typedPacket('Ping', { value: 'ping' })).timeout(1000).submit();
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

test('CH-001 ZLinkFrameworkRuntimeHost dispatches client-server channel request handlers', async () => {
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
              const reply = { created: payload.gameName };
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
      client.requestToChannel('play', typedPacket('CreateGame', { gameName: 'sample' })).timeout(1000).submit()
    );

    assert.deepEqual(calls, [{ created: 'sample' }]);
    assert.deepEqual(reply, { created: 'sample' });
  } finally {
    await clientRuntime.stop();
    await serverRuntime.stop();
  }
});

test('ZLinkFrameworkRuntimeHost waits for in-flight channel dispatch before closing router', async () => {
  const calls = [];
  let releaseHandler;
  const handlerStarted = new Promise((resolve) => {
    releaseHandler = resolve;
  });
  let handlerCanFinish;
  const handlerRelease = new Promise((resolve) => {
    handlerCanFinish = resolve;
  });
  const router = fakeRuntimeRouter(calls, {
    parts: encodeDotnetEnvelope({
      kind: 1,
      channelName: 'api',
      messageName: 'SlowReq',
      contentType: 'application/json',
      correlationId: 'slow-1',
      deadline: null,
      topic: null,
      errorCode: null,
      errorMessage: null
    }, { value: 'wait' }).map(fakeMessagePart),
    routingId: 'client-node',
    requestSeq: 1n,
    close() {
      calls.push('received:close');
      this.parts.forEach((part) => part.close());
    }
  });
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({
      channels: {
        api: {
          server: { bind: 'inproc://slow-api' },
          requestHandlers: [{
            packetName: 'SlowReq',
            handler: {
              async handle(payload) {
                calls.push(`handler:start:${payload.value}`);
                releaseHandler();
                await handlerRelease;
                calls.push('handler:finish');
                return { ok: true };
              }
            }
          }]
        }
      }
    })
  }, {
    backendAdapterFactory: fakeRuntimeBackendAdapterFactory(calls, router)
  });

  await runtime.start();
  await handlerStarted;
  const stop = runtime.stop();
  await new Promise((resolve) => setImmediate(resolve));

  assert.equal(calls.includes('router:dispose'), false);
  handlerCanFinish();
  await stop;
  assert.deepEqual(calls.filter((call) => call === 'router:reply' || call === 'router:dispose'), [
    'router:reply',
    'router:dispose'
  ]);
});

test('ZLinkFrameworkRuntimeHost applies server socket maxMessageSize', async () => {
  const calls = [];
  const router = fakeRuntimeRouter(calls);
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({
      channels: {
        api: {
          server: {
            bind: 'inproc://max-message-size-api',
            maxMessageSize: 2048
          },
          requestHandlers: [{
            packetName: 'Ping',
            handler: {
              handle() {
                return { ok: true };
              }
            }
          }]
        }
      }
    })
  }, {
    backendAdapterFactory: fakeRuntimeBackendAdapterFactory(calls, router)
  });

  try {
    await runtime.start();
    assert.equal(router.maxMessageSize, 2048);
  } finally {
    await runtime.stop();
  }
});

test('CH-006 ZLinkFrameworkRuntimeHost dispatches client-server send handlers', async () => {
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const calls = [];
  const serverRegistration = framework.createFrameworkRegistration({
    channels: {
      play: {
        server: { bind: endpoint },
        sendHandlers: [{
          packetName: 'RecordCommand',
          handler: {
            handle(payload, context) {
              assert.equal(context.channelName, 'play');
              assert.equal(context.packetName, 'RecordCommand');
              calls.push(payload.gameName);
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
    await submitWhenReachable(() =>
      client.sendToChannel('play', typedPacket('RecordCommand', { gameName: 'sample' })).submit()
    );

    await waitFor(() => calls.length === 1, 'CH-006 channel send handler evidence');
    assert.deepEqual(calls, ['sample']);
  } finally {
    await clientRuntime.stop();
    await serverRuntime.stop();
  }
});

test('DERR-001 ZLinkFrameworkRuntimeHost replies error and reports observer for missing channel request handler', async () => {
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const dispatchEvents = [];
  class DispatchObserver {
    onMessageFlow(event) {
      dispatchEvents.push(event);
    }
  }
  const serverRegistration = framework.createFrameworkRegistration({
    dispatch: dispatchOptions(DispatchObserver),
    channels: {
      play: {
        server: { bind: endpoint },
        requestHandlers: [{
          packetName: 'KnownReq',
          handler: {
            handle(payload, context) {
              assert.equal(context.channelName, 'play');
              assert.equal(context.packetName, 'KnownReq');
              return { value: `known:${payload.value}` };
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

    const knownBefore = await submitWhenReachable(() =>
      client.requestToChannel('play', typedPacket('KnownReq', { value: 'before' })).timeout(1000).submit()
    );
    assert.deepEqual(knownBefore, { value: 'known:before' });

    await assert.rejects(
      () => client.requestToChannel('play', typedPacket('UnknownReq', { value: 'missing' })).timeout(1000).submit(),
      /No channel request handler is registered for 'play:UnknownReq'/
    );

    await waitUntil(() => dispatchEvents.length === 1, 1000);
    assert.equal(dispatchEvents[0].surface, framework.ZLinkDispatchErrorSurface.Channel);
    assert.equal(dispatchEvents[0].messageKind, framework.ZLinkDispatchMessageKind.Request);
    assert.equal(dispatchEvents[0].outcome, framework.ZLinkMessageFlowOutcome.Error);
    assert.equal(dispatchEvents[0].errorReason, framework.ZLinkDispatchErrorReason.HandlerMissing);
    assert.equal(dispatchEvents[0].errorAction, framework.ZLinkDispatchErrorAction.ReplyError);
    assert.equal(dispatchEvents[0].packetName, 'UnknownReq');
    assert.equal(dispatchEvents[0].channelName, 'play');

    const knownAfter = await client
      .requestToChannel('play', typedPacket('KnownReq', { value: 'after' }))
      .timeout(1000)
      .submit();
    assert.deepEqual(knownAfter, { value: 'known:after' });
  } finally {
    await clientRuntime.stop();
    await serverRuntime.stop();
  }
});

test('DERR-002 ZLinkFrameworkRuntimeHost reports observer for missing channel send handler', async () => {
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const dispatchEvents = [];
  class DispatchObserver {
    onMessageFlow(event) {
      dispatchEvents.push(event);
    }
  }
  const serverRegistration = framework.createFrameworkRegistration({
    dispatch: dispatchOptions(DispatchObserver),
    channels: {
      play: {
        server: { bind: endpoint },
        requestHandlers: [{
          packetName: 'KnownReq',
          handler: {
            handle(payload, context) {
              assert.equal(context.channelName, 'play');
              assert.equal(context.packetName, 'KnownReq');
              return { value: `known:${payload.value}` };
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

    const knownBefore = await submitWhenReachable(() =>
      client.requestToChannel('play', typedPacket('KnownReq', { value: 'before-send-error' })).timeout(1000).submit()
    );
    assert.deepEqual(knownBefore, { value: 'known:before-send-error' });

    client.sendToChannel('play', typedPacket('UnknownCommand', { value: 'missing-send' })).submit();

    await waitUntil(() => dispatchEvents.length === 1, 1000);
    assert.equal(dispatchEvents[0].surface, framework.ZLinkDispatchErrorSurface.Channel);
    assert.equal(dispatchEvents[0].messageKind, framework.ZLinkDispatchMessageKind.Send);
    assert.equal(dispatchEvents[0].outcome, framework.ZLinkMessageFlowOutcome.Error);
    assert.equal(dispatchEvents[0].errorReason, framework.ZLinkDispatchErrorReason.HandlerMissing);
    assert.equal(dispatchEvents[0].errorAction, framework.ZLinkDispatchErrorAction.Drop);
    assert.equal(dispatchEvents[0].packetName, 'UnknownCommand');
    assert.equal(dispatchEvents[0].channelName, 'play');

    const knownAfter = await client
      .requestToChannel('play', typedPacket('KnownReq', { value: 'after-send-error' }))
      .timeout(1000)
      .submit();
    assert.deepEqual(knownAfter, { value: 'known:after-send-error' });
  } finally {
    await clientRuntime.stop();
    await serverRuntime.stop();
  }
});

test('REG-003 ZLinkFrameworkRuntimeHost dispatches manual channel handlers and reports missing packets', async () => {
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const fanoutEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const dispatchEvents = [];
  const sendCalls = [];
  const publishCalls = [];
  class DispatchObserver {
    onMessageFlow(event) {
      dispatchEvents.push(event);
    }
  }
  const serverRegistration = framework.createFrameworkRegistration({
    dispatch: dispatchOptions(DispatchObserver),
    channels: {
      'manual-reg': {
        server: { bind: endpoint },
        requestHandlers: [{
          packetName: 'ManualRegisteredReq',
          handler: {
            handle(payload, context) {
              assert.equal(context.channelName, 'manual-reg');
              assert.equal(context.packetName, 'ManualRegisteredReq');
              return { value: `manual:${payload.value}` };
            }
          }
        }],
        sendHandlers: [{
          packetName: 'ManualRegisteredCommand',
          handler: {
            handle(payload, context) {
              sendCalls.push({
                payload,
                channelName: context.channelName,
                packetName: context.packetName
              });
            }
          }
        }]
      }
    }
  });
  const clientRegistration = framework.createFrameworkRegistration({
    channels: {
      'manual-reg': { client: { manualConnections: [endpoint] } }
    }
  });
  const publisherRegistration = framework.createFrameworkRegistration({
    channels: {
      'manual-events': { publisher: { bind: fanoutEndpoint } }
    }
  });
  const subscriberRegistration = framework.createFrameworkRegistration({
    dispatch: dispatchOptions(DispatchObserver),
    channels: {
      'manual-events': {
        subscriber: { manualConnections: [fanoutEndpoint] },
        publishHandlers: [{
          packetName: 'ManualRegisteredEvent',
          handler: {
            handle(payload, context) {
              publishCalls.push({
                payload,
                channelName: context.channelName,
                packetName: context.packetName,
                topic: context.topic
              });
            }
          }
        }]
      }
    }
  });
  const serverRuntime = new framework.ZLinkFrameworkRuntimeHost({ registration: serverRegistration });
  const clientRuntime = new framework.ZLinkFrameworkRuntimeHost({ registration: clientRegistration });
  const publisherRuntime = new framework.ZLinkFrameworkRuntimeHost({ registration: publisherRegistration });
  const subscriberRuntime = new framework.ZLinkFrameworkRuntimeHost({ registration: subscriberRegistration });

  try {
    await serverRuntime.start();
    await clientRuntime.start();
    await publisherRuntime.start();
    await subscriberRuntime.start();
    const client = new framework.DefaultZLinkChannelClient(clientRegistration, clientRuntime.channelTransport);
    const fanout = new framework.DefaultZLinkFanoutClient(publisherRegistration, publisherRuntime.channelTransport);

    const reply = await submitWhenReachable(() =>
      client
        .requestToChannel('manual-reg', typedPacket('ManualRegisteredReq', { value: 'registered' }))
        .timeout(1000)
        .submit()
    );
    assert.deepEqual(reply, { value: 'manual:registered' });

    await client
      .sendToChannel('manual-reg', typedPacket('ManualRegisteredCommand', { value: 'command' }))
      .submit();
    await waitFor(() => sendCalls.length === 1, 'REG-003 manual send handler');
    assert.deepEqual(sendCalls, [{
      payload: { value: 'command' },
      channelName: 'manual-reg',
      packetName: 'ManualRegisteredCommand'
    }]);

    await publishUntilHandled(
      fanout,
      'manual-events',
      'manual',
      'ManualRegisteredEvent',
      { value: 'published' },
      () => publishCalls.length === 1
    );
    assert.deepEqual(publishCalls, [{
      payload: { value: 'published' },
      channelName: 'manual-events',
      packetName: 'ManualRegisteredEvent',
      topic: 'manual'
    }]);

    await assert.rejects(
      () => client
        .requestToChannel('manual-reg', typedPacket('ManualMissingReq', { value: 'missing' }))
        .timeout(1000)
        .submit(),
      (error) => error instanceof framework.ZLinkFrameworkException
        && error.kind === framework.ZLinkFrameworkErrorKind.HandlerNotFound
        && /No channel request handler is registered for 'manual-reg:ManualMissingReq'/.test(error.message)
    );

    await client
      .sendToChannel('manual-reg', typedPacket('ManualMissingCommand', { value: 'missing-command' }))
      .submit();

    await publishUntilHandled(
      fanout,
      'manual-events',
      'manual',
      'ManualMissingEvent',
      { value: 'missing-event' },
      () => hasDispatchEvent(
        dispatchEvents,
        framework.ZLinkDispatchMessageKind.Publish,
        framework.ZLinkDispatchErrorReason.HandlerMissing,
        framework.ZLinkDispatchErrorAction.Drop,
        'ManualMissingEvent',
        'manual-events'
      )
    );

    await waitUntil(() =>
      hasDispatchEvent(
        dispatchEvents,
        framework.ZLinkDispatchMessageKind.Request,
        framework.ZLinkDispatchErrorReason.HandlerMissing,
        framework.ZLinkDispatchErrorAction.ReplyError,
        'ManualMissingReq',
        'manual-reg'
      ) &&
      hasDispatchEvent(
        dispatchEvents,
        framework.ZLinkDispatchMessageKind.Send,
        framework.ZLinkDispatchErrorReason.HandlerMissing,
        framework.ZLinkDispatchErrorAction.Drop,
        'ManualMissingCommand',
        'manual-reg'
      ) &&
      hasDispatchEvent(
        dispatchEvents,
        framework.ZLinkDispatchMessageKind.Publish,
        framework.ZLinkDispatchErrorReason.HandlerMissing,
        framework.ZLinkDispatchErrorAction.Drop,
        'ManualMissingEvent',
        'manual-events'
      ),
    1000);
  } finally {
    await subscriberRuntime.stop();
    await publisherRuntime.stop();
    await clientRuntime.stop();
    await serverRuntime.stop();
  }
});

test('DERR-007 ZLinkFrameworkRuntimeHost replies error and reports observer for handler exception', async () => {
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const dispatchEvents = [];
  class DispatchObserver {
    onMessageFlow(event) {
      dispatchEvents.push(event);
    }
  }
  const serverRegistration = framework.createFrameworkRegistration({
    dispatch: dispatchOptions(DispatchObserver),
    channels: {
      play: {
        server: { bind: endpoint },
        requestHandlers: [{
          packetName: 'KnownReq',
          handler: {
            handle(payload, context) {
              assert.equal(context.channelName, 'play');
              assert.equal(context.packetName, 'KnownReq');
              return { value: `known:${payload.value}` };
            }
          }
        }, {
          packetName: 'ThrowReq',
          handler: {
            handle() {
              throw new Error('DERR-007 handler exception');
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

    const knownBefore = await submitWhenReachable(() =>
      client.requestToChannel('play', typedPacket('KnownReq', { value: 'before-handler-error' })).timeout(1000).submit()
    );
    assert.deepEqual(knownBefore, { value: 'known:before-handler-error' });

    await assert.rejects(
      () => client.requestToChannel('play', typedPacket('ThrowReq', { value: 'boom' })).timeout(1000).submit(),
      /DERR-007 handler exception/
    );

    await waitUntil(() => dispatchEvents.length === 1, 1000);
    assert.equal(dispatchEvents[0].surface, framework.ZLinkDispatchErrorSurface.Channel);
    assert.equal(dispatchEvents[0].messageKind, framework.ZLinkDispatchMessageKind.Request);
    assert.equal(dispatchEvents[0].outcome, framework.ZLinkMessageFlowOutcome.Error);
    assert.equal(dispatchEvents[0].errorReason, framework.ZLinkDispatchErrorReason.HandlerException);
    assert.equal(dispatchEvents[0].errorAction, framework.ZLinkDispatchErrorAction.ReplyError);
    assert.equal(dispatchEvents[0].packetName, 'ThrowReq');
    assert.equal(dispatchEvents[0].channelName, 'play');
    assert.equal(dispatchEvents[0].errorType, 'Error');
    assert.equal(dispatchEvents[0].errorMessage, 'DERR-007 handler exception');

    const knownAfter = await client
      .requestToChannel('play', typedPacket('KnownReq', { value: 'after-handler-error' }))
      .timeout(1000)
      .submit();
    assert.deepEqual(knownAfter, { value: 'known:after-handler-error' });
  } finally {
    await clientRuntime.stop();
    await serverRuntime.stop();
  }
});

test('CH-002 manual endpoint round-robin distributes requests across three servers', async () => {
  const endpoints = [
    `tcp://127.0.0.1:${await reservePort()}`,
    `tcp://127.0.0.1:${await reservePort()}`,
    `tcp://127.0.0.1:${await reservePort()}`
  ];
  const servers = [
    createRoundRobinServer(endpoints[0], 'server-a'),
    createRoundRobinServer(endpoints[1], 'server-b'),
    createRoundRobinServer(endpoints[2], 'server-c')
  ];
  const clientRegistration = framework.createFrameworkRegistration({
    channels: {
      'round-robin': { client: { manualConnections: endpoints } }
    }
  });
  const clientRuntime = new framework.ZLinkFrameworkRuntimeHost({ registration: clientRegistration });

  try {
    for (const server of servers) {
      await server.runtime.start();
    }
    await clientRuntime.start();
    const client = new framework.DefaultZLinkChannelClient(clientRegistration, clientRuntime.channelTransport);
    const seenServers = new Set();
    for (let i = 0; i < 30 && seenServers.size < 3; i++) {
      const reply = await submitWhenReachable(() =>
        client
          .requestToChannel('round-robin', typedPacket('RoundRobinProbe', { requestId: `warmup-${i}` }))
          .timeout(1000)
          .submit()
      );
      seenServers.add(reply.serverId);
    }
    assert.deepEqual([...seenServers].sort(), ['server-a', 'server-b', 'server-c']);

    const replies = [];
    for (let i = 0; i < 90; i++) {
      replies.push(await client
        .requestToChannel('round-robin', typedPacket('RoundRobinProbe', { requestId: `verify-${i}` }))
        .timeout(1000)
        .submit());
    }

    assertRoundRobinCounts(countBy(replies.map((reply) => reply.serverId)));
    assertRoundRobinCounts(Object.fromEntries(servers.map((server) => [
      server.serverId,
      server.evidence.filter((requestId) => requestId.startsWith('verify-')).length
    ])));
  } finally {
    await clientRuntime.stop();
    for (const server of [...servers].reverse()) {
      await server.runtime.stop();
    }
  }
});

test('DSC-008 requestToChannel traffic survives location scale-out and scale-in', async () => {
  const locationStore = new framework.ZLinkInMemoryLocationStore();
  const heldPorts = await reserveHeldPorts(3);
  const providerAEndpoint = `tcp://127.0.0.1:${heldPorts.ports[0]}`;
  const providerBEndpoint = `tcp://127.0.0.1:${heldPorts.ports[1]}`;
  const providerCEndpoint = `tcp://127.0.0.1:${heldPorts.ports[2]}`;
  const providerA = createScaleoutProvider(locationStore, providerAEndpoint, 'provider-a');
  const providerB = createScaleoutProvider(locationStore, providerBEndpoint, 'provider-b');
  const providerC = createScaleoutProvider(locationStore, providerCEndpoint, 'provider-c');
  let clientAppA;
  let clientAppB;

  try {
    await heldPorts.release(providerAEndpoint);
    await providerA.runtime.start();
    await waitForReadyEndpoints(locationStore, [providerAEndpoint]);
    clientAppA = await createScaleoutClientApp(locationStore);
    clientAppB = await createScaleoutClientApp(locationStore);
    const clientA = clientAppA.get(nestjs.ZLINK_CHANNEL_CLIENT);
    const clientB = clientAppB.get(nestjs.ZLINK_CHANNEL_CLIENT);

    const first = await requestScaleoutProbe(clientA, 'node-warmup-a');
    assert.equal(first.providerId, 'provider-a');
    assert.equal(providerA.evidence.includes('node-warmup-a'), true);

    const singleProviderTraffic = await runScaleoutTrafficBatch([clientA, clientB], 'node-traffic-a', 10);
    for (const providerId of singleProviderTraffic.observedProviders) {
      assert.equal(providerId, 'provider-a');
    }

    await heldPorts.release(providerBEndpoint);
    await providerB.runtime.start();
    await waitForReadyEndpoints(locationStore, [providerAEndpoint, providerBEndpoint]);
    await heldPorts.release(providerCEndpoint);
    await providerC.runtime.start();
    await waitForReadyEndpoints(locationStore, [providerAEndpoint, providerBEndpoint, providerCEndpoint]);
    const scaleoutTraffic = await waitForScaleoutTrafficProviders([clientA, clientB], 'node-scaleout', ['provider-b', 'provider-c']);
    assertRequestIdsHandledOnce(scaleoutTraffic.completedRequestIds, providerA, providerB, providerC);

    await providerA.runtime.stop();
    await waitUntilEndpointIsNotReady(locationStore, providerAEndpoint);
    await waitForAutoConnectPollCycle();

    const providerACountBeforeScaleIn = providerA.evidence.length;
    const scaleinTraffic = await runScaleoutTrafficBatch([clientA, clientB], 'node-scalein-traffic', 10);
    assertRequestIdsHandledOnce(scaleinTraffic.completedRequestIds, providerA, providerB, providerC);
    for (const providerId of scaleinTraffic.observedProviders) {
      assert.notEqual(providerId, 'provider-a');
    }
    const scaleinRequestIds = [];
    const scaleinProviders = new Set();
    for (let i = 0; i < 10; i += 1) {
      const requestId = `node-scalein-${i}`;
      scaleinRequestIds.push(requestId);
      const client = i % 2 === 0 ? clientA : clientB;
      const reply = await requestScaleoutProbe(client, requestId);
      scaleinProviders.add(reply.providerId);
      assert.notEqual(reply.providerId, 'provider-a');
    }
    assert.equal(providerA.evidence.length, providerACountBeforeScaleIn);
    for (const providerId of scaleinProviders) {
      assert.equal(providerId === 'provider-b' || providerId === 'provider-c', true);
    }
    assertRequestIdsHandledOnce(scaleinRequestIds, providerA, providerB, providerC);
  } finally {
    await heldPorts.releaseAll();
    await clientAppB?.close();
    await clientAppA?.close();
    await Promise.allSettled([
      providerC.runtime.stop(),
      providerB.runtime.stop(),
      providerA.runtime.stop()
    ]);
  }
});

test('DSC-009 same routing id different endpoint replaces located provider', async () => {
  const locationStore = new framework.ZLinkInMemoryLocationStore();
  const providerV1Endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const providerV2Endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const providerRid = 'api-a';

  await locationStore.renewOwnerLease('provider-v1', providerRid, 30000);
  await locationStore.updatePeer(
    scaleoutPeer('provider-v1', providerRid, providerV1Endpoint),
    framework.ZLinkLocationWriteIntent.NewClaim
  );
  await waitForSingleReadyEndpoint(locationStore, providerRid, providerV1Endpoint);

  await locationStore.removeAllByOwner('provider-v1');
  await locationStore.removeOwnerLease('provider-v1');
  await locationStore.renewOwnerLease('provider-v2', providerRid, 30000);
  await locationStore.updatePeer(
    scaleoutPeer('provider-v2', providerRid, providerV2Endpoint),
    framework.ZLinkLocationWriteIntent.NewClaim
  );

  await waitForSingleReadyEndpoint(locationStore, providerRid, providerV2Endpoint);
});

test('ZLinkFrameworkRuntimeHost uses channel serializer registry for typed request replies', async () => {
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const contentType = 'application/x-test-codec';
  const calls = [];
  const serializer = {
    serialize(value) {
      return zlink.Message.from(Buffer.from(JSON.stringify({ wrapped: value })));
    },
    deserialize(message) {
      return JSON.parse(Buffer.from(message.data()).toString()).wrapped;
    }
  };
  const registrationOptions = {
    codecs: {
      serializers: [{ contentType, serializer }]
    }
  };
  const serverRegistration = framework.createFrameworkRegistration({
    ...registrationOptions,
    channels: {
      play: {
        server: { bind: endpoint },
        requestHandlers: [{
          packetName: 'CreateGame',
          handler: {
            handle(payload, context) {
              assert.equal(context.contentType, contentType);
              assert.equal(Buffer.isBuffer(payload), false);
              assert.deepEqual(payload, { gameName: 'sample' });
              const reply = { created: payload.gameName };
              calls.push(reply);
              return reply;
            }
          }
        }]
      }
    }
  });
  const clientRegistration = framework.createFrameworkRegistration({
    ...registrationOptions,
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
      client.requestToChannel('play', typedPacket('CreateGame', { gameName: 'sample' })).timeout(1000).submit()
    );

    assert.deepEqual(calls, [{ created: 'sample' }]);
    assert.deepEqual(reply, { created: 'sample' });
  } finally {
    await clientRuntime.stop();
    await serverRuntime.stop();
  }
});

test('CDC-001 ZLinkFrameworkRuntimeHost JSON codec round-trips nested arrays and nullable fields', async () => {
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const request = {
    name: 'root',
    revision: 42,
    optionalLabel: null,
    tags: ['alpha', 'beta'],
    children: [
      { name: 'first', rank: 1 },
      { name: 'second', rank: 2 }
    ],
    nested: { name: 'nested', rank: 3 }
  };
  const serverRegistration = framework.createFrameworkRegistration({
    channels: {
      codec: {
        server: { bind: endpoint },
        requestHandlers: [{
          packetName: 'JsonCodecProbe',
          handler: {
            handle(payload, context) {
              assert.equal(context.channelName, 'codec');
              assert.equal(context.packetName, 'JsonCodecProbe');
              assert.deepEqual(payload, request);
              return payload;
            }
          }
        }]
      }
    }
  });
  const clientRegistration = framework.createFrameworkRegistration({
    channels: {
      codec: { client: { manualConnections: [endpoint] } }
    }
  });
  const serverRuntime = new framework.ZLinkFrameworkRuntimeHost({ registration: serverRegistration });
  const clientRuntime = new framework.ZLinkFrameworkRuntimeHost({ registration: clientRegistration });

  try {
    await serverRuntime.start();
    await clientRuntime.start();
    const client = new framework.DefaultZLinkChannelClient(clientRegistration, clientRuntime.channelTransport);
    const reply = await submitWhenReachable(() =>
      client.requestToChannel('codec', typedPacket('JsonCodecProbe', request)).timeout(1000).submit()
    );

    assert.deepEqual(reply, request);
    assert.equal(reply.optionalLabel, null);
    assert.deepEqual(reply.tags, ['alpha', 'beta']);
    assert.deepEqual(reply.nested, { name: 'nested', rank: 3 });
  } finally {
    await clientRuntime.stop();
    await serverRuntime.stop();
  }
});

test('ZLinkFrameworkRuntimeHost uses protobuf codec extension for channels', async () => {
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const contentType = 'application/x-protobuf';
  const calls = [];
  const codecs = new framework.DefaultZLinkCodecRegistryBuilder()
    .use(frameworkProtobuf.zlinkProtobufCodec());
  const registrationOptions = {
    codecs: {
      codecs: codecs.registeredCodecs,
      serializers: [...codecs.registeredSerializers].map(([registeredContentType, serializer]) => ({
        contentType: registeredContentType,
        serializer
      }))
    }
  };
  const serverRegistration = framework.createFrameworkRegistration({
    ...registrationOptions,
    channels: {
      play: {
        server: { bind: endpoint },
        requestHandlers: [{
          packetName: 'CreateGame',
          handler: {
            handle(payload, context) {
              assert.equal(context.contentType, contentType);
              calls.push(payload);
              return { created: payload.gameName, players: payload.players };
            }
          }
        }]
      }
    }
  });
  const clientRegistration = framework.createFrameworkRegistration({
    ...registrationOptions,
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
      client.requestToChannel('play', typedPacket('CreateGame', { gameName: 'sample', players: ['p1', 'p2'] }))
        .timeout(1000)
        .submit()
    );

    assert.deepEqual(reply, { created: 'sample', players: ['p1', 'p2'] });
    assert.deepEqual(calls, [{ gameName: 'sample', players: ['p1', 'p2'] }]);
  } finally {
    await clientRuntime.stop();
    await serverRuntime.stop();
  }
});

test('PUB-001 ZLinkFrameworkRuntimeHost delivers the same sequence to three fanout subscribers', async () => {
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const topic = 'score';
  const first = [];
  const second = [];
  const third = [];
  const publisherRegistration = framework.createFrameworkRegistration({
    channels: {
      events: { publisher: { bind: endpoint } }
    }
  });
  const createSubscriberRegistration = (calls) => framework.createFrameworkRegistration({
    channels: {
      events: {
        subscriber: { manualConnections: [endpoint] },
        publishHandlers: [{
          packetName: 'ProfileChanged',
          handler: {
            handle(payload, context) {
              calls.push({
                payload,
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
  const publisherRuntime = new framework.ZLinkFrameworkRuntimeHost({ registration: publisherRegistration });
  const firstRuntime = new framework.ZLinkFrameworkRuntimeHost({ registration: createSubscriberRegistration(first) });
  const secondRuntime = new framework.ZLinkFrameworkRuntimeHost({ registration: createSubscriberRegistration(second) });
  const thirdRuntime = new framework.ZLinkFrameworkRuntimeHost({ registration: createSubscriberRegistration(third) });

  try {
    await publisherRuntime.start();
    await firstRuntime.start();
    await secondRuntime.start();
    await thirdRuntime.start();

    const fanout = new framework.DefaultZLinkFanoutClient(publisherRegistration, publisherRuntime.channelTransport);
    const common = await publishUntilCommonFanoutSequence(fanout, topic, first, second, third);
    for (const calls of [first, second, third]) {
      assert.ok(calls.some((call) =>
        call.payload.sequence === common &&
        call.channelName === 'events' &&
        call.packetName === 'ProfileChanged' &&
        call.topic === topic &&
        call.contentType === 'application/json'
      ));
    }
  } finally {
    await thirdRuntime.stop();
    await secondRuntime.stop();
    await firstRuntime.stop();
    await publisherRuntime.stop();
  }
});

test('fanout publisher binds during runtime start before the first publish', async () => {
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const calls = [];
  const publisherRegistration = framework.createFrameworkRegistration({
    channels: {
      events: { publisher: { bind: endpoint } }
    }
  });
  const subscriberRegistration = framework.createFrameworkRegistration({
    channels: {
      events: {
        subscriber: { manualConnections: [endpoint] },
        publishHandlers: [{
          packetName: 'ProfileChanged',
          handler: {
            handle(payload) {
              calls.push(payload);
            }
          }
        }]
      }
    }
  });
  const publisherRuntime = new framework.ZLinkFrameworkRuntimeHost({ registration: publisherRegistration });
  const subscriberRuntime = new framework.ZLinkFrameworkRuntimeHost({ registration: subscriberRegistration });

  try {
    await publisherRuntime.start();
    await subscriberRuntime.start();
    await new Promise((resolve) => setTimeout(resolve, 200));

    const fanout = new framework.DefaultZLinkFanoutClient(publisherRegistration, publisherRuntime.channelTransport);
    await fanout
      .publish('events', 'score', typedPacket('ProfileChanged', { sequence: 1 }))
      .submit();

    await waitFor(() => calls.length === 1, 'first fanout publish after runtime start');
    assert.deepEqual(calls[0], { sequence: 1 });
  } finally {
    await subscriberRuntime.stop();
    await publisherRuntime.stop();
  }
});

test('ZLinkModule route client uses runtime host route transport after bootstrap', async () => {
  const ctx = zlink.createContext();
  const remoteRouter = zlink.createRouterSocket(ctx);
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const module = nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
    .addRouteMeshChannel('mesh')
      .enableRouter(endpoint)
      .routingId('node-a')
    .build());
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

    const replyPromise = submitWhenReachable(() =>
      routeClient.requestToNode('mesh', 'node-b', typedPacket('RoutePing', { value: 'ping' })).timeout(1000).submit()
    );
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

    routeClient.sendToNode('mesh', 'node-b', typedPacket('RouteNotice', { value: 'one-way' })).submit();
    const sent = await recvRoutedEnvelopeMessage(remoteRouter);
    const sentEnvelope = decodeDotnetEnvelope(sent.parts);
    assert.equal(sentEnvelope.header.kind, 3);
    assert.equal(sentEnvelope.header.channelName, 'mesh');
    assert.equal(sentEnvelope.header.messageName, 'RouteNotice');
    assert.deepEqual(sentEnvelope.body, { value: 'one-way' });
    sent.close();
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
      dispatchErrors: noDispatchErrorReporter(),
      handlers: [
        {
          kind: 'send',
          packetName: 'RouteNotice',
          handler: {
            async handle(payload, context) {
              events.push(`send:${context.channelName}:${context.packetName}:${payload.value}`);
            }
          }
        },
        {
          kind: 'request',
          packetName: 'RoutePing',
          handler: {
            async handle(payload, context) {
              events.push(`request:${context.channelName}:${context.packetName}:${context.routerChannelId}:${payload.value}`);
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
    assert.match(events[1], /^request:mesh:RoutePing:mesh:ping$/);
    reply.forEach((part) => part.close());
  } finally {
    remoteDealer.close();
    localRouter.close();
    ctx.close();
  }
});

test('ZLinkRoutePacketDispatcher drops route requests without reply sequence without throwing', async () => {
  const reported = [];
  const dispatcher = new framework.ZLinkRoutePacketDispatcher({
    routerChannelId: 'mesh',
    dispatchErrors: {
      report(event) {
        reported.push(event);
      }
    },
    handlers: [
      {
        kind: 'request',
        packetName: 'RoutePing',
        handler: {
          async handle() {
            throw new Error('handler must not run without a reply path');
          }
        }
      }
    ]
  });

  const parts = encodeDotnetEnvelope({
    kind: 1,
    channelName: 'mesh',
    messageName: 'RoutePing',
    contentType: 'application/json',
    correlationId: 'missing-seq',
    deadline: null,
    topic: null,
    errorCode: null,
    errorMessage: null
  }, { value: 'ping' }).map((part) => fakeMessagePart(part));

  await dispatcher.dispatch({
    parts,
    routingId: 'node-b',
    requestSeq: null,
    send() {
      throw new Error('send path must not be used for request');
    }
  }, {
    reply() {
      throw new Error('reply path must not be used without requestSeq');
    }
  });

  assert.equal(reported.length, 1);
  assert.equal(reported[0].reason, framework.ZLinkDispatchErrorReason.ReplyPathMissing);
  assert.equal(reported[0].action, framework.ZLinkDispatchErrorAction.Drop);
  assert.equal(reported[0].packetName, 'RoutePing');
  parts.forEach((part) => part.close());
});

test('ZLinkRoutePacketDispatcher forwards SPOT-addressed route frames to local SPOT delivery', async () => {
  const forwarded = [];
  const dispatcher = new framework.ZLinkRoutePacketDispatcher({
    routerChannelId: 'mesh',
    dispatchErrors: noDispatchErrorReporter(),
    handlers: []
  });
  const parts = [
    fakeMessagePart(Buffer.from('spot-header')),
    fakeMessagePart(Buffer.from('spot-body'))
  ];

  const consumed = await dispatcher.dispatch({
    parts,
    routingId: 'node-b',
    spotRid: 'room-1',
    requestSeq: null,
    send() {
      return captureRawMultipart(forwarded);
    }
  }, {
    reply() {
      throw new Error('reply path must not be used for SPOT-addressed frame');
    }
  });

  assert.equal(forwarded.length, 2);
  assert.equal(forwarded[0].data().toString(), 'spot-header');
  assert.equal(forwarded[1].data().toString(), 'spot-body');
  assert.equal(consumed, true);
});

test('ZLinkRoutePacketDispatcher lets route bridge handle SPOT-addressed bridge frames first', async () => {
  const handled = [];
  const dispatcher = new framework.ZLinkRoutePacketDispatcher({
    routerChannelId: 'mesh',
    dispatchErrors: noDispatchErrorReporter(),
    handlers: [],
    spotRouteBridge: {
      handleRouterReceived(channelName, sourceNodeRid, requestSeq, parts) {
        handled.push({
          channelName,
          sourceNodeRid,
          requestSeq,
          parts: parts.map((part) => part.data().toString())
        });
        return true;
      }
    }
  });
  const parts = [
    fakeMessagePart(Buffer.from('__zlink.routed_spot.egress.request')),
    fakeMessagePart(Buffer.from('room-1')),
    fakeMessagePart(Buffer.from('payload'))
  ];

  await dispatcher.dispatch({
    parts,
    routingId: 'node-b',
    spotRid: 'room-1',
    requestSeq: 7n,
    send() {
      throw new Error('direct SPOT delivery must not run for bridge frames');
    }
  }, {
    reply() {
      throw new Error('reply path must not be used directly');
    }
  });

  assert.deepEqual(handled, [{
    channelName: 'mesh',
    sourceNodeRid: 'node-b',
    requestSeq: 7n,
    parts: ['__zlink.routed_spot.egress.request', 'room-1', 'payload']
  }]);
});

test('ZLinkModule route channel dispatches inbound routed handlers after bootstrap', async () => {
  const ctx = zlink.createContext();
  const remoteDealer = zlink.createDealerSocket(ctx);
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const events = [];
  class RouteNoticeHandler {
    async handle(payload, context) {
      events.push(`send:${context.channelName}:${context.packetName}:${payload.value}`);
    }
  }
  class RoutePingHandler {
    async handle(payload, context) {
      events.push(`request:${context.channelName}:${context.packetName}:${context.routerChannelId}:${payload.value}`);
      return { value: 'pong' };
    }
  }
  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .addRouteMeshChannel('mesh')
        .enableRouter(endpoint)
        .routingId('node-a')
        .addSendHandler('RouteNotice', RouteNoticeHandler)
        .addRequestHandler('RoutePing', RoutePingHandler)
      .build())],
    providers: [RouteNoticeHandler, RoutePingHandler]
  })(HandlerModule);
  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const runtime = app.get(nestjs.ZLINK_FRAMEWORK_RUNTIME, { strict: false });

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
    assert.match(events[1], /^request:mesh:RoutePing:mesh:ping$/);
    reply.forEach((part) => part.close());
  } finally {
    remoteDealer.close();
    await runtime.stop();
    await app.close();
    ctx.close();
  }
});

test('ZLinkModule routeMesh channel option dispatches inbound routed handlers after bootstrap', async () => {
  const ctx = zlink.createContext();
  const remoteDealer = zlink.createDealerSocket(ctx);
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const events = [];
  let filterInvocations = 0;
  class ChannelOnlyFilter {
    async invoke(_invocation, next) {
      filterInvocations += 1;
      return next();
    }
  }
  class RoutePingHandler {
    async handle(payload, context) {
      events.push(`request:${context.channelName}:${context.packetName}:${context.routerChannelId}:${payload.value}`);
      return { value: 'pong' };
    }
  }
  class HandlerModule {}
  Module({
    imports: [nestjs.ZLinkModule.forRoot(nestjs.zlinkFramework()
      .options({ filters: [ChannelOnlyFilter] })
      .addRouteMeshChannel('mesh')
        .enableRouter(endpoint)
        .routingId('node-a')
        .addRequestHandler('RoutePing', RoutePingHandler)
      .build())],
    providers: [ChannelOnlyFilter, RoutePingHandler]
  })(HandlerModule);
  const app = await NestFactory.createApplicationContext(HandlerModule, { logger: false, abortOnError: false });
  const runtime = app.get(nestjs.ZLINK_FRAMEWORK_RUNTIME, { strict: false });

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
    assert.match(events[0], /^request:mesh:RoutePing:mesh:ping$/);
    assert.equal(filterInvocations, 0);
    reply.forEach((part) => part.close());
  } finally {
    remoteDealer.close();
    await runtime.stop();
    await app.close();
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

test('channel runtime keeps one outstanding dealer request per socket', async () => {
  const callbacks = [];
  const socket = {
    ...fakeBackpressuredDealer(),
    writable: true,
    sendTimeoutMs: -1,
    request(parts, callback) {
      this.requestAttempts++;
      callbacks.push(callback);
      this.sentParts = parts.map(fakeMessagePart);
      return true;
    }
  };
  const manager = new framework.ZLinkChannelRuntimeManager(
    framework.createFrameworkRegistration({
      channels: { api: { client: { manualConnections: ['tcp://peer:7101'] } } }
    }),
    fakeChannelAdapter({ dealer: socket }),
    fakeContext()
  );

  const first = manager.request('api', 'Ping', { value: 'first' }, 1000);
  const second = manager.request('api', 'Ping', { value: 'second' }, 1000);
  await Promise.resolve();

  assert.equal(socket.requestAttempts, 1);
  assert.equal(callbacks.length, 1);
  callbacks.shift()(0, encodeDotnetEnvelope({
    kind: 2,
    channelName: 'api',
    messageName: 'Ping',
    contentType: 'application/json',
    correlationId: null,
    deadline: null,
    topic: null,
    errorCode: null,
    errorMessage: null
  }, { value: 'first-reply' }).map(fakeMessagePart));

  assert.deepEqual(await first, { value: 'first-reply' });
  await waitUntil(() => socket.requestAttempts === 2);
  assert.equal(callbacks.length, 1);
  callbacks.shift()(0, encodeDotnetEnvelope({
    kind: 2,
    channelName: 'api',
    messageName: 'Ping',
    contentType: 'application/json',
    correlationId: null,
    deadline: null,
    topic: null,
    errorCode: null,
    errorMessage: null
  }, { value: 'second-reply' }).map(fakeMessagePart));

  assert.deepEqual(await second, { value: 'second-reply' });
  await manager.dispose();
});

test('PUB-001 partial ZLinkFanoutClient publishes through public pub/sub binding sockets', async () => {
  const ctx = zlink.createContext();
  const pub = zlink.createPubSocket(ctx);
  const sub = zlink.createSubSocket(ctx);
  const dealer = zlink.createDealerSocket(ctx);
  const endpoint = `inproc://framework-channel-publish-${process.pid}-${Date.now()}`;
  const topic = 'events';

  try {
    pub.bind(endpoint);
    sub.setSubscription(topic);
    sub.recvTimeout = 20;
    sub.connect(endpoint);

    const registration = framework.createFrameworkRegistration({
      channels: { events: { publisher: { bind: endpoint } } }
    });
    const fanout = new framework.DefaultZLinkFanoutClient(
      registration,
      new framework.ZLinkDealerChannelClientTransport(dealer, pub)
    );

    const received = new zlink.TopicMessage();
    try {
      await publishUntilSubscribed(fanout, sub, received, topic, 'Event', 'published');
      assert.equal(received.topic, topic);
      const envelope = decodeDotnetEnvelope(received.parts);
      assert.equal(envelope.header.kind, 4);
      assert.equal(envelope.header.channelName, 'events');
      assert.equal(envelope.header.messageName, 'Event');
      assert.equal(envelope.header.topic, topic);
      assert.equal(envelope.body, 'published');
    } finally {
      received.close();
    }
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
      channelName: 'api',
      dispatchErrors: noDispatchErrorReporter(),
      handlers: new Map([
        ['Ping', {
          async handle(payload) {
            assert.equal(payload, 'ping');
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

    const replyPromise = client.requestToChannel('api', typedPacket('Ping', 'ping')).timeout(1000).submit();
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

test('ZLinkChannelRequestDispatcher replies error and reports observer for missing request handler', async () => {
  const events = [];
  class DispatchObserver {
    onMessageFlow(event) {
      events.push(event);
    }
  }
  const replies = [];
  const dispatcher = new framework.ZLinkChannelRequestDispatcher({
    channelName: 'api',
    dispatchErrors: dispatchErrorReporter(
      DispatchObserver,
      { reportRuntimeTaskException() {} }
    ),
    handlers: new Map(),
    sendHandlers: new Map()
  });
  const parts = encodeDotnetEnvelope({
    kind: 1,
    channelName: 'api',
    messageName: 'MissingReq',
    contentType: 'application/json',
    correlationId: 'corr-1',
    deadline: null,
    topic: null,
    errorCode: null,
    errorMessage: null
  }, { value: 'request' }).map(fakeMessagePart);
  const router = {
    reply() {
      return captureMultipart(replies);
    }
  };

  await dispatcher.dispatch({
    parts,
    routingId: 'client-1',
    requestSeq: 7n
  }, router);
  await new Promise((resolve) => setImmediate(resolve));

  assert.equal(replies.length, 2);
  const replyEnvelope = decodeDotnetEnvelope(replies);
  assert.equal(replyEnvelope.header.kind, 5);
  assert.equal(replyEnvelope.header.correlationId, 'corr-1');
  assert.equal(events.length, 1);
  assert.equal(events[0].surface, framework.ZLinkDispatchErrorSurface.Channel);
  assert.equal(events[0].messageKind, framework.ZLinkDispatchMessageKind.Request);
  assert.equal(events[0].outcome, framework.ZLinkMessageFlowOutcome.Error);
  assert.equal(events[0].errorReason, framework.ZLinkDispatchErrorReason.HandlerMissing);
  assert.equal(events[0].errorAction, framework.ZLinkDispatchErrorAction.ReplyError);
  assert.equal(events[0].packetName, 'MissingReq');
  assert.equal(events[0].channelName, 'api');
  assert.equal(events[0].correlationId, 'corr-1');
});

test('ZLinkChannelRequestDispatcher drops requests without reply sequence without invoking handlers', async () => {
  const events = [];
  let handlerInvocations = 0;
  class DispatchObserver {
    onMessageFlow(event) {
      events.push(event);
    }
  }
  const dispatcher = new framework.ZLinkChannelRequestDispatcher({
    channelName: 'api',
    dispatchErrors: dispatchErrorReporter(
      DispatchObserver,
      { reportRuntimeTaskException() {} }
    ),
    handlers: new Map([
      ['Ping', {
        handle() {
          handlerInvocations += 1;
          return 'pong';
        }
      }]
    ])
  });
  const parts = encodeDotnetEnvelope({
    kind: 1,
    channelName: 'api',
    messageName: 'Ping',
    contentType: 'application/json',
    correlationId: 'corr-no-seq',
    deadline: null,
    topic: null,
    errorCode: null,
    errorMessage: null
  }, 'ping').map(fakeMessagePart);

  await dispatcher.dispatch({
    parts,
    routingId: 'client-1',
    requestSeq: null
  }, {
    reply() {
      assert.fail('request without requestSeq must not create a reply operation');
    }
  });
  await new Promise((resolve) => setImmediate(resolve));

  assert.equal(handlerInvocations, 0);
  assert.equal(events.length, 1);
  assert.equal(events[0].surface, framework.ZLinkDispatchErrorSurface.Channel);
  assert.equal(events[0].messageKind, framework.ZLinkDispatchMessageKind.Request);
  assert.equal(events[0].errorReason, framework.ZLinkDispatchErrorReason.ReplyPathMissing);
  assert.equal(events[0].errorAction, framework.ZLinkDispatchErrorAction.Drop);
  assert.equal(events[0].packetName, 'Ping');
  assert.equal(events[0].correlationId, 'corr-no-seq');
});

test('DERR-006 ZLinkChannelRequestDispatcher replies error and reports observer for payload decode failure', async () => {
  const events = [];
  const runtimeFailures = [];
  let handlerInvocations = 0;
  class DispatchObserver {
    onMessageFlow(event) {
      events.push(event);
    }
  }
  const replies = [];
  const dispatcher = new framework.ZLinkChannelRequestDispatcher({
    channelName: 'api',
    dispatchErrors: dispatchErrorReporter(
      DispatchObserver,
      {
        reportRuntimeTaskException(taskName, error) {
          runtimeFailures.push({ taskName, error });
        }
      }
    ),
    handlers: new Map([
      ['BrokenReq', {
        handle() {
          handlerInvocations += 1;
          return { value: 'unexpected' };
        }
      }],
      ['KnownReq', {
        handle(payload) {
          return { value: `known:${payload.value}` };
        }
      }]
    ]),
    sendHandlers: new Map()
  });
  const parts = [
    fakeMessagePart(Buffer.from(JSON.stringify({
      formatMarker: 0xf2,
      flowId: '018f2b63-9d4a-7abc-8def-0123456789ab',
      flowOrigin: 1,
      kind: 1,
      channelName: 'api',
      messageName: 'BrokenReq',
      contentType: 'application/json',
      correlationId: 'corr-decode',
      deadline: null,
      topic: null,
      errorCode: null,
      errorMessage: null
    }))),
    fakeMessagePart(Buffer.from('{'))
  ];
  const router = {
    reply() {
      return captureMultipart(replies);
    }
  };

  await dispatcher.dispatch({
    parts,
    routingId: 'client-1',
    requestSeq: 11n
  }, router);
  await new Promise((resolve) => setImmediate(resolve));

  assert.equal(handlerInvocations, 0);
  assert.equal(replies.length, 2);
  const replyEnvelope = decodeDotnetEnvelope(replies);
  assert.equal(replyEnvelope.header.kind, 5);
  assert.equal(replyEnvelope.header.correlationId, 'corr-decode');
  assert.match(replyEnvelope.header.errorMessage, /PayloadDecodeFailed/);
  assert.equal(events.length, 1);
  assert.equal(events[0].surface, framework.ZLinkDispatchErrorSurface.Channel);
  assert.equal(events[0].messageKind, framework.ZLinkDispatchMessageKind.Request);
  assert.equal(events[0].outcome, framework.ZLinkMessageFlowOutcome.Error);
  assert.equal(events[0].errorReason, framework.ZLinkDispatchErrorReason.PayloadDecodeFailed);
  assert.equal(events[0].errorAction, framework.ZLinkDispatchErrorAction.ReplyError);
  assert.equal(events[0].packetName, 'BrokenReq');
  assert.equal(events[0].channelName, 'api');
  assert.equal(events[0].correlationId, 'corr-decode');
  assert.equal(events[0].errorType, 'ZLinkFrameworkException');
  assert.match(events[0].errorMessage, /PayloadDecodeFailed/);
  assert.equal(runtimeFailures.length, 0);

  replies.length = 0;
  await dispatcher.dispatch({
    parts: encodeDotnetEnvelope({
      kind: 1,
      channelName: 'api',
      messageName: 'KnownReq',
      contentType: 'application/json',
      correlationId: 'corr-after-decode',
      deadline: null,
      topic: null,
      errorCode: null,
      errorMessage: null
    }, { value: 'after-decode-error' }).map(fakeMessagePart),
    routingId: 'client-1',
    requestSeq: 12n
  }, router);
  const afterEnvelope = decodeDotnetEnvelope(replies);
  assert.equal(afterEnvelope.header.kind, 2);
  assert.deepEqual(afterEnvelope.body, { value: 'known:after-decode-error' });
});

test('ZLinkChannelRequestDispatcher waits for send-ready before completing backpressured error replies', async () => {
  let readyHandler = () => undefined;
  const replies = [];
  const replySubmitter = new framework.ZLinkAsyncSubmitter(
    (handler) => {
      readyHandler = handler;
    },
    { timeoutMs: 1000 }
  );
  const dispatcher = new framework.ZLinkChannelRequestDispatcher({
    channelName: 'api',
    dispatchErrors: noDispatchErrorReporter(),
    handlers: new Map([
      ['ThrowReq', {
        handle() {
          throw new Error('deterministic handler failure');
        }
      }]
    ]),
    sendHandlers: new Map(),
    replySubmitter
  });
  const router = {
    writable: false,
    attempts: 0,
    reply() {
      return captureBackpressuredMultipart(replies, () => {
        this.attempts += 1;
        return this.writable;
      });
    }
  };

  const dispatchPromise = dispatcher.dispatch({
    parts: encodeDotnetEnvelope({
      kind: 1,
      channelName: 'api',
      messageName: 'ThrowReq',
      contentType: 'application/json',
      correlationId: 'corr-backpressured-error',
      deadline: null,
      topic: null,
      errorCode: null,
      errorMessage: null
    }, { value: 'boom' }).map(fakeMessagePart),
    routingId: 'client-1',
    requestSeq: 17n
  }, router);

  await Promise.resolve();
  assert.equal(router.attempts, 2);
  assert.equal(replies.length, 0);

  router.writable = true;
  readyHandler();
  await dispatchPromise;

  assert.equal(router.attempts, 3);
  assert.equal(replies.length, 2);
  const replyEnvelope = decodeDotnetEnvelope(replies);
  assert.equal(replyEnvelope.header.kind, 5);
  assert.equal(replyEnvelope.header.correlationId, 'corr-backpressured-error');
  assert.match(replyEnvelope.header.errorMessage, /deterministic handler failure/);
});

test('ZLinkAsyncSubmitter discards queued ownership exactly once when aborted before submit', async () => {
  const controller = new AbortController();
  let readyHandler = () => undefined;
  let discarded = 0;
  const submitter = new framework.ZLinkAsyncSubmitter((handler) => { readyHandler = handler; });
  const pending = submitter.submitCommand(
    () => false,
    controller.signal,
    () => { discarded++; }
  );

  controller.abort();
  await assert.rejects(() => pending, /aborted/i);
  readyHandler();
  submitter.dispose();
  assert.equal(discarded, 1);
});

test('ZLinkAsyncSubmitter one-way submission rejects a full local queue synchronously', () => {
  const submitter = new framework.ZLinkAsyncSubmitter(() => undefined, { capacity: 1 });
  submitter.submitCommandOneWay(() => false);

  assert.throws(
    () => submitter.submitCommandOneWay(() => false),
    /queue is full/i
  );
  submitter.dispose();
});

test('ZLinkAsyncSubmitter one-way submission preserves immediate transport errors', () => {
  const submitter = new framework.ZLinkAsyncSubmitter(() => undefined);

  assert.throws(
    () => submitter.submitCommandOneWay(() => { throw new Error('immediate transport failure'); }),
    /immediate transport failure/
  );
  submitter.dispose();
});

test('ZLinkAsyncSubmitter reports one-way failures that happen after queue acceptance', async () => {
  const controller = new AbortController();
  const failures = [];
  const submitter = new framework.ZLinkAsyncSubmitter(() => undefined, {
    onCommandFailure: (error) => failures.push(error)
  });
  submitter.submitCommandOneWay(() => false, controller.signal);

  controller.abort();
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(failures.length, 1);
  assert.match(failures[0].message, /aborted/i);
  submitter.dispose();
});

test('ZLinkAsyncSubmitter does not discard an accepted synchronous rejection', async () => {
  let discarded = 0;
  const submitter = new framework.ZLinkAsyncSubmitter(() => undefined);
  const pending = submitter.submitRequest(
    (_resolve, reject) => {
      reject(new Error('synchronous transport failure'));
      return true;
    },
    undefined,
    1000,
    () => { discarded++; }
  );

  await assert.rejects(pending, /synchronous transport failure/);
  assert.equal(discarded, 0);
  submitter.dispose();
});

test('ZLinkAsyncSubmitter settles with both abort and discard failures', async () => {
  const controller = new AbortController();
  const submitter = new framework.ZLinkAsyncSubmitter(() => undefined);
  const pending = submitter.submitCommand(
    () => false,
    controller.signal,
    () => { throw new Error('discard failed'); }
  );

  controller.abort();
  await assert.rejects(pending, (error) => {
    assert.equal(error instanceof AggregateError, true);
    assert.match(error.message, /discard failed/i);
    assert.equal(error.errors.length, 2);
    return true;
  });
  submitter.dispose();
});

test('DERR-009 ZLinkChannelRequestDispatcher writes dispatch errors to file log', async () => {
  const logDir = fs.mkdtempSync(path.join(os.tmpdir(), 'zlink-node-derr-009-'));
  const logPath = path.join(logDir, 'dispatch-errors.log');
  const events = [];
  class DispatchObserver {
    onMessageFlow(event) {
      events.push(event);
      fs.appendFileSync(
        logPath,
        [
          'message-flow-error',
          `surface=${event.surface}`,
          `messageKind=${event.messageKind}`,
          `reason=${event.errorReason}`,
          `action=${event.errorAction}`,
          `packetName=${event.packetName}`,
          `channelName=${event.channelName}`,
          `correlationId=${event.correlationId}`
        ].join(' ') + '\n'
      );
    }
  }
  const dispatcher = new framework.ZLinkChannelRequestDispatcher({
    channelName: 'api',
    dispatchErrors: dispatchErrorReporter(
      DispatchObserver,
      { reportRuntimeTaskException() {} }
    ),
    handlers: new Map([
      ['DecodeReq', {
        handle() {
          return { value: 'unexpected' };
        }
      }],
      ['ThrowReq', {
        handle() {
          throw new Error('DERR-007 handler exception');
        }
      }]
    ]),
    sendHandlers: new Map()
  });
  const router = {
    reply() {
      return captureMultipart([]);
    }
  };

  try {
    await dispatcher.dispatch({
      parts: encodeDotnetEnvelope({
        kind: 1,
        channelName: 'api',
        messageName: 'MissingReq',
        contentType: 'application/json',
        correlationId: 'corr-missing',
        deadline: null,
        topic: null,
        errorCode: null,
        errorMessage: null
      }, { value: 'missing' }).map(fakeMessagePart),
      routingId: 'client-1',
      requestSeq: 21n
    }, router);
    await dispatcher.dispatch({
      parts: [
        fakeMessagePart(Buffer.from(JSON.stringify({
          formatMarker: 0xf2,
          flowId: '018f2b63-9d4a-7abc-8def-0123456789ab',
          flowOrigin: 1,
          kind: 1,
          channelName: 'api',
          messageName: 'DecodeReq',
          contentType: 'application/json',
          correlationId: 'corr-decode',
          deadline: null,
          topic: null,
          errorCode: null,
          errorMessage: null
        }))),
        fakeMessagePart(Buffer.from('{'))
      ],
      routingId: 'client-1',
      requestSeq: 22n
    }, router);
    await dispatcher.dispatch({
      parts: encodeDotnetEnvelope({
        kind: 1,
        channelName: 'api',
        messageName: 'ThrowReq',
        contentType: 'application/json',
        correlationId: 'corr-throw',
        deadline: null,
        topic: null,
        errorCode: null,
        errorMessage: null
      }, { value: 'boom' }).map(fakeMessagePart),
      routingId: 'client-1',
      requestSeq: 23n
    }, router);
    await new Promise((resolve) => setImmediate(resolve));

    assert.equal(events.length, 3);
    const text = fs.readFileSync(logPath, 'utf8');
    assert.equal((text.match(/message-flow-error/g) ?? []).length, 3);
    assert.match(text, /surface=channel/);
    assert.match(text, /messageKind=request/);
    assert.match(text, /reason=handlerMissing/);
    assert.match(text, /reason=payloadDecodeFailed/);
    assert.match(text, /reason=handlerException/);
    assert.match(text, /action=replyError/);
    assert.match(text, /packetName=MissingReq/);
    assert.match(text, /packetName=DecodeReq/);
    assert.match(text, /packetName=ThrowReq/);
    assert.match(text, /channelName=api/);
    assert.match(text, /correlationId=corr-decode/);
  } finally {
    fs.rmSync(logDir, { recursive: true, force: true });
  }
});

test('channel dispatch failures use contract log levels without duplicate one-way logs', () => {
  const calls = { error: [], warn: [], debug: [] };
  const originals = {
    error: console.error,
    warn: console.warn,
    debug: console.debug
  };
  console.error = (...args) => calls.error.push(args.join(' '));
  console.warn = (...args) => calls.warn.push(args.join(' '));
  console.debug = (...args) => calls.debug.push(args.join(' '));

  try {
    const reporter = noDispatchErrorReporter();
    reporter.report({
      surface: framework.ZLinkDispatchErrorSurface.Channel,
      messageKind: framework.ZLinkDispatchMessageKind.Send,
      reason: framework.ZLinkDispatchErrorReason.HandlerMissing,
      action: framework.ZLinkDispatchErrorAction.Drop,
      packetName: 'MissingSend'
    });
    reporter.report({
      surface: framework.ZLinkDispatchErrorSurface.Channel,
      messageKind: framework.ZLinkDispatchMessageKind.Publish,
      reason: framework.ZLinkDispatchErrorReason.PayloadDecodeFailed,
      action: framework.ZLinkDispatchErrorAction.Drop,
      packetName: 'BrokenPublish'
    });
    reporter.report({
      surface: framework.ZLinkDispatchErrorSurface.Channel,
      messageKind: framework.ZLinkDispatchMessageKind.Publish,
      reason: framework.ZLinkDispatchErrorReason.HandlerException,
      action: framework.ZLinkDispatchErrorAction.Drop,
      packetName: 'ThrowingPublish',
      error: new Error('application failure')
    });

    assert.equal(calls.warn.length, 1);
    assert.match(calls.warn[0], /packet=MissingSend/);
    assert.equal(calls.debug.length, 1);
    assert.match(calls.debug[0], /packet=BrokenPublish/);
    assert.equal(calls.error.length, 1);
    assert.match(calls.error[0], /packet=ThrowingPublish/);
    assert.match(calls.error[0], /errorReason=handlerException/);
  } finally {
    console.error = originals.error;
    console.warn = originals.warn;
    console.debug = originals.debug;
  }
});

test('ZLinkDispatchErrorReporter isolates observer failures', async () => {
  const sinkFailures = [];
  class ThrowingObserver {
    onMessageFlow() {
      throw new Error('observer failed');
    }
  }
  const reporter = dispatchErrorReporter(
    ThrowingObserver,
    {
      reportRuntimeTaskException(source, error) {
        sinkFailures.push({ source, error });
      }
    }
  );

  assert.doesNotThrow(() => reporter.report({
    surface: framework.ZLinkDispatchErrorSurface.Channel,
    messageKind: framework.ZLinkDispatchMessageKind.Request,
    reason: framework.ZLinkDispatchErrorReason.HandlerMissing,
    action: framework.ZLinkDispatchErrorAction.ReplyError,
    packetName: 'MissingReq',
    channelName: 'api',
    correlationId: 'corr-1'
  }));
  assert.equal(reporter.reportedCount, 1);
  await new Promise((resolve) => setImmediate(resolve));

  assert.equal(sinkFailures.length, 1);
  assert.equal(sinkFailures[0].source, 'dispatch-error-observer');
  assert.equal(reporter.observerFailureCount, 1);

  class FactoryObserver {
    onMessageFlow() {}
  }
  const factoryFailures = [];
  const factoryReporter = dispatchErrorReporter(
    FactoryObserver,
    {
      create() {
        throw new Error('factory failed');
      }
    },
    {
      reportRuntimeTaskException(source, error) {
        factoryFailures.push({ source, error });
      }
    }
  );

  assert.doesNotThrow(() => factoryReporter.report({
    surface: framework.ZLinkDispatchErrorSurface.Channel,
    messageKind: framework.ZLinkDispatchMessageKind.Request,
    reason: framework.ZLinkDispatchErrorReason.HandlerMissing,
    action: framework.ZLinkDispatchErrorAction.ReplyError
  }));
  assert.equal(factoryReporter.reportedCount, 1);
  await new Promise((resolve) => setImmediate(resolve));

  assert.equal(factoryFailures.length, 1);
  assert.equal(factoryFailures[0].source, 'dispatch-error-observer');
  assert.equal(factoryReporter.observerFailureCount, 1);

  const noObserverReporter = noDispatchErrorReporter();
  assert.doesNotThrow(() => noObserverReporter.report({
    surface: framework.ZLinkDispatchErrorSurface.Channel,
    messageKind: framework.ZLinkDispatchMessageKind.Send,
    reason: framework.ZLinkDispatchErrorReason.HandlerMissing,
    action: framework.ZLinkDispatchErrorAction.Drop
  }));
  assert.equal(noObserverReporter.reportedCount, 1);
  assert.equal(noObserverReporter.observerFailureCount, 0);
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

function createScaleoutProvider(locationStore, bindEndpoint, providerId, routingId) {
  const evidence = [];
  const registration = framework.createFrameworkRegistration({
    locations: { storeInstance: locationStore, options: scaleoutLocationOptions() },
    channels: {
      'scaleout-api': {
        server: {
          bind: bindEndpoint,
          routingId
        },
        requestHandlers: [{
          packetName: 'ScaleoutProbe',
          handler: {
            handle(payload) {
              evidence.push(payload.requestId);
              return {
                requestId: payload.requestId,
                providerId
              };
            }
          }
        }]
      }
    }
  });
  return {
    evidence,
    runtime: new framework.ZLinkFrameworkRuntimeHost({ registration })
  };
}

function createRoundRobinServer(bindEndpoint, serverId) {
  const evidence = [];
  const registration = framework.createFrameworkRegistration({
    channels: {
      'round-robin': {
        server: { bind: bindEndpoint },
        requestHandlers: [{
          packetName: 'RoundRobinProbe',
          handler: {
            handle(payload, context) {
              assert.equal(context.channelName, 'round-robin');
              assert.equal(context.packetName, 'RoundRobinProbe');
              evidence.push(payload.requestId);
              return { requestId: payload.requestId, serverId };
            }
          }
        }]
      }
    }
  });
  return {
    serverId,
    evidence,
    runtime: new framework.ZLinkFrameworkRuntimeHost({ registration })
  };
}

function countBy(values) {
  const counts = {};
  for (const value of values) {
    counts[value] = (counts[value] ?? 0) + 1;
  }
  return counts;
}

function assertRoundRobinCounts(counts) {
  assert.deepEqual(counts, {
    'server-a': 30,
    'server-b': 30,
    'server-c': 30
  });
}

async function createScaleoutClientApp(locationStore) {
  class ScaleoutClientModule {}
  const builder = nestjs.zlinkFramework().addLocationStore(locationStore);
  Object.assign(builder.configureLocations(), scaleoutLocationOptions());
  builder
    .addClientServerChannel('scaleout-api')
      .enableClient();
  Module({
    imports: [nestjs.ZLinkModule.forRoot(builder.build())]
  })(ScaleoutClientModule);
  return NestFactory.createApplicationContext(ScaleoutClientModule, { logger: false });
}

function scaleoutLocationOptions() {
  return {
    pollingIntervalMs: 20,
    heartbeatIntervalMs: 5000,
    ownerLeaseTtlMs: 30000
  };
}

async function waitForAutoConnectPollCycle() {
  await new Promise((resolve) => setTimeout(resolve, scaleoutLocationOptions().pollingIntervalMs * 3));
}

function requestScaleoutProbe(client, requestId) {
  return withAbortTimeout(
    (signal) => submitWhenReachable(() =>
      client
        .requestToChannel('scaleout-api', typedPacket('ScaleoutProbe', { requestId }))
        .timeout(1000)
        .submit(signal)
    ),
    1500,
    `scaleout probe ${requestId}`
  );
}

async function runScaleoutTrafficBatch(clients, label, count) {
  const traffic = {
    completedRequestIds: [],
    observedProviders: new Set()
  };
  for (let index = 0; index < count; index += 1) {
    const client = clients[index % clients.length];
    const requestId = `${label}-${index}`;
    const reply = await requestScaleoutProbe(client, requestId);
    traffic.completedRequestIds.push(requestId);
    traffic.observedProviders.add(reply.providerId);
  }
  return traffic;
}

async function waitForScaleoutTrafficProviders(clients, label, providers) {
  const deadline = Date.now() + 5000;
  const traffic = {
    completedRequestIds: [],
    observedProviders: new Set()
  };
  let index = 0;
  while (Date.now() < deadline) {
    const client = clients[index % clients.length];
    const requestId = `${label}-${index}`;
    index += 1;
    const reply = await requestScaleoutProbe(client, requestId);
    traffic.completedRequestIds.push(requestId);
    traffic.observedProviders.add(reply.providerId);
    if (providers.every((provider) => traffic.observedProviders.has(provider))) {
      return traffic;
    }
  }
  assert.fail(
    `HAR-007 classification-required labels=core-capi,bindings,framework,sample,harness: transition traffic did not observe providers ${providers.join(',')}`
  );
}

function scaleoutPeer(ownerId, nodeRid, endpoint) {
  return {
    autoConnectType: framework.ZLinkLocationAutoConnectType.ClientServer,
    meshName: 'scaleout-api',
    nodeRid: zlink.RoutingId.from(nodeRid),
    role: framework.ZLinkLocationRole.Router,
    endpoint,
    weight: 100,
    value: 0n,
    ownerId,
    generation: 0n,
    updatedAt: new Date(0)
  };
}

function assertRequestIdsHandledOnce(requestIds, ...providers) {
  for (const requestId of requestIds) {
    const count = providers.reduce((sum, provider) =>
      sum + provider.evidence.filter((observed) => observed === requestId).length, 0);
    assert.equal(count, 1, `${requestId} should be handled exactly once`);
  }
}

async function waitForReadyEndpoints(store, endpoints) {
  const expected = new Set(endpoints);
  await waitForScaleoutPeers(store, (entries) =>
    [...expected].every((endpoint) => entries.some((entry) => entry.endpoint === endpoint))
  );
}

async function waitUntilEndpointIsNotReady(store, endpoint) {
  await waitForScaleoutPeers(store, (entries) => entries.every((entry) => entry.endpoint !== endpoint));
}

async function waitForSingleReadyEndpoint(store, routingId, endpoint) {
  await waitForScaleoutPeers(store, (entries) =>
    entries.length === 1 && String(entries[0].nodeRid) === routingId && entries[0].endpoint === endpoint,
    routingId
  );
}

async function waitForScaleoutPeers(store, predicate, routingId) {
  const deadline = Date.now() + 5000;
  let lastEntries = [];
  while (Date.now() < deadline) {
    lastEntries = await store.listPeers({
      autoConnectType: framework.ZLinkLocationAutoConnectType.ClientServer,
      meshName: 'scaleout-api',
      role: framework.ZLinkLocationRole.Router,
      nodeRid: routingId
    });
    if (predicate(lastEntries)) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  assert.fail(
    `HAR-007 classification-required labels=core-capi,bindings,framework,sample,harness: location peers did not converge: ${JSON.stringify(lastEntries)}`
  );
}

function isHostUnreachable(error) {
  if (error instanceof framework.ZLinkFrameworkException) {
    return error.kind === framework.ZLinkFrameworkErrorKind.RouteNotConnected && error.isRetriable === true;
  }
  return error instanceof Error &&
    (((error.code === 2 || error.code === 12) && /Host unreachable/.test(error.message)) ||
      (error.code === 5 && /Connection refused/.test(error.message)));
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

async function publishUntilCommonFanoutSequence(fanout, topic, first, second, third) {
  const deadline = Date.now() + 3000;
  let sequence = 0;
  while (Date.now() < deadline) {
    const common = commonFanoutSequence(first, second, third);
    if (common !== undefined) {
      return common;
    }
    await fanout
      .publish('events', topic, typedPacket('ProfileChanged', { sequence, profileId: `p${sequence}` }))
      .submit();
    sequence += 1;
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
  const common = commonFanoutSequence(first, second, third);
  if (common !== undefined) {
    return common;
  }
  assert.fail(`fanout sequence did not reach all subscribers: first=${JSON.stringify(first)}, second=${JSON.stringify(second)}, third=${JSON.stringify(third)}`);
}

async function publishUntilHandled(fanout, channelName, topic, packetName, payload, predicate) {
  const deadline = Date.now() + 3000;
  while (Date.now() < deadline) {
    await fanout
      .publish(channelName, topic, typedPacket(packetName, payload))
      .submit();
    if (predicate()) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
  assert.fail(`publish handler evidence did not appear for ${channelName}:${packetName}`);
}

function commonFanoutSequence(first, second, third) {
  const secondSequences = new Set(second.map((call) => call.payload.sequence));
  const thirdSequences = new Set(third.map((call) => call.payload.sequence));
  return first
    .map((call) => call.payload.sequence)
    .find((sequence) => secondSequences.has(sequence) && thirdSequences.has(sequence));
}

function hasDispatchEvent(events, messageKind, reason, action, packetName, channelName) {
  return events.some((event) =>
    event.surface === framework.ZLinkDispatchErrorSurface.Channel &&
    event.messageKind === messageKind &&
    event.outcome === framework.ZLinkMessageFlowOutcome.Error &&
    event.errorReason === reason &&
    event.errorAction === action &&
    event.packetName === packetName &&
    event.channelName === channelName
  );
}

async function publishUntilSubscribed(fanout, subscriber, received, topic, packetName, payload) {
  const deadline = Date.now() + 1000;
  while (Date.now() < deadline) {
    fanout.publish('events', topic, typedPacket(packetName, payload)).submit();
    try {
      if (subscriber.subscribe(received)) {
        return;
      }
    } catch (error) {
      if (!(error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData)) {
        throw error;
      }
    }
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
  assert.fail('fanout pub/sub socket proof did not receive a publish before timeout');
}

async function reservePort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const { port } = server.address();
  await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return port;
}

async function reserveHeldPorts(count) {
  const entries = [];
  for (let index = 0; index < count; index += 1) {
    const server = net.createServer();
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    const { port } = server.address();
    entries.push({ port, server, released: false });
  }
  return {
    ports: entries.map((entry) => entry.port),
    async release(endpoint) {
      const port = Number(endpoint.replace(/^tcp:\/\/127\.0\.0\.1:/, ''));
      const entry = entries.find((candidate) => candidate.port === port);
      if (entry !== undefined) {
        await releaseHeldPort(entry);
      }
    },
    async releaseAll() {
      await Promise.all(entries.map((entry) => releaseHeldPort(entry)));
    }
  };
}

async function releaseHeldPort(entry) {
  if (entry.released) {
    return;
  }
  entry.released = true;
  await new Promise((resolve, reject) => entry.server.close((error) => error ? reject(error) : resolve()));
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
    Buffer.from(JSON.stringify({
      formatMarker: 0xf2,
      flowId: '018f2b63-9d4a-7abc-8def-0123456789ab',
      flowOrigin: 1,
      ...header
    })),
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

function noDispatchErrorReporter() {
  return new framework.ZLinkDispatchErrorReporter(
    undefined,
    undefined,
    { reportRuntimeTaskException() {} }
  );
}

function dispatchErrorReporter(observerType, providerResolverOrSink, maybeSink) {
  const providerResolver = maybeSink === undefined ? undefined : providerResolverOrSink;
  const sink = maybeSink ?? providerResolverOrSink;
  return new framework.ZLinkDispatchErrorReporter(
    undefined,
    providerResolver,
    sink,
    {
      diagnostics: {},
      liveMode: { mode: framework.ZLinkMessageFlowLogMode.ErrorsOnly },
      messageFlowObserverType: observerType,
      providerResolver
    }
  );
}

function captureMultipart(parts) {
  return {
    message(part) {
      parts.push(fakeMessagePart(part));
      return this;
    },
    submit() {}
  };
}

function captureBackpressuredMultipart(parts, submit) {
  const pending = [];
  return {
    message(part) {
      pending.push(fakeMessagePart(part));
      return this;
    },
    submit() {
      if (!submit()) {
        return false;
      }
      parts.push(...pending);
      return true;
    }
  };
}

function captureRawMultipart(parts) {
  return {
    message(part) {
      parts.push(part);
      return this;
    },
    submit() {}
  };
}

function submitRequestMultipart(operation, parts) {
  let current = operation.message(parts[0]);
  for (let index = 1; index < parts.length; index++) {
    current = current.message(parts[index]);
  }
  return new Promise((resolve, reject) => {
    const accepted = current.submit((result, replyParts) => {
      if (result !== 0) {
        reject(new Error(`request failed with result ${result}`));
        return;
      }
      resolve(replyParts);
    });
    if (!accepted) {
      reject(new Error('request submit was not accepted'));
    }
  });
}

function withTimeout(promise, timeoutMs, label) {
  let timeout;
  const guard = new Promise((_, reject) => {
    timeout = setTimeout(() => reject(new Error(`${label} timed out`)), timeoutMs);
  });
  return Promise.race([promise, guard]).finally(() => clearTimeout(timeout));
}

function withAbortTimeout(action, timeoutMs, label) {
  const controller = new AbortController();
  let timeout;
  const guard = new Promise((_, reject) => {
    timeout = setTimeout(() => {
      controller.abort();
      reject(new Error(`${label} timed out`));
    }, timeoutMs);
  });
  return Promise.race([action(controller.signal), guard]).finally(() => {
    clearTimeout(timeout);
    controller.abort();
  });
}

async function assertAborted(action) {
  await assert.rejects(
    action,
    (error) => error instanceof Error && error.message === 'The operation was aborted.'
  );
}

function assertAbortedSync(action) {
  assert.throws(
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

function createMultipartRequestOperation(overrides = {}) {
  return {
    message() {
      return this;
    },
    timeout() {
      return this;
    },
    submit(callback) {
      callback(0, []);
      return true;
    },
    ...overrides
  };
}

function fakeRuntimeBackendAdapterFactory(calls, router) {
  return {
    createChannelAdapter() {
      return {
        createContext() {
          calls.push('context:create');
          return {
            nativeInstance: {},
            shutdown() {
              calls.push('context:shutdown');
            },
            async dispose() {
              calls.push('context:dispose');
            }
          };
        },
        createRouterSocket() {
          return router;
        }
      };
    },
    createMonitoringAdapter() {
      return {
        openSocketMonitor() {
          return {
            nativeInstance: {},
            onEvent() {},
            recv() {
              return null;
            },
            async dispose() {}
          };
        }
      };
    }
  };
}

function fakeRuntimeRouter(calls, received) {
  let readyHandler = () => undefined;
  let nextReceived = received;
  return {
    nativeInstance: {},
    peerWeight: 0,
    sendHighWaterMark: 0,
    receiveHighWaterMark: 0,
    sendTimeoutMs: 0,
    maxMessageSize: 0,
    disposed: false,
    setChannelName(channelName) {
      calls.push(`router:setChannelName:${channelName}`);
    },
    setRoutingId(routingId) {
      calls.push(`router:setRoutingId:${routingId}`);
    },
    bind(endpoint) {
      calls.push(`router:bind:${endpoint}`);
    },
    connect(endpoint) {
      calls.push(`router:connect:${endpoint}`);
    },
    disconnect(endpoint) {
      calls.push(`router:disconnect:${endpoint}`);
    },
    onSendReady(handler) {
      readyHandler = handler;
    },
    ready() {
      readyHandler();
    },
    recv() {
      const current = nextReceived;
      nextReceived = undefined;
      return current;
    },
    send() {
      return true;
    },
    request() {
      return true;
    },
    sendToSpot() {
      return true;
    },
    requestToSpot() {
      return true;
    },
    reply() {
      if (this.disposed) {
        throw new Error('router replied after dispose');
      }
      calls.push('router:reply');
      return captureMultipart([]);
    },
    async dispose() {
      this.disposed = true;
      calls.push('router:dispose');
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

async function waitUntil(predicate, timeoutMs = 1000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (predicate()) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 5));
  }
  throw new Error('Timed out waiting for condition.');
}

function fakeChannelAdapter({ dealer, router }) {
  return {
    createDealerSocket() {
      return dealer;
    },
    createPublisherSocket() {
      throw new Error('publisher not used');
    },
    createRouterSocket() {
      if (router === undefined) {
        throw new Error('router not used');
      }
      return router;
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

function fakeRouteRouter(options = {}) {
  let readyHandler = () => undefined;
  return {
    nativeInstance: {},
    writable: true,
    requestAttempts: 0,
    recvAttempts: 0,
    recvQueue: [],
    setChannelName(channelName) {
      this.channelName = channelName;
    },
    setRoutingId(routingId) {
      this.routingId = routingId;
    },
    bind(endpoint) {
      this.endpoint = endpoint;
    },
    connect(endpoint) {
      this.connectedEndpoint = endpoint;
    },
    attachDiscovery(discovery) {
      this.discovery = discovery;
    },
    onSendReady(handler) {
      readyHandler = handler;
    },
    ready() {
      readyHandler();
    },
    request() {
      this.requestAttempts++;
      return options.acceptWithoutReply === true;
    },
    recv() {
      this.recvAttempts++;
      return this.recvQueue.shift();
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
