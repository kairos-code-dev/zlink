const assert = require('node:assert/strict');
const test = require('node:test');

const zlink = require('../../../../../bindings/node/dist');

test('node binding exposes the public API required by framework P2-P8', () => {
  for (const name of [
    'createContext',
    'createDealerSocket',
    'createRouterSocket',
    'createPubSocket',
    'createSubSocket',
    'createDiscovery',
    'createSpotNode',
    'createStreamSocket',
    'createRegistry',
    'createRegistryQueryClient'
  ]) {
    assert.equal(typeof zlink[name], 'function', `${name} must be public`);
  }
});

test('node binding public API covers discovery registry monitor and actor gateway wrappers', () => {
  const context = zlink.createContext();
  const closeables = [];

  try {
    const dealer = zlink.createDealerSocket(context);
    const router = zlink.createRouterSocket(context);
    const publisher = zlink.createPubSocket(context);
    const subscriber = zlink.createSubSocket(context);
    const discovery = zlink.createDiscovery(context, zlink.AutoConnectType.ClientServer, 'p15-channel');
    const spotNode = zlink.createSpotNode(context);
    const stream = zlink.createStreamSocket(context);
    const registry = zlink.createRegistry(context);
    const registryQueryClient = zlink.createRegistryQueryClient(context);
    const monitor = dealer.monitorOpen();

    closeables.push(monitor, registryQueryClient, registry, stream, spotNode, discovery, subscriber, publisher, router, dealer);

    assert.equal(typeof dealer.attachDiscovery, 'function');
    assert.equal(typeof router.attachDiscovery, 'function');
    assert.equal(typeof publisher.attachDiscovery, 'function');
    assert.equal(typeof subscriber.attachDiscovery, 'function');
    assert.equal(typeof discovery.connectRegistry, 'function');
    assert.equal(typeof discovery.memberPeers, 'function');
    assert.equal(typeof spotNode.attachDiscovery, 'function');
    assert.equal(typeof spotNode.createActor, 'function');
    assert.equal(typeof stream.setPacketHandler, 'function');
    assert.equal(typeof stream.attachActorGateway, 'function');
    assert.equal(typeof stream.bindActor, 'function');
    assert.equal(typeof stream.unbindActor, 'function');
    assert.equal(typeof stream.sendBoundActor, 'function');
    assert.equal(typeof stream.boundActors, 'function');
    assert.equal(typeof registry.memberPeers, 'function');
    assert.equal(typeof registryQueryClient.topology, 'function');
    assert.equal(typeof monitor.onEvent, 'function');
    assert.equal(typeof monitor.recv, 'function');
    assert.equal(typeof monitor.status, 'function');

    stream.attachActorGateway(spotNode);
    const actor = spotNode.createActor('p15-actor');
    const sessionRid = zlink.RoutingId.from('p15-session');
    assert.equal(typeof stream.bindActor(sessionRid, actor.ref()).submit, 'function');
    assert.equal(typeof stream.unbindActor(sessionRid, 'p15-actor').submit, 'function');
    assert.equal(typeof stream.sendBoundActor(sessionRid, 'p15-actor').submit, 'function');
    assert.equal(Array.isArray(stream.boundActors(sessionRid)), true);
    actor.close(0);
  } finally {
    for (const closeable of closeables) {
      closeable.close();
    }
    context.close();
  }
});
