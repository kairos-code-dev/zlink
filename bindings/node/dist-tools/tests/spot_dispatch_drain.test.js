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
