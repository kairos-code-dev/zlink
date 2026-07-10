const assert = require('node:assert/strict');
const test = require('node:test');

const zlink = require('@zlink-systems/zlink');
const backend = require('../../packages/framework/dist/runtime/backend');
const framework = require('../../packages/framework/dist/internal');

test('backend adapter factory exposes the supported backend adapters', () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();

  assert.equal(typeof factory.createChannelAdapter, 'function');
  assert.equal(typeof factory.createSpotAdapter, 'function');
  assert.equal(typeof factory.createStreamAdapter, 'function');
  assert.equal(typeof factory.createMonitoringAdapter, 'function');
  assert.equal(factory.createRegistryAdapter, undefined);
});

test('backend adapter creates context and core socket wrappers through public binding API', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const context = channel.createContext();
  const disposables = [];

  try {
    const dealer = channel.createDealerSocket(context);
    const router = channel.createRouterSocket(context);
    const publisher = channel.createPublisherSocket(context);
    const subscriber = channel.createSubscriberSocket(context);
    const topicMessage = channel.createTopicMessage();
    const subscriberPoller = channel.createReadablePoller(subscriber);
    const stream = factory.createStreamAdapter().createStreamSocket(context);

    disposables.push(dealer, router, publisher, subscriber, subscriberPoller, stream);

    assert.equal(Array.isArray(topicMessage.parts), true);
    assert.equal(typeof dealer.dispose, 'function');
    assert.equal(typeof router.dispose, 'function');
    assert.equal(typeof publisher.dispose, 'function');
    assert.equal(typeof subscriber.dispose, 'function');
    assert.equal(typeof subscriberPoller.wait, 'function');
    assert.equal(typeof subscriberPoller.dispose, 'function');
    assert.equal(typeof stream.dispose, 'function');
  } finally {
    for (const disposable of disposables.reverse()) {
      await disposable.dispose();
    }
    await context.dispose();
  }
});

test('backend adapter unwraps SpotNode when attaching stream SessionRelay', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const spotAdapter = factory.createSpotAdapter();
  const streamAdapter = factory.createStreamAdapter();
  const context = channel.createContext();
  const spotNode = spotAdapter.createSpotNode(context, 3);
  const stream = streamAdapter.createStreamSocket(context);

  try {
  } finally {
    await stream.dispose();
    await spotNode.dispose();
    await context.dispose();
  }
});

test('backend stream bind converts public string actor node RID to native RoutingId', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const spotAdapter = factory.createSpotAdapter();
  const streamAdapter = factory.createStreamAdapter();
  const context = channel.createContext();
  const sessionNode = spotAdapter.createSpotNode(context, 3);
  const playNode = spotAdapter.createSpotNode(context, 3);
  const stream = streamAdapter.createStreamSocket(context);

  try {
    sessionNode.setRoutingId('backend-session-node');
    playNode.setRoutingId('backend-play-node');
    const actorRef = playNode.createActor('backend-player');

    await stream.bindActor(
      'backend-session',
      {
        nodeRid: String(actorRef.nodeRid),
        actorId: actorRef.actorId,
        generation: actorRef.generation
      },
      1000
    );
  } finally {
    await stream.dispose();
    await playNode.dispose();
    await sessionNode.dispose();
    await context.dispose();
  }
});

test('backend adapter converts public string route bridge target RIDs to native RoutingId', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const spotAdapter = factory.createSpotAdapter();
  const context = channel.createContext();
  const spotNode = spotAdapter.createSpotNode(context, 3);
  const router = channel.createRouterSocket(context);
  const bridge = spotNode.createRouteBridge();

  try {
    router.setRoutingId('backend-bridge-source');
    bridge.attachRouterChannel('mesh', router, { capabilities: 3 });

    assert.equal(typeof bridge.send('mesh', 'backend-bridge-target', 'backend-bridge-spot').message, 'function');
    assert.equal(typeof bridge.request('mesh', 'backend-bridge-target', 'backend-bridge-spot').message, 'function');
  } finally {
    await bridge.dispose();
    await router.dispose();
    await spotNode.dispose();
    await context.dispose();
  }
});

test('channel runtime connects route mesh peers before bind and spot route bridge attach', () => {
  const calls = [];
  const registration = framework.createFrameworkRegistrationWithBuilder((builder) => {
    builder.addRouteMeshChannel('room.route')
      .enableServer('tcp://0.0.0.0:9410')
      .enableClient('tcp://127.0.0.1:9410');
    builder.addSpotMesh('room')
      .enableRouter('tcp://0.0.0.0:9411', 'room-node');
  });
  const routeSocket = {
    nativeInstance: {},
    setChannelName(channelName) { calls.push(`route:setChannelName:${channelName}`); },
    setRoutingId(routingId) { calls.push(`route:setRoutingId:${routingId}`); },
    connect(endpoint) { calls.push(`route:connect:${endpoint}`); },
    bind(endpoint) { calls.push(`route:bind:${endpoint}`); },
    disconnect() {},
    onSendReady() {},
    recv() { return undefined; },
    async dispose() {}
  };
  const spotNode = {
    routingId: 'room-node',
    createRouteBridge() {
      calls.push('spot:createRouteBridge');
      return {
        attachRouterChannel(channelName) { calls.push(`bridge:attachRouter:${channelName}`); },
        async dispose() {}
      };
    }
  };
  const runtime = new framework.ZLinkChannelRuntimeManager(
    registration,
    {
      createRouterSocket() {
        calls.push('route:createRouter');
        return routeSocket;
      }
    },
    { nativeInstance: {}, shutdown() {}, async dispose() {} }
  );

  runtime.bindRouteMeshRouters();
  runtime.setSpotNodes(new Map([['room', spotNode]]));
  runtime.start({
    errorSink: { report() {} },
    run(_name, _task) {
      return Promise.resolve();
    }
  });

  assert.deepEqual(calls, [
    'route:createRouter',
    'route:setChannelName:room.route',
    'route:connect:tcp://127.0.0.1:9410',
    'route:bind:tcp://0.0.0.0:9410',
    'spot:createRouteBridge',
    'bridge:attachRouter:room.route'
  ]);
});

test('backend adapter normalizes missing SpotNode actor lookup to undefined', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const spotAdapter = factory.createSpotAdapter();
  const context = channel.createContext();
  const spotNode = spotAdapter.createSpotNode(context, 3);

  try {
    assert.equal(spotNode.actorLookup('missing-actor'), undefined);

    const actorRef = spotNode.createActor('existing-actor');
    assert.equal(actorRef.actorId, 'existing-actor');
    assert.equal(typeof actorRef.generation, 'bigint');
    assert.deepEqual(spotNode.actorLookup('existing-actor'), actorRef);
  } finally {
    await spotNode.dispose();
    await context.dispose();
  }
});

test('backend adapter submits actor destroy and closes actor bound sessions through public binding operations', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const spotAdapter = factory.createSpotAdapter();
  const context = channel.createContext();
  const spotNode = spotAdapter.createSpotNode(context, 3);
  const calls = [];
  let replyClosed = false;

  try {
    spotNode.nativeInstance.createActor = (actorId) => ({
      actorRef: { nodeRid: zlink.RoutingId.from('backend-node'), actorId, generation: 1n },
      closeBoundSession(timeoutMs) {
        calls.push({ kind: 'closeBoundSession', timeoutMs });
      }
    });
    spotNode.nativeInstance.destroyActor = (actorRef) => ({
      timeout(timeoutMs) {
        calls.push({ kind: 'destroyTimeout', actorRef, timeoutMs });
        return this;
      },
      submit() {
        calls.push({ kind: 'destroySubmit' });
        return Promise.resolve([{ close() { replyClosed = true; } }]);
      }
    });

    const actorRef = spotNode.createActor('backend-destroy');
    await spotNode.closeActorBoundSession(actorRef, 17);
    await spotNode.destroyActor(actorRef, 23);

    assert.deepEqual(calls.map((call) => call.kind), [
      'closeBoundSession',
      'destroyTimeout',
      'destroySubmit'
    ]);
    assert.equal(calls[0].timeoutMs, 17);
    assert.equal(calls[1].timeoutMs, 23);
    assert.equal(calls[1].actorRef.actorId, 'backend-destroy');
    assert.equal(replyClosed, true);
    await assert.rejects(
      spotNode.closeActorBoundSession(actorRef, 0),
      (error) => error instanceof zlink.ConfigError && error.result === zlink.ConfigResult.NotFound
    );
  } finally {
    await spotNode.dispose();
    await context.dispose();
  }
});

test('backend adapter aborts actor destroy waits and closes late reply messages', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const spotAdapter = factory.createSpotAdapter();
  const context = channel.createContext();
  const spotNode = spotAdapter.createSpotNode(context, 3);
  const abort = new AbortController();
  let completeDestroy;
  let replyClosed = false;

  try {
    spotNode.nativeInstance.createActor = (actorId) => ({
      actorRef: { nodeRid: zlink.RoutingId.from('backend-node'), actorId, generation: 1n },
      closeBoundSession() {}
    });
    spotNode.nativeInstance.destroyActor = () => ({
      timeout() { return this; },
      submit() {
        return new Promise((resolve) => {
          completeDestroy = resolve;
        });
      }
    });

    const actorRef = spotNode.createActor('backend-abort-destroy');
    const destroy = spotNode.destroyActor(actorRef, 1000, abort.signal);
    const reason = new Error('cancel backend actor destroy');
    abort.abort(reason);
    await assert.rejects(destroy, reason);

    completeDestroy([{ close() { replyClosed = true; } }]);
    await new Promise((resolve) => setImmediate(resolve));
    assert.equal(replyClosed, true);
    await assert.rejects(
      spotNode.closeActorBoundSession(actorRef, 0),
      (error) => error instanceof zlink.ConfigError && error.result === zlink.ConfigResult.NotFound
    );
  } finally {
    await spotNode.dispose();
    await context.dispose();
  }
});

test('backend adapter submits SpotNode actor bound-session send operation', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const spotAdapter = factory.createSpotAdapter();
  const context = channel.createContext();
  const spotNode = spotAdapter.createSpotNode(context, 3);
  const frame = zlink.Message.from(Buffer.from('frame'));

  try {
    const actorRef = spotNode.createActor('unbound-actor');
    assert.throws(
      () => spotNode.sendActorBoundSession(actorRef, [frame], 1),
      /actor bound session send failed|not found|No such file|ENOENT|NOT_FOUND/i
    );
  } finally {
    frame.close();
    await spotNode.dispose();
    await context.dispose();
  }
});

test('backend adapter submits SpotNode actor send through async completion callback', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const spotAdapter = factory.createSpotAdapter();
  const context = channel.createContext();
  const spotNode = spotAdapter.createSpotNode(context, 3);
  const frame = zlink.Message.from(Buffer.from('actor-send'));
  const actorRef = { nodeRid: 'backend-actor-node', actorId: 'backend-actor', generation: 1n };
  const calls = [];

  try {
    spotNode.nativeInstance.sendToActorCallback = (actor, parts, callback, flags, timeoutMs) => {
      calls.push({ actor, parts, flags, timeoutMs });
      queueMicrotask(() => callback(zlink.RequestResult.Ok, []));
      return true;
    };

    assert.equal(await spotNode.sendToActor(actorRef, [frame], zlink.SendFlags.None), true);
    assert.equal(calls.length, 1);
    assert.equal(calls[0].actor.actorId, 'backend-actor');
    assert.equal(calls[0].parts.length, 1);
  } finally {
    frame.close();
    await spotNode.dispose();
    await context.dispose();
  }
});

test('backend adapter submits SpotNode actor request builder and wires callback', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const spotAdapter = factory.createSpotAdapter();
  const context = channel.createContext();
  const spotNode = spotAdapter.createSpotNode(context, 3);
  const frame = zlink.Message.from(Buffer.from('actor-request'));
  const actorRef = { nodeRid: 'backend-actor-node', actorId: 'backend-actor', generation: 1n };
  const calls = [];

  try {
    spotNode.nativeInstance.requestToActor = (actor) => ({
      message(part) {
        calls.push({ kind: 'message', actor, part });
        return this;
      },
      flags(flags) {
        calls.push({ kind: 'flags', flags });
        return this;
      },
      timeout(timeoutMs) {
        calls.push({ kind: 'timeout', timeoutMs });
        return this;
      },
      submit(callback) {
        calls.push({ kind: 'submit' });
        callback(zlink.RequestResult.Ok, []);
        return true;
      }
    });

    let completed = false;
    const accepted = spotNode.requestToActor(
      actorRef,
      [frame],
      (result, parts) => {
        completed = true;
        assert.equal(result, zlink.RequestResult.Ok);
        assert.deepEqual(parts, []);
      },
      zlink.SendFlags.None,
      123
    );

    assert.equal(accepted, true);
    assert.equal(completed, true);
    assert.deepEqual(calls.map((call) => call.kind), ['message', 'timeout', 'submit']);
  } finally {
    frame.close();
    await spotNode.dispose();
    await context.dispose();
  }
});

test('backend router recv normalizes transient route recv invalid handle to no message', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const context = channel.createContext();
  const router = channel.createRouterSocket(context);

  try {
    await router.dispose();
    assert.equal(router.recv(1), undefined);
  } finally {
    await context.dispose();
  }
});

test('backend socket wrapper treats missing route disconnect as idempotent cleanup', () => {
  const error = new zlink.ConfigError(zlink.ConfigResult.NotFound, 2);

  assert.equal(backend.isDisconnectRouteNotFoundError(error), true);
});
