// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../dist/canonical');
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
    const topic = 'room:lobby';
    const sent = 'hello-spot';
    try {
        pubNode.bind(endpoint);
        subNode.connectPeer(endpoint);
        sub.setSubscription(topic);
        const deadline = Date.now() + 5000;
        let received = null;
        while (Date.now() < deadline) {
            pub.publish(topic, Buffer.from(sent));
            try {
                received = sub.subscribe(zlink.RecvFlags.DontWait);
                break;
            }
            catch (error) {
                if (!(error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData)) {
                    throw error;
                }
            }
            await new Promise((resolve) => setTimeout(resolve, 25));
        }
        assert.notEqual(received, null);
        assert.equal(received.topic, topic);
        const recv = received.parts[0].data().toString();
        assert.equal(recv, sent);
        console.log(`[spot/recv] publish: "${topic}/${sent}" \u2192 subscribe: "${topic}/${recv}"`);
    }
    finally {
        sub.close();
        pub.close();
        subNode.close();
        pubNode.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
