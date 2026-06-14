// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const zlink = require('@zlink-systems/zlink');
const { tcpEndpoint, waitSpotPeerConnected } = require('./sample_support');
const AUTO_CONNECT_SPOT_MESH = 5;
const CHANNEL_NAME = 'sample';
function delay(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
}
async function main() {
    const ctx = zlink.createContext();
    const registry = zlink.createRegistry(ctx);
    const publisherDiscovery = zlink.createDiscovery(ctx, AUTO_CONNECT_SPOT_MESH, CHANNEL_NAME);
    const subscriberDiscovery = zlink.createDiscovery(ctx, AUTO_CONNECT_SPOT_MESH, CHANNEL_NAME);
    const publisherNode = zlink.createSpotNode(ctx);
    const subscriberNode = zlink.createSpotNode(ctx);
    let publisher = null;
    let subscriber = null;
    const topic = 'room:lobby';
    const sent = 'hello-spot';
    const registryPub = await tcpEndpoint();
    const registryRouter = await tcpEndpoint();
    const publisherEndpoint = await tcpEndpoint();
    const subscriberEndpoint = await tcpEndpoint();
    try {
        registry.bind(registryPub, registryRouter);
        registry.setBroadcastInterval(50);
        publisherDiscovery.connectRegistry(registryRouter);
        subscriberDiscovery.connectRegistry(registryRouter);
        publisherNode.setRoutingId(zlink.RoutingId.from(Buffer.from('z-node-spot-recv-publisher')));
        subscriberNode.setRoutingId(zlink.RoutingId.from(Buffer.from('a-node-spot-recv-subscriber')));
        publisherNode.attachDiscovery(publisherDiscovery);
        subscriberNode.attachDiscovery(subscriberDiscovery);
        publisherNode.setPubBind(publisherEndpoint);
        subscriberNode.setPubBind(subscriberEndpoint);
        publisher = publisherNode.createSpot();
        subscriber = subscriberNode.createSpot();
        publisher.setRoutingId(zlink.RoutingId.from(Buffer.from('z-node-spot-recv-publisher-spot')));
        subscriber.setRoutingId(zlink.RoutingId.from(Buffer.from('a-node-spot-recv-subscriber-spot')));
        subscriber.setSubscription(topic);
        await waitSpotPeerConnected(publisherNode);
        await waitSpotPeerConnected(subscriberNode);
        const deadline = Date.now() + 5000;
        const received = new zlink.TopicMessage();
        let hasReceived = false;
        while (Date.now() < deadline) {
            publisherNode.status();
            subscriberNode.status();
            subscriberNode.subjects();
            publisher.publish(topic).message(Buffer.from(sent)).submit();
            try {
                if (subscriber.subscribe(received, zlink.RecvFlags.DontWait)) {
                    hasReceived = true;
                    break;
                }
            }
            catch (error) {
                if (!(error instanceof zlink.RecvError)) {
                    throw error;
                }
                if (error.result !== zlink.RecvResult.NoData && error.nativeErrno !== 2) {
                    throw error;
                }
            }
            await delay(25);
        }
        if (!hasReceived) {
            throw new Error('spot delivery did not arrive');
        }
        try {
            assert.equal(received.topic, topic);
            const recv = received.parts[0].data().toString();
            assert.equal(recv, sent);
            console.log(`[spot/recv] channel: "${CHANNEL_NAME}" tick: 1 publish: "${topic}/${sent}" -> recv: "${topic}/${recv}"`);
        }
        finally {
            received.close();
        }
    }
    finally {
        if (subscriber) {
            subscriber.close();
        }
        if (publisher) {
            publisher.close();
        }
        subscriberNode.close();
        publisherNode.close();
        subscriberDiscovery.close();
        publisherDiscovery.close();
        registry.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
