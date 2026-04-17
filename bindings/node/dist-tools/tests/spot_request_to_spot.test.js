// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const test = require('node:test');
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
async function waitForPeer(node) {
    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
        if (node.statusSnapshot().connectedPeerCount > 0) {
            return;
        }
        await new Promise((resolve) => setTimeout(resolve, 10));
    }
    throw new Error('spot peer connection timed out');
}
test('spot requestToSpot promise resolves through routed reply', async () => {
    const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const ctx = new zlink.Context();
    const requesterNode = new zlink.SpotNode(ctx);
    const responderNode = new zlink.SpotNode(ctx);
    const requester = requesterNode.createSpot();
    const responder = responderNode.createSpot();
    try {
        responderNode.bind(endpoint);
        requesterNode.connectPeer(endpoint);
        await waitForPeer(requesterNode);
        const handled = (async () => {
            const deadline = Date.now() + 5000;
            while (Date.now() < deadline) {
                try {
                    const received = responder.recvRouted(zlink.RecvFlags.DontWait);
                    try {
                        assert.ok(received.routingId);
                        assert.ok(received.spotRid);
                        assert.notEqual(received.requestSeq, null);
                        assert.equal(received.parts.length, 1);
                        assert.equal(received.parts[0].data().toString(), 'spot-ping');
                        received.reply(zlink.Message.from(Buffer.from('spot-pong')));
                        return;
                    }
                    finally {
                        received.close();
                    }
                }
                catch (error) {
                    if (!(error instanceof zlink.RecvError)) {
                        throw error;
                    }
                }
                await new Promise((resolve) => setTimeout(resolve, 10));
            }
            throw new Error('spot routed request timed out');
        })();
        const reply = await requester.requestToSpot(responderNode.routingId, responder.routingId, Buffer.from('spot-ping'), 2000);
        assert.equal(reply.length, 1);
        assert.equal(reply[0].data().toString(), 'spot-pong');
        await handled;
    }
    finally {
        responder.close();
        requester.close();
        responderNode.close();
        requesterNode.close();
        ctx.close();
    }
});
