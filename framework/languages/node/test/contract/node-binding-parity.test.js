const assert = require('node:assert/strict');
const test = require('node:test');

const zlink = require('@zlink-systems/zlink');

test('node binding exposes the public API required by framework P2-P8', () => {
  for (const name of [
    'createContext',
    'createDealerSocket',
    'createRouterSocket',
    'createPubSocket',
    'createSubSocket',
    'createSpotNode',
    'createStreamSocket'
  ]) {
    assert.equal(typeof zlink[name], 'function', `${name} must be public`);
  }
  for (const name of [
    'createDiscovery',
    'createRegistry',
    'createRegistryQueryClient'
  ]) {
    assert.equal(zlink[name], undefined, `${name} must not be public`);
  }
});

test('node binding public API covers monitor and session relay wrappers without discovery registry wrappers', () => {
  const context = zlink.createContext();
  const closeables = [];

  try {
    const dealer = zlink.createDealerSocket(context);
    const router = zlink.createRouterSocket(context);
    const publisher = zlink.createPubSocket(context);
    const subscriber = zlink.createSubSocket(context);
    const spotNode = zlink.createSpotNode(context);
    const stream = zlink.createStreamSocket(context);
    const monitor = dealer.monitorOpen();

    closeables.push(monitor, stream, spotNode, subscriber, publisher, router, dealer);

    assert.equal(dealer.attachDiscovery, undefined);
    assert.equal(router.attachDiscovery, undefined);
    assert.equal(publisher.attachDiscovery, undefined);
    assert.equal(subscriber.attachDiscovery, undefined);
    assert.equal(spotNode.attachDiscovery, undefined);
    assert.equal(typeof spotNode.createActor, 'function');
    assert.equal(typeof stream.setPacketHandler, 'function');
    assert.equal(typeof stream.bindActor, 'function');
    assert.equal(typeof stream.unbindActor, 'function');
    assert.equal(typeof stream.sendBoundActor, 'function');
    assert.equal(typeof stream.boundActors, 'function');
    assert.equal(typeof monitor.onEvent, 'function');
    assert.equal(typeof monitor.recv, 'function');
    assert.equal(typeof monitor.status, 'function');
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
