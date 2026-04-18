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
  assert.equal(pub.onReceive, undefined);

  assert.equal(typeof xpub.publish, 'function');
  assert.equal(typeof xpub.onSendReady, 'function');
  assert.equal(xpub.send, undefined);
  assert.equal(typeof xpub.receiveSubscriptionEvent, 'function');
  assert.equal(typeof xpub.options, 'object');
  assert.equal(xpub.recv, undefined);
  assert.equal(xpub.subscribe, undefined);
  assert.equal(xpub.onReceive, undefined);

  assert.equal(typeof sub.subscribe, 'function');
  assert.equal(typeof sub.setSubscription, 'function');
  assert.equal(typeof sub.unsetSubscription, 'function');
  assert.equal(typeof sub.attachDiscovery, 'function');
  assert.equal(typeof sub.options, 'object');
  assert.equal(sub.send, undefined);
  assert.equal(sub.setSockOpt, undefined);
  assert.equal(sub.setRoutingId, undefined);
  assert.equal(sub.onSubscribe, undefined);

  assert.equal(typeof xsub.subscribe, 'function');
  assert.equal(typeof xsub.options, 'object');
  assert.equal(typeof xsub.options.topicsCount, 'number');
  assert.equal(xsub.topicsCount, undefined);
  assert.equal(xsub.send, undefined);
  assert.equal(xsub.setSockOpt, undefined);
  assert.equal(xsub.setRoutingId, undefined);
  assert.equal(xsub.onSubscribe, undefined);

  assert.equal(typeof pair.send, 'function');
  assert.equal(typeof pair.trySend, 'function');
  assert.equal(typeof pair.recv, 'function');
  assert.equal(typeof pair.tryRecv, 'function');
  assert.equal(typeof pair.onSendReady, 'function');
  assert.equal(typeof pair.options, 'object');
  assert.equal(pair.subscribe, undefined);
  assert.equal(pair.setSockOpt, undefined);
  assert.equal(pair.setRoutingId, undefined);
  assert.equal(pair.onReceive, undefined);

  assert.equal(typeof dealer.send, 'function');
  assert.equal(typeof dealer.trySend, 'function');
  assert.equal(typeof dealer.recv, 'function');
  assert.equal(typeof dealer.tryRecv, 'function');
  assert.equal(typeof dealer.onSendReady, 'function');
  assert.equal(typeof dealer.options, 'object');
  assert.equal(dealer.subscribe, undefined);
  assert.equal(dealer.setSockOpt, undefined);
  assert.equal(typeof dealer.setRoutingId, 'function');
  assert.equal(typeof dealer.getRoutingId, 'function');
  assert.equal(typeof dealer.attachDiscovery, 'function');
  assert.equal(dealer.onReceive, undefined);
  assert.equal(typeof stream.send, 'function');
  assert.equal(typeof stream.trySend, 'function');
  assert.equal(typeof stream.recv, 'function');
  assert.equal(typeof stream.tryRecv, 'function');
  assert.equal(stream.onReceive, undefined);
  assert.equal(typeof stream.onPacket, 'function');
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
  assert.equal(typeof router.tryRecv, 'function');
  assert.equal(typeof router.send, 'function');
  assert.equal(typeof router.trySend, 'function');
  assert.equal(typeof router.onSendReady, 'function');
  assert.equal(typeof router.options, 'object');
  assert.equal(router.subscribe, undefined);
  assert.equal(router.setSockOpt, undefined);
  assert.equal(typeof router.setRoutingId, 'function');
  assert.equal(typeof router.getRoutingId, 'function');
  assert.equal(typeof router.attachDiscovery, 'function');
  assert.equal(router.onReceive, undefined);
  assert.equal(typeof router.request, 'function');
  assert.equal(typeof router.reply, 'function');
  assert.equal(typeof router.requestToSpot, 'function');
  assert.equal(typeof router.replyToSpot, 'function');
  assert.equal(router.recvSpot, undefined);
  assert.equal(router.onSpotReceive, undefined);

  const spotNode = new zlink.SpotNode(ctx);
  assert.equal(spotNode.lastEndpoint, undefined);
  assert.equal(typeof spotNode.setRoutingId, 'function');
  assert.equal(typeof spotNode.attachChannelDealer, 'function');
  assert.equal(typeof spotNode.attachChannelDealerManual, 'function');
  assert.equal(typeof spotNode.attachPubIngress, 'function');
  spotNode.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from('node-surface')));
  assert.ok(spotNode.routingId instanceof zlink.RoutingId);
  const spot = spotNode.createSpot();
  assert.equal(typeof spot.publish, 'function');
  assert.equal(typeof spot.sendChannel, 'function');
  assert.equal(typeof spot.requestChannel, 'function');
  assert.equal(typeof spot.subscribe, 'function');
  assert.equal(typeof spot.receiveSubscriptionEvent, 'function');
  assert.equal(typeof spot.onDispatchEvent, 'function');
  assert.equal(typeof spot.onRoutedReceive, 'function');
  assert.equal(spot.onSubscribe, undefined);
  spot.close();
  spotNode.close();

  const monitor = stream.monitorOpen(zlink.MonitorEvent.ALL);
  assert.equal(typeof monitor.recv, 'function');
  assert.equal(typeof monitor.onEvent, 'function');
  assert.equal(typeof zlink.MonitorSocket.ignoreHandler, 'function');
  monitor.close();

  const poller = new zlink.Poller();
  assert.equal(typeof poller.size, 'number');
  poller.close();

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
