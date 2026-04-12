'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist/canonical');

test('canonical socket classes expose only directionally valid methods', () => {
  const ctx = new zlink.Context();
  const pub = new zlink.PubSocket(ctx);
  const xpub = new zlink.XPubSocket(ctx);
  const sub = new zlink.SubSocket(ctx);
  const xsub = new zlink.XSubSocket(ctx);
  const pair = new zlink.PairSocket(ctx);
  const dealer = new zlink.DealerSocket(ctx);
  const router = new zlink.RouterSocket(ctx);
  const stream = new zlink.StreamSocket(ctx);

  assert.equal(typeof pub.publish, 'function');
  assert.equal(typeof pub.onSendReady, 'function');
  assert.equal(typeof pub.attachDiscovery, 'function');
  assert.equal(typeof pub.options, 'object');
  assert.equal(typeof Object.getOwnPropertyDescriptor(
    Object.getPrototypeOf(pub.options),
    'verbose'
  ).set, 'function');
  assert.equal(pub.send, undefined);
  assert.equal(pub.recv, undefined);
  assert.equal(pub.subscribe, undefined);

  assert.equal(typeof xpub.publish, 'function');
  assert.equal(typeof xpub.onSendReady, 'function');
  assert.equal(xpub.send, undefined);
  assert.equal(typeof xpub.receiveSubscriptionEvent, 'function');
  assert.equal(typeof xpub.options, 'object');
  assert.equal(xpub.recv, undefined);
  assert.equal(xpub.subscribe, undefined);

  assert.equal(typeof sub.subscribe, 'function');
  assert.equal(typeof sub.setSubscription, 'function');
  assert.equal(typeof sub.unsetSubscription, 'function');
  assert.equal(typeof sub.onSubscribe, 'function');
  assert.equal(typeof sub.attachDiscovery, 'function');
  assert.equal(typeof sub.options, 'object');
  assert.equal(sub.send, undefined);
  assert.equal(sub.setSockOpt, undefined);
  assert.equal(sub.setRoutingId, undefined);

  assert.equal(typeof xsub.subscribe, 'function');
  assert.equal(typeof xsub.onSubscribe, 'function');
  assert.equal(typeof xsub.options, 'object');
  assert.equal(typeof xsub.options.topicsCount, 'number');
  assert.equal(xsub.topicsCount, undefined);
  assert.equal(xsub.send, undefined);
  assert.equal(xsub.setSockOpt, undefined);
  assert.equal(xsub.setRoutingId, undefined);

  assert.equal(typeof pair.send, 'function');
  assert.equal(typeof pair.recv, 'function');
  assert.equal(typeof pair.onReceive, 'function');
  assert.equal(typeof pair.onSendReady, 'function');
  assert.equal(typeof pair.options, 'object');
  assert.equal(pair.subscribe, undefined);
  assert.equal(pair.setSockOpt, undefined);
  assert.equal(pair.setRoutingId, undefined);

  assert.equal(typeof dealer.send, 'function');
  assert.equal(typeof dealer.recv, 'function');
  assert.equal(typeof dealer.onReceive, 'function');
  assert.equal(typeof dealer.onSendReady, 'function');
  assert.equal(typeof dealer.options, 'object');
  assert.equal(dealer.subscribe, undefined);
  assert.equal(dealer.setSockOpt, undefined);
  assert.equal(typeof dealer.setRoutingId, 'function');
  assert.equal(typeof dealer.getRoutingId, 'function');
  assert.equal(typeof dealer.attachDiscovery, 'function');
  assert.equal(typeof zlink.RequestDealer, 'function');
  assert.equal(typeof zlink.RequestRouter, 'function');

  assert.equal(typeof stream.send, 'function');
  assert.equal(typeof stream.recv, 'function');
  assert.equal(typeof stream.onReceive, 'function');
  assert.equal(typeof stream.onSendReady, 'function');
  assert.equal(typeof stream.options, 'object');
  assert.equal(stream.subscribe, undefined);
  assert.equal(stream.connect, undefined);
  assert.equal(stream.disconnect, undefined);
  assert.equal(stream.streamAttach, undefined);
  assert.equal(stream.setSockOpt, undefined);
  assert.equal(typeof stream.setRoutingId, 'function');
  assert.equal(typeof stream.getRoutingId, 'function');

  assert.equal(typeof router.recv, 'function');
  assert.equal(typeof router.send, 'function');
  assert.equal(typeof router.onReceive, 'function');
  assert.equal(typeof router.onSendReady, 'function');
  assert.equal(typeof router.options, 'object');
  assert.equal(router.subscribe, undefined);
  assert.equal(router.setSockOpt, undefined);
  assert.equal(typeof router.setRoutingId, 'function');
  assert.equal(typeof router.getRoutingId, 'function');
  assert.equal(typeof router.attachDiscovery, 'function');

  const spotNode = new zlink.SpotNode(ctx);
  assert.equal(spotNode.lastEndpoint, undefined);
  spotNode.close();

  const monitor = stream.monitorOpen(zlink.MonitorEvent.ALL);
  assert.equal(typeof monitor.recv, 'function');
  assert.equal(typeof monitor.onEvent, 'function');
  monitor.close();

  stream.close();
  router.close();
  dealer.close();
  pair.close();
  xsub.close();
  sub.close();
  xpub.close();
  pub.close();
  ctx.close();
});
