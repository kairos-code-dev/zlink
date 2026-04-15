// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../dist/canonical');
const topic = 'room:lobby';
const sent = 'hello-spot';
const SERVICE_TYPE_SPOT = 0x3002;
const SERVICE_NAME = 'spot-child-service';
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
    const pubSendSocket = new zlink.PubSocket(ctx);
    const pubRecvSocket = new zlink.SubSocket(ctx);
    const subSendSocket = new zlink.PubSocket(ctx);
    const subRecvSocket = new zlink.SubSocket(ctx);
    let pub = null;
    let sub = null;
    try {
        pubNode.attachPubSub(SERVICE_NAME, pubSendSocket, pubRecvSocket);
        subNode.attachPubSub(SERVICE_NAME, subSendSocket, subRecvSocket);
        pub = pubNode.createSpot();
        sub = subNode.createSpot();
        pubSendSocket.bind(endpoint);
        subRecvSocket.connect(endpoint);
        sub.setSubscription(topic);
        const receivedPromise = new Promise((resolve, reject) => {
            const deadline = Date.now() + 5000;
            const poll = () => {
                try {
                    const received = sub.subscribe(zlink.RecvFlags.DontWait);
                    resolve(received);
                    return;
                }
                catch (error) {
                    if (error instanceof zlink.RecvError
                        && error.result === zlink.RecvResult.NoData) {
                        if (Date.now() >= deadline) {
                            reject(new Error('spot callback sample timed out'));
                            return;
                        }
                        setImmediate(poll);
                        return;
                    }
                    reject(error);
                }
            };
            poll();
        });
        const timeoutPromise = new Promise((_, reject) => {
            setTimeout(() => reject(new Error('spot callback sample timed out')), 5000);
        });
        const publishTimer = setInterval(() => {
            pub.publish(SERVICE_NAME, topic, Buffer.from(sent));
        }, 25);
        const received = await Promise.race([receivedPromise, timeoutPromise]).finally(() => {
            clearInterval(publishTimer);
        });
        assert.ok(received.routingId === null || received.routingId instanceof zlink.RoutingId);
        assert.equal(received.topic, topic);
        assert.equal(received.serviceName, SERVICE_NAME);
        const recv = received.parts[0].data().toString();
        assert.equal(recv, sent);
        console.log(`[spot/callback] publish: "${topic}/${sent}" \u2192 subscribe: "${topic}/${recv}"`);
    }
    finally {
        if (sub) {
            sub.close();
        }
        if (pub) {
            pub.close();
        }
        subNode.close();
        pubNode.close();
        subRecvSocket.close();
        subSendSocket.close();
        pubRecvSocket.close();
        pubSendSocket.close();
        ctx.close();
    }
    process.exit(0);
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
