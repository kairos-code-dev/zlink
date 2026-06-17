// SPDX-License-Identifier: MPL-2.0

'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('@zlink-systems/zlink');

async function reservePort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const { port } = server.address();
  await new Promise<void>((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return port;
}

async function waitFor(condition, label, timeoutMs = 5000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (condition()) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
  throw new Error(`${label} timed out`);
}

test('spot setDispatchHandler permits subscribe drain after async callback delivery', async () => {
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const ctx = zlink.createContext();
  const publisherNode = zlink.createSpotNode(ctx);
  const subscriberNode = zlink.createSpotNode(ctx);
  const publisher = publisherNode.createSpot();
  const subscriber = subscriberNode.createSpot();
  let readableEvents = 0;

  try {
    publisherNode.setPubBind(endpoint);
    subscriber.setSubscription('dispatch-drain');
    subscriber.setDispatchHandler((info) => {
      if (info.event === zlink.SpotDispatchEvent.SubscribeReadable) {
        readableEvents += 1;
      }
    });
    subscriberNode.connectPeer(endpoint);

    await waitFor(
      () => subscriberNode.status().connectedPeerCount > 0,
      'spot peer connection'
    );

    const payload = Buffer.from('dispatch-payload');
    const publishDeadline = Date.now() + 5000;
    while (readableEvents === 0 && Date.now() < publishDeadline) {
      publisher.publish('dispatch-drain').message(payload).flags(zlink.SendFlags.DontWait).submit();
      await new Promise((resolve) => setTimeout(resolve, 10));
    }
    assert.notEqual(readableEvents, 0);

    const received = new zlink.TopicMessage();
    assert.equal(subscriber.subscribe(received, zlink.RecvFlags.DontWait), true);
    try {
      assert.equal(received.topic, 'dispatch-drain');
      assert.equal(received.parts[0].data().toString(), 'dispatch-payload');
    } finally {
      received.close();
    }
  } finally {
    subscriber.close();
    publisher.close();
    subscriberNode.close();
    publisherNode.close();
    ctx.close();
  }
});

async function waitActorJoin(spot, label) {
  const deadline = Date.now() + 2000;
  while (Date.now() < deadline) {
    const request = spot.recvActorJoin(zlink.RecvFlags.DontWait);
    if (request) {
      return request;
    }
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
  throw new Error(`${label} actor join timed out`);
}

test('entry spot join carries request and reply parts', async () => {
  const ctx = zlink.createContext();
  const node = zlink.createSpotNode(ctx, zlink.SpotNodeMode.All);
  const userSpot = node.createSpot();
  const entrySpot = node.entrySpot();
  const actor = node.createActor('node-entry-join');

  try {
    const userJoin = actor.join(userSpot)
      .message(Buffer.from('user-join-request'))
      .timeout(1000)
      .submit();
    const userRequest = await waitActorJoin(userSpot, 'user spot');
    assert.equal(userRequest.message.data().toString(), 'user-join-request');
    userSpot.replyActorJoin(userRequest, 0)
      .message(Buffer.from('user-reply'))
      .submit();
    const userResult = await userJoin;
    assert.equal(userResult.result.result, zlink.RequestResult.Ok);

    const entryJoin = node.joinActorEntrySpot(
      actor.ref(),
      node.routingId,
      Buffer.from('entry-join-request')
    )
      .timeout(1000)
      .submit();
    const entryRequest = await waitActorJoin(entrySpot, 'entry spot');
    assert.equal(entryRequest.message.data().toString(), 'entry-join-request');
    entrySpot.replyActorJoin(entryRequest, 0)
      .message(Buffer.from('entry-reply'))
      .submit();
    const entryResult = await entryJoin;

    assert.equal(entryResult.result.result, zlink.RequestResult.Ok);
    assert.equal(entryResult.result.joinResultCode, 0);
    assert.equal(entryResult.parts.length, 1);
    assert.equal(entryResult.parts[0].data().toString(), 'entry-reply');

    const returnedToUser = actor.join(userSpot)
      .message(Buffer.from('return-to-user'))
      .timeout(1000)
      .submit();
    const returnRequest = await waitActorJoin(userSpot, 'user spot return');
    userSpot.replyActorJoin(returnRequest, 0)
      .message(Buffer.from('user-return-reply'))
      .submit();
    const returnResult = await returnedToUser;
    assert.equal(returnResult.result.result, zlink.RequestResult.Ok);

    const rejectedEntryJoin = node.joinActorEntrySpot(
      actor.ref(),
      node.routingId,
      Buffer.from('entry-reject-request')
    )
      .timeout(1000)
      .submit();
    const rejectedEntryRequest = await waitActorJoin(entrySpot, 'entry spot reject');
    assert.equal(rejectedEntryRequest.message.data().toString(), 'entry-reject-request');
    entrySpot.replyActorJoin(rejectedEntryRequest, 7)
      .message(Buffer.from('entry-rejected'))
      .submit();
    const rejectedEntryResult = await rejectedEntryJoin;

    assert.equal(rejectedEntryResult.result.result, zlink.RequestResult.Ok);
    assert.equal(rejectedEntryResult.result.joinResultCode, 7);
    assert.equal(rejectedEntryResult.parts.length, 1);
    assert.equal(rejectedEntryResult.parts[0].data().toString(), 'entry-rejected');
    const userActors = userSpot.actors();
    assert.equal(userActors.length, 1);
    assert.equal(userActors[0].actorId, actor.ref().actorId);

    const cleanupEntryJoin = node.joinActorEntrySpot(
      actor.ref(),
      node.routingId,
      Buffer.from('entry-cleanup-request')
    )
      .timeout(1000)
      .submit();
    const cleanupEntryRequest = await waitActorJoin(entrySpot, 'entry spot cleanup');
    entrySpot.replyActorJoin(cleanupEntryRequest, 0)
      .message(Buffer.from('entry-cleanup'))
      .submit();
    const cleanupEntryResult = await cleanupEntryJoin;
    assert.equal(cleanupEntryResult.result.result, zlink.RequestResult.Ok);
  } finally {
    actor.close();
    entrySpot.close();
    userSpot.close();
    node.close();
    ctx.close();
  }
});
