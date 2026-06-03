// SPDX-License-Identifier: MPL-2.0
//
// 자립형 가이드 예제: SPOT 토픽 pub/sub.
// 한 노드가 토픽에 publish하면, 그 토픽을 구독한 다른 노드가 받는다.
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const net = require('node:net');
const { once } = require('node:events');
const zlink = require('@zlink-systems/zlink');
async function reservePort() {
    const server = net.createServer();
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    const { port } = server.address();
    await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
    return port;
}
async function waitPeer(node) {
    const deadline = Date.now() + 15000;
    while (Date.now() < deadline) {
        if (node.status().connectedPeerCount > 0)
            return;
        await new Promise((resolve) => setTimeout(resolve, 25));
    }
    throw new Error('spot peer not connected');
}
async function main() {
    // --8<-- [start:doc]
    const ctx = zlink.createContext();
    const publisherNode = zlink.createSpotNode(ctx);
    const subscriberNode = zlink.createSpotNode(ctx);
    let publisher = null;
    let subscriber = null;
    const topic = 'room:lobby';
    try {
        const pubEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
        const subEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
        publisherNode.setPubBind(pubEndpoint);
        subscriberNode.setPubBind(subEndpoint);
        publisherNode.connectPeer(subEndpoint);
        subscriberNode.connectPeer(pubEndpoint);
        publisher = publisherNode.createSpot();
        subscriber = subscriberNode.createSpot();
        // 구독자는 받을 토픽을 등록한다.
        subscriber.setSubscription(topic);
        await waitPeer(publisherNode);
        await waitPeer(subscriberNode);
        // 연결 직후 첫 publish가 닿기 전일 수 있어, 도착할 때까지 반복 발행한다.
        const received = new zlink.TopicMessage();
        let delivered = false;
        const deadline = Date.now() + 5000;
        while (Date.now() < deadline) {
            publisher.publish(topic).message(Buffer.from('hello-everyone')).submit();
            if (subscriber.subscribe(received, zlink.RecvFlags.DontWait)) {
                delivered = true;
                break;
            }
            await new Promise((resolve) => setTimeout(resolve, 10));
        }
        assert.ok(delivered, 'spot delivery did not arrive');
        console.log(`[spot/pubsub] topic "${received.topic}" -> recv: "${received.parts[0].data().toString()}"`);
        received.close();
    }
    finally {
        if (publisher)
            publisher.close();
        if (subscriber)
            subscriber.close();
        publisherNode.close();
        subscriberNode.close();
        ctx.close();
    }
    // --8<-- [end:doc]
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
