// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../dist/canonical');
const topic = 'room:lobby';
const sent = 'hello-spot';
async function reservePort() {
    const server = net.createServer();
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    const { port } = server.address();
    await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
    return port;
}
async function main() {
    const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const ctx = new zlink.Context();
    const pubNode = new zlink.SpotNode(ctx);
    const subNode = new zlink.SpotNode(ctx);
    const pub = pubNode.createSpot();
    const sub = subNode.createSpot();
    try {
        const receivedPromise = new Promise((resolve, reject) => {
            try {
                sub.onSubscribe((message) => {
                    resolve(message);
                });
            }
            catch (error) {
                reject(error);
            }
        });
        const timeoutPromise = new Promise((_, reject) => {
            setTimeout(() => reject(new Error('spot callback sample timed out')), 5000);
        });
        pubNode.bind(endpoint);
        subNode.connectPeer(endpoint);
        sub.setSubscription(topic);
        const publishTimer = setInterval(() => {
            pub.publish(topic, Buffer.from(sent));
        }, 25);
        const received = await Promise.race([receivedPromise, timeoutPromise]).finally(() => {
            clearInterval(publishTimer);
        });
        assert.ok(received.routingId instanceof zlink.RoutingId);
        assert.equal(received.topic, topic);
        const recv = received.parts[0].data().toString();
        assert.equal(recv, sent);
        console.log(`[spot/callback] publish: "${topic}/${sent}" \u2192 subscribe: "${topic}/${recv}"`);
    }
    finally {
        sub.close();
        pub.close();
        subNode.close();
        pubNode.close();
        ctx.close();
    }
    process.exit(0);
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
