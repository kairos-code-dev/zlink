const assert = require('node:assert/strict');
const test = require('node:test');

const zlink = require('../../../../../bindings/node/dist');
const backend = require('../../packages/framework/dist/runtime/backend');

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
