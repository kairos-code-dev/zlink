'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist');

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
  assert.equal(typeof pub.tryPublish, 'function');
  assert.equal(pub.send, undefined);
  assert.equal(pub.receive, undefined);
  assert.equal(pub.subscribe, undefined);

  assert.equal(typeof xpub.publish, 'function');
  assert.equal(typeof xpub.tryPublish, 'function');
  assert.equal(xpub.send, undefined);
  assert.equal(typeof xpub.receiveSubscriptionEvent, 'function');
  assert.equal(typeof xpub.tryReceiveSubscriptionEvent, 'function');
  assert.equal(xpub.receive, undefined);
  assert.equal(xpub.subscribe, undefined);

  assert.equal(typeof sub.subscribe, 'function');
  assert.equal(typeof sub.trySubscribe, 'function');
  assert.equal(typeof sub.setSubscription, 'function');
  assert.equal(typeof sub.unsetSubscription, 'function');
  assert.equal(typeof sub.subscribeHandler, 'function');
  assert.equal(sub.send, undefined);
  assert.equal(sub.setSockOpt, undefined);
  assert.equal(sub.setRoutingId, undefined);

  assert.equal(typeof xsub.subscribe, 'function');
  assert.equal(typeof xsub.trySubscribe, 'function');
  assert.equal(typeof xsub.subscribeHandler, 'function');
  assert.equal(xsub.send, undefined);
  assert.equal(xsub.setSockOpt, undefined);
  assert.equal(xsub.setRoutingId, undefined);

  assert.equal(typeof pair.send, 'function');
  assert.equal(typeof pair.trySend, 'function');
  assert.equal(typeof pair.receive, 'function');
  assert.equal(typeof pair.tryReceive, 'function');
  assert.equal(typeof pair.recvHandler, 'function');
  assert.equal(pair.subscribe, undefined);
  assert.equal(pair.setSockOpt, undefined);
  assert.equal(pair.setRoutingId, undefined);

  assert.equal(typeof dealer.send, 'function');
  assert.equal(typeof dealer.trySend, 'function');
  assert.equal(typeof dealer.receive, 'function');
  assert.equal(typeof dealer.tryReceive, 'function');
  assert.equal(typeof dealer.recvHandler, 'function');
  assert.equal(dealer.subscribe, undefined);
  assert.equal(dealer.setSockOpt, undefined);
  assert.equal(typeof dealer.setRoutingId, 'function');
  assert.equal(typeof dealer.getRoutingId, 'function');

  assert.equal(typeof stream.send, 'function');
  assert.equal(typeof stream.trySend, 'function');
  assert.equal(typeof stream.receive, 'function');
  assert.equal(typeof stream.tryReceive, 'function');
  assert.equal(typeof stream.recvHandler, 'function');
  assert.equal(stream.subscribe, undefined);
  assert.equal(stream.streamAttach, undefined);
  assert.equal(stream.setSockOpt, undefined);
  assert.equal(stream.setRoutingId, undefined);

  assert.equal(typeof router.receive, 'function');
  assert.equal(typeof router.tryReceive, 'function');
  assert.equal(typeof router.send, 'function');
  assert.equal(typeof router.trySend, 'function');
  assert.equal(typeof router.recvHandler, 'function');
  assert.equal(router.subscribe, undefined);
  assert.equal(router.setSockOpt, undefined);
  assert.equal(typeof router.setRoutingId, 'function');
  assert.equal(typeof router.getRoutingId, 'function');

  const monitor = stream.monitorOpen(zlink.MonitorEvent.ALL);
  assert.equal(typeof monitor.recv, 'function');
  assert.equal(typeof monitor.tryRecv, 'function');
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
