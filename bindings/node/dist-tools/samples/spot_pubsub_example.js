// SPDX-License-Identifier: MPL-2.0
//
// 자립형 가이드 예제: RouteMesh 채널 토픽 pub/sub.
// 한 노드의 publisher가 채널 토픽에 publish하면, 그 토픽을 구독한 다른 노드의
// spot이 pull dispatch로 받는다.
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const zlink = require('@zlink-systems/zlink');
const { MeshPump, tcpEndpoint, waitPeerAdmitted } = require('./sample_support');
const CHANNEL = 'rooms';
async function main() {
    // --8<-- [start:doc]
    const ctx = zlink.createContext();
    const publisherNode = zlink.createMeshNode(ctx, { meshName: 'samples' });
    const subscriberNode = zlink.createMeshNode(ctx, { meshName: 'samples' });
    let subscriberPump = null;
    let publisher = null;
    let subscriber = null;
    const topic = 'room:lobby';
    try {
        const pubEndpoint = await tcpEndpoint();
        const subEndpoint = await tcpEndpoint();
        publisherNode.setBind(pubEndpoint);
        subscriberNode.setBind(subEndpoint);
        // 두 노드가 같은 채널을 광고해야 구독이 상대에게 전파된다.
        publisherNode.addChannelName(CHANNEL);
        subscriberNode.addChannelName(CHANNEL);
        publisherNode.start();
        subscriberNode.start();
        publisherNode.connectPeer({ endpoint: subEndpoint });
        subscriberNode.connectPeer({ endpoint: pubEndpoint });
        publisher = publisherNode.createPublisher();
        subscriber = subscriberNode.createSpot();
        // 구독자는 받을 채널·토픽을 등록한다.
        subscriber.setSubscription(CHANNEL, topic, zlink.SubscriptionKind.Exact);
        subscriberPump = new MeshPump(subscriberNode);
        await waitPeerAdmitted(publisherNode);
        await waitPeerAdmitted(subscriberNode);
        // 연결 직후 첫 publish가 닿기 전일 수 있어, 도착할 때까지 반복 발행한다.
        let received = null;
        const deadline = Date.now() + 5000;
        while (Date.now() < deadline && received === null) {
            publisher.publish(CHANNEL, topic, Buffer.from('hello-everyone'));
            subscriberPump.drain((record) => {
                // A published delivery carries the channel topic it was sent on.
                if (record.topic !== null)
                    received = record;
            });
            if (received === null)
                await new Promise((resolve) => setTimeout(resolve, 10));
        }
        assert.ok(received, 'spot delivery did not arrive');
        console.log(`[spot/pubsub] topic "${received.topic}" -> recv: "${received.parts[0].data().toString()}"`);
    }
    finally {
        if (subscriberPump)
            subscriberPump.close();
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
