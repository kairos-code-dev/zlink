// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
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
    await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
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
        await waitFor(() => subscriberNode.status().connectedPeerCount > 0, 'spot peer connection');
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
        }
        finally {
            received.close();
        }
    }
    finally {
        subscriber.close();
        publisher.close();
        subscriberNode.close();
        publisherNode.close();
        ctx.close();
    }
});
test('entry spot setDispatchHandler permits multipart subscribe drain after peer publish', async () => {
    const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const ctx = zlink.createContext();
    const publisherNode = zlink.createSpotNode(ctx);
    const subscriberNode = zlink.createSpotNode(ctx);
    const publisher = publisherNode.createSpot();
    const subscriber = subscriberNode.entrySpot();
    let readableEvents = 0;
    try {
        publisherNode.setPubBind(endpoint);
        subscriber.setSubscription('entry-dispatch-drain');
        subscriber.setDispatchHandler((info) => {
            if (info.event === zlink.SpotDispatchEvent.SubscribeReadable) {
                readableEvents += 1;
            }
        });
        subscriberNode.connectPeer(endpoint);
        await waitFor(() => subscriberNode.status().connectedPeerCount > 0, 'entry spot peer connection');
        const publishDeadline = Date.now() + 5000;
        while (readableEvents === 0 && Date.now() < publishDeadline) {
            publisher.publish('entry-dispatch-drain')
                .message(Buffer.from('header'))
                .message(Buffer.from('payload'))
                .flags(zlink.SendFlags.DontWait)
                .submit();
            await new Promise((resolve) => setTimeout(resolve, 10));
        }
        assert.notEqual(readableEvents, 0);
        const received = new zlink.TopicMessage();
        assert.equal(subscriber.subscribe(received, zlink.RecvFlags.DontWait), true);
        try {
            assert.equal(received.topic, 'entry-dispatch-drain');
            assert.deepEqual(received.parts.map((part) => part.data().toString()), ['header', 'payload']);
        }
        finally {
            received.close();
        }
    }
    finally {
        subscriber.close();
        publisher.close();
        subscriberNode.close();
        publisherNode.close();
        ctx.close();
    }
});
test('spot setDispatchHandler carries routed request payload for async callback delivery', async () => {
    const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const ctx = zlink.createContext();
    const responderNode = zlink.createSpotNode(ctx);
    const requester = zlink.createRouterSocket(ctx);
    const responder = responderNode.createSpot();
    try {
        requester.bind(endpoint);
        responderNode.connectRouterChannelPeerRid('api', responderNode.routingId, endpoint);
        await waitFor(() => responderNode.peers().some((peer) => peer.channelName === 'api'
            && peer.kind === zlink.SpotPeerKind.RouterChannel), 'spot router channel peer connection');
        const handled = new Promise((resolve, reject) => {
            responder.setDispatchHandler((info) => {
                if (info.event !== zlink.SpotDispatchEvent.RoutedReadable) {
                    return;
                }
                try {
                    assert.notEqual(info.routed, null);
                    assert.ok(info.routed.routingId);
                    assert.equal(info.routed.spotRid, null);
                    assert.notEqual(info.routed.requestSeq, null);
                    assert.equal(info.routed.parts.length, 1);
                    assert.equal(info.routed.parts[0].data().toString(), 'dispatch-routed-body');
                    info.routed.reply()
                        .message(Buffer.from('dispatch-routed-reply-header'))
                        .message(Buffer.from('dispatch-routed-reply-body'))
                        .submit();
                    resolve();
                }
                catch (error) {
                    reject(error);
                }
                finally {
                    info.routed?.close();
                }
            });
        });
        const reply = await requester.requestToSpot(responderNode.routingId, responder.routingId)
            .message(Buffer.from('dispatch-routed-body'))
            .timeout(2000)
            .submit();
        assert.equal(reply.length, 2);
        assert.equal(reply[0].data().toString(), 'dispatch-routed-reply-header');
        assert.equal(reply[1].data().toString(), 'dispatch-routed-reply-body');
        await handled;
    }
    finally {
        responder.close();
        requester.close();
        responderNode.close();
        ctx.close();
    }
});
test('spot setDispatchHandler replies to routed spot request origin', async () => {
    const responderPeerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const responderRouterEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const requesterPeerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const requesterRouterEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const responderCtx = zlink.createContext();
    const requesterCtx = zlink.createContext();
    const responderNode = zlink.createSpotNode(responderCtx);
    const requesterNode = zlink.createSpotNode(requesterCtx);
    const responder = responderNode.createSpot();
    const requester = requesterNode.createSpot();
    try {
        responderNode.setRoutingId(zlink.RoutingId.from(Buffer.from('dispatch-responder-node')));
        requesterNode.setRoutingId(zlink.RoutingId.from(Buffer.from('dispatch-requester-node')));
        responder.setRoutingId(zlink.RoutingId.from(Buffer.from('dispatch-responder-spot')));
        requester.setRoutingId(zlink.RoutingId.from(Buffer.from('dispatch-requester-spot')));
        responderNode.setRouterBind(responderRouterEndpoint);
        responderNode.setPubBind(responderPeerEndpoint);
        requesterNode.setRouterBind(requesterRouterEndpoint);
        requesterNode.setPubBind(requesterPeerEndpoint);
        responderNode.connectPeer(requesterPeerEndpoint);
        requesterNode.connectPeer(responderPeerEndpoint);
        await Promise.all([
            waitFor(() => responderNode.status().connectedPeerCount > 0, 'responder spot peer connection'),
            waitFor(() => requesterNode.status().connectedPeerCount > 0, 'requester spot peer connection')
        ]);
        const handled = new Promise((resolve, reject) => {
            responder.setDispatchHandler((info) => {
                if (info.event !== zlink.SpotDispatchEvent.RoutedReadable) {
                    return;
                }
                try {
                    assert.notEqual(info.routed, null);
                    assert.ok(info.routed.routingId);
                    assert.ok(info.routed.spotRid);
                    assert.notEqual(info.routed.requestSeq, null);
                    assert.equal(info.routed.parts.length, 1);
                    assert.equal(info.routed.parts[0].data().toString(), 'spot-routed-body');
                    info.routed.reply()
                        .message(Buffer.from('spot-routed-reply'))
                        .submit();
                    resolve();
                }
                catch (error) {
                    reject(error);
                }
                finally {
                    info.routed?.close();
                }
            });
        });
        const reply = await requester.requestToSpot(responderNode.routingId, responder.routingId)
            .message(Buffer.from('spot-routed-body'))
            .timeout(2000)
            .submit();
        assert.equal(reply.length, 1);
        assert.equal(reply[0].data().toString(), 'spot-routed-reply');
        await handled;
    }
    finally {
        requester.close();
        responder.close();
        requesterNode.close();
        responderNode.close();
        requesterCtx.close();
        responderCtx.close();
    }
});
test('entry spot setDispatchHandler replies to routed spot request origin', async () => {
    const responderPeerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const responderRouterEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const requesterPeerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const requesterRouterEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const responderCtx = zlink.createContext();
    const requesterCtx = zlink.createContext();
    const responderNode = zlink.createSpotNode(responderCtx);
    const requesterNode = zlink.createSpotNode(requesterCtx);
    const responder = responderNode.entrySpot();
    const requester = requesterNode.createSpot();
    try {
        responderNode.setRoutingId(zlink.RoutingId.from(Buffer.from('dispatch-entry-responder-node')));
        requesterNode.setRoutingId(zlink.RoutingId.from(Buffer.from('dispatch-entry-requester-node')));
        requester.setRoutingId(zlink.RoutingId.from(Buffer.from('dispatch-entry-requester-spot')));
        responderNode.setRouterBind(responderRouterEndpoint);
        responderNode.setPubBind(responderPeerEndpoint);
        requesterNode.setRouterBind(requesterRouterEndpoint);
        requesterNode.setPubBind(requesterPeerEndpoint);
        responderNode.connectPeer(requesterPeerEndpoint);
        requesterNode.connectPeer(responderPeerEndpoint);
        await Promise.all([
            waitFor(() => responderNode.status().connectedPeerCount > 0, 'entry responder spot peer connection'),
            waitFor(() => requesterNode.status().connectedPeerCount > 0, 'entry requester spot peer connection')
        ]);
        const handled = new Promise((resolve, reject) => {
            responder.setDispatchHandler((info) => {
                if (info.event !== zlink.SpotDispatchEvent.RoutedReadable) {
                    return;
                }
                try {
                    assert.notEqual(info.routed, null);
                    assert.ok(info.routed.routingId);
                    assert.ok(info.routed.spotRid);
                    assert.notEqual(info.routed.requestSeq, null);
                    assert.equal(info.routed.parts.length, 1);
                    assert.equal(info.routed.parts[0].data().toString(), 'entry-routed-body');
                    info.routed.reply()
                        .message(Buffer.from('entry-routed-reply'))
                        .submit();
                    resolve();
                }
                catch (error) {
                    reject(error);
                }
                finally {
                    info.routed?.close();
                }
            });
        });
        const reply = await requester.requestToSpot(responderNode.routingId, responder.routingId)
            .message(Buffer.from('entry-routed-body'))
            .timeout(2000)
            .submit();
        assert.equal(reply.length, 1);
        assert.equal(reply[0].data().toString(), 'entry-routed-reply');
        await handled;
    }
    finally {
        requester.close();
        responderNode.close();
        requesterNode.close();
        responderCtx.close();
        requesterCtx.close();
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
        const entryJoin = node.joinActorEntrySpot(actor.ref(), node.routingId, Buffer.from('entry-join-request'))
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
        const rejectedEntryJoin = node.joinActorEntrySpot(actor.ref(), node.routingId, Buffer.from('entry-reject-request'))
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
        const cleanupEntryJoin = node.joinActorEntrySpot(actor.ref(), node.routingId, Buffer.from('entry-cleanup-request'))
            .timeout(1000)
            .submit();
        const cleanupEntryRequest = await waitActorJoin(entrySpot, 'entry spot cleanup');
        entrySpot.replyActorJoin(cleanupEntryRequest, 0)
            .message(Buffer.from('entry-cleanup'))
            .submit();
        const cleanupEntryResult = await cleanupEntryJoin;
        assert.equal(cleanupEntryResult.result.result, zlink.RequestResult.Ok);
    }
    finally {
        actor.close();
        entrySpot.close();
        userSpot.close();
        node.close();
        ctx.close();
    }
});
