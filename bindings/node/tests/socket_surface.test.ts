'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('@zlink-systems/zlink');

test('canonical socket classes expose only directionally valid methods', () => {
  const ctx = zlink.createContext();
  const pub = zlink.createPubSocket(ctx);
  const xpub = zlink.createXPubSocket(ctx);
  const sub = zlink.createSubSocket(ctx);
  const xsub = zlink.createXSubSocket(ctx);
  const pair = zlink.createPairSocket(ctx);
  const dealer = zlink.createDealerSocket(ctx);
  const router = zlink.createRouterSocket(ctx);
  const stream = zlink.createStreamSocket(ctx);

  assert.equal(typeof pub.publish, 'function');
  assert.equal(typeof pub.setSendReadyHandler, 'function');
  assert.equal(pub.attachDiscovery, undefined);
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
  assert.equal(typeof xpub.setSendReadyHandler, 'function');
  assert.equal(xpub.send, undefined);
  assert.equal(typeof xpub.receiveSubscriptionEvent, 'function');
  assert.equal(typeof xpub.options, 'object');
  assert.equal(xpub.recv, undefined);
  assert.equal(xpub.subscribe, undefined);
  assert.equal(xpub.onReceive, undefined);

  assert.equal(typeof sub.subscribe, 'function');
  assert.equal(sub.subscribePayloadInto, undefined);
  assert.equal(typeof sub.setSubscription, 'function');
  assert.equal(typeof sub.unsetSubscription, 'function');
  assert.equal(sub.attachDiscovery, undefined);
  assert.equal(typeof sub.options, 'object');
  assert.equal(sub.send, undefined);
  assert.equal(sub.setSockOpt, undefined);
  assert.equal(sub.setRoutingId, undefined);
  assert.equal(sub.onSubscribe, undefined);

  assert.equal(typeof xsub.subscribe, 'function');
  assert.equal(xsub.subscribePayloadInto, undefined);
  assert.equal(typeof xsub.options, 'object');
  assert.equal(typeof xsub.options.topicsCount, 'number');
  assert.equal(xsub.topicsCount, undefined);
  assert.equal(xsub.send, undefined);
  assert.equal(xsub.setSockOpt, undefined);
  assert.equal(xsub.setRoutingId, undefined);
  assert.equal(xsub.onSubscribe, undefined);

  assert.equal(typeof pair.send, 'function');
  assert.equal(pair.trySend, undefined);
  assert.equal(typeof pair.recv, 'function');
  assert.equal(pair.tryRecv, undefined);
  assert.equal(typeof pair.setSendReadyHandler, 'function');
  assert.equal(typeof pair.options, 'object');
  assert.equal(pair.subscribe, undefined);
  assert.equal(pair.setSockOpt, undefined);
  assert.equal(pair.setRoutingId, undefined);
  assert.equal(pair.onReceive, undefined);

  assert.equal(typeof dealer.send, 'function');
  assert.equal(dealer.trySend, undefined);
  assert.equal(typeof dealer.recv, 'function');
  assert.equal(dealer.tryRecv, undefined);
  assert.equal(typeof dealer.setSendReadyHandler, 'function');
  assert.equal(typeof dealer.options, 'object');
  assert.equal(dealer.subscribe, undefined);
  assert.equal(dealer.setSockOpt, undefined);
  assert.equal(typeof dealer.setRoutingId, 'function');
  assert.equal(typeof dealer.getRoutingId, 'function');
  assert.equal(dealer.attachDiscovery, undefined);
  assert.equal(dealer.onReceive, undefined);
  assert.equal(typeof stream.send, 'function');
  assert.equal(stream.trySend, undefined);
  assert.equal(typeof stream.recv, 'function');
  assert.equal(stream.tryRecv, undefined);
  assert.equal(stream.onReceive, undefined);
  assert.equal(typeof stream.setPacketHandler, 'function');
  assert.equal(typeof stream.setSendReadyHandler, 'function');
  assert.equal(typeof stream.options, 'object');
  assert.equal(stream.subscribe, undefined);
  assert.equal(stream.connect, undefined);
  assert.equal(stream.disconnect, undefined);
  assert.equal(typeof stream.disconnectRid, 'function');
  assert.equal(stream.streamAttach, undefined);
  assert.equal(stream.setSockOpt, undefined);
  assert.equal(typeof stream.setRoutingId, 'function');
  assert.equal(typeof stream.getRoutingId, 'function');
  assert.equal(typeof stream.bindActor, 'function');
  assert.equal(typeof stream.unbindActor, 'function');
  assert.equal(typeof stream.sendBoundActor, 'function');

  assert.equal(typeof router.recv, 'function');
  assert.equal(router.tryRecv, undefined);
  assert.equal(typeof router.send, 'function');
  assert.equal(router.trySend, undefined);
  assert.equal(typeof router.setSendReadyHandler, 'function');
  assert.equal(typeof router.options, 'object');
  assert.equal(router.subscribe, undefined);
  assert.equal(router.setSockOpt, undefined);
  assert.equal(typeof router.setRoutingId, 'function');
  assert.equal(typeof router.getRoutingId, 'function');
  assert.equal(router.attachDiscovery, undefined);
  assert.equal(router.onReceive, undefined);
  assert.equal(typeof router.request, 'function');
  assert.equal(typeof router.reply, 'function');
  assert.equal(typeof router.requestToSpot, 'function');
  assert.equal(typeof router.replyToSpot, 'function');
  assert.equal(router.recvSpot, undefined);
  assert.equal(router.onSpotReceive, undefined);

  const spotNode = zlink.createSpotNode(ctx);
  assert.equal(spotNode.lastEndpoint, undefined);
  assert.equal(typeof spotNode.setPubBind, 'function');
  assert.equal(typeof spotNode.setRouterBind, 'function');
  assert.equal(typeof spotNode.setRoutingId, 'function');
  assert.equal(typeof spotNode.setPublisherRoutingId, 'function');
  assert.equal(typeof spotNode.setSubscriberRoutingId, 'function');
  assert.equal(typeof spotNode.createRouteBridge, 'function');
  assert.equal(typeof spotNode.createPublisher, 'function');
  assert.equal(typeof spotNode.createActor, 'function');
  assert.equal(typeof spotNode.actorLookup, 'function');
  assert.equal(zlink.SpotNode, undefined);
  assert.equal(typeof spotNode.remoteActorGetRef, 'function');
  assert.equal(typeof spotNode.destroyActor, 'function');
  assert.equal(typeof spotNode.joinActor, 'function');
  assert.equal(typeof spotNode.leaveActor, 'function');
  assert.equal(typeof spotNode.entrySpot, 'function');
  assert.equal(typeof spotNode.spotLookup, 'function');
  assert.equal(typeof spotNode.getOrCreateSpot, 'function');
  assert.equal(typeof spotNode.spots, 'function');
  assert.equal(typeof spotNode.actors, 'function');
  spotNode.setRoutingId(zlink.RoutingId.from(Buffer.from('node-surface')));
  assert.ok(spotNode.routingId instanceof zlink.RoutingId);
  const actor = spotNode.createActor('surface-actor');
  const actorRef = actor.ref();
  assert.equal(actorRef.actorId, 'surface-actor');
  assert.equal(actorRef.generation === 0n, false);
  assert.equal(typeof actor.join, 'function');
  assert.equal(typeof actor.leave, 'function');
  assert.equal(typeof actor.recvPart, 'function');
  assert.equal(typeof actor.sendBoundSession, 'function');
  assert.equal(typeof actor.closeBoundSession, 'function');
  const spot = spotNode.createSpot();
  const bridge = spotNode.createRouteBridge();
  const publisher = spotNode.createPublisher();
  assert.equal(typeof bridge.attachRouterChannel, 'function');
  assert.equal(typeof bridge.send, 'function');
  assert.equal(typeof bridge.request, 'function');
  assert.equal(typeof bridge.close, 'function');
  assert.equal(typeof publisher.publish, 'function');
  assert.equal(typeof publisher.close, 'function');
  assert.equal(typeof spot.publish, 'function');
  assert.equal(spot.publishFrom, undefined);
  assert.equal(typeof spot.sendToChannel, 'function');
  assert.equal(typeof spot.sendToSpot, 'function');
  assert.equal(spot.sendToSpotFrom, undefined);
  assert.equal(typeof spot.requestToChannel, 'function');
  assert.equal(spot.requestToSpotFrom, undefined);
  assert.equal(typeof spot.subscribe, 'function');
  assert.equal(spot.subscribePayloadInto, undefined);
  assert.equal(spot.recvRoutedPayloadInto, undefined);
  assert.equal(spot.receiveSubscriptionEvent, undefined);
  assert.equal(typeof spot.setDispatchHandler, 'function');
  assert.equal(spot.onRoutedReceive, undefined);
  assert.equal(typeof spot.recvActorLifecycle, 'function');
  assert.equal(typeof spot.recvActorJoin, 'function');
  assert.equal(typeof spot.replyActorJoin, 'function');
  assert.equal(typeof spot.actors, 'function');
  assert.equal(spot.onSubscribe, undefined);
  publisher.close();
  bridge.close();
  spot.close();
  actor.close();
  spotNode.close();

  const monitor = stream.monitorOpen();
  assert.equal(typeof monitor.recv, 'function');
  assert.equal(typeof monitor.onEvent, 'function');
  assert.equal(zlink.MonitorSocket, undefined);
  monitor.close();

  const poller = zlink.createPoller();
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

test('value and handle objects without documented constructors are runtime guarded', () => {
  const ctx = zlink.createContext();
  const pub = zlink.createPubSocket(ctx);

  assert.throws(() => new zlink.RoutingId(Buffer.from('id')), TypeError);
  assert.throws(() => new zlink.Received([]), TypeError);
  assert.doesNotThrow(() => new zlink.TopicMessage());
  assert.doesNotThrow(() => new zlink.SubscriptionEvent());
  assert.throws(() => new zlink.MonitorEvent({ event: zlink.MonitorEventType.Connected, value: 0 }), TypeError);
  assert.equal(zlink.Actor, undefined);
  assert.equal(zlink.Spot, undefined);
  assert.equal(zlink.CommonSocketOptions, undefined);
  assert.equal(zlink.ContextOptions, undefined);
  assert.equal(zlink.RoutingId.fromBytes, undefined);
  assert.equal(zlink.RoutingId.fromString, undefined);

  assert.ok(zlink.RoutingId.from(Buffer.from('id')) instanceof zlink.RoutingId);
  assert.ok(zlink.Message.from('payload') instanceof zlink.Message);

  pub.close();
  ctx.close();
});

test('spot operation builders keep payload and single-submit validation centralized', () => {
  const ctx = zlink.createContext();
  const node = zlink.createSpotNode(ctx);
  const spot = node.createSpot();
  const actor = node.createActor('builder-actor-owned');
  const actorRef = actor.ref();
  const rid = zlink.RoutingId.from(Buffer.from('builder-target'));

  assert.throws(() => spot.sendToChannel('svc').submit(), /requires at least one message/);
  const bridge = node.createRouteBridge();
  const publisher = node.createPublisher();
  assert.throws(() => bridge.send('svc', rid, rid).submit(), /requires at least one message/);
  assert.throws(() => bridge.request('svc', rid, rid).submit(), /requires at least one message/);
  assert.throws(() => publisher.publish('topic').submit(), /requires at least one message/);
  publisher.close();
  bridge.close();
  assert.throws(() => spot.requestToChannel('svc').submit(), /requires at least one message/);
  assert.throws(() => spot.replyToRouter(rid, 1n).submit(), /requires at least one message/);
  assert.throws(() => node.joinActor(actorRef, rid, rid).submit(() => {}), /requires at least one message/);

  const op = spot.sendToChannel('svc').message('payload');
  assert.throws(() => op.submit(), zlink.SubmitError);
  assert.throws(() => op.message('again'), /already been submitted/);

  const stream = zlink.createStreamSocket(ctx);
  assert.throws(() => stream.sendBoundActor(rid, actorRef.actorId).submit(), /requires at least one message/);
  assert.throws(() => actor.sendBoundSession().submit(), /requires at least one message/);
  actor.close();
  stream.close();

  spot.close();
  node.close();
  ctx.close();
});
