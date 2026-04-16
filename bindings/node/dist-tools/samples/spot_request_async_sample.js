// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../dist/canonical');
const REQUEST_PAYLOAD = 'spot-ping';
const REPLY_PAYLOAD = 'spot-pong';
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
        const status = node.statusSnapshot();
        if (status.connectedPeerCount > 0 && node.peersSnapshot().length > 0) {
            return;
        }
        await new Promise((resolve) => setTimeout(resolve, 10));
    }
    throw new Error('spot peer connection timed out');
}
async function main() {
    const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const ctx = new zlink.Context();
    const requesterNode = new zlink.SpotNode(ctx);
    const responderNode = new zlink.SpotNode(ctx);
    let requester;
    let responder;
    let handledResolve;
    const handled = new Promise((resolve) => {
        handledResolve = resolve;
    });
    try {
        requester = requesterNode.createSpot();
        responder = responderNode.createSpot();
        responderNode.bind(endpoint);
        requesterNode.connectPeer(endpoint);
        await waitForPeer(requesterNode);
        const responderPromise = new Promise((resolve, reject) => {
            const deadline = Date.now() + 5000;
            const poll = () => {
                try {
                    const received = responder.recvRouted(zlink.RecvFlags.DontWait);
                    try {
                        assert.ok(received.routingId);
                        assert.ok(received.spotRid);
                        assert.notEqual(received.requestSeq, null);
                        assert.equal(received.parts.length, 1);
                        assert.equal(received.parts[0].data().toString(), REQUEST_PAYLOAD);
                        received.reply(zlink.Message.from(Buffer.from(REPLY_PAYLOAD)));
                        handledResolve();
                        resolve(null);
                    }
                    finally {
                        received.close();
                    }
                }
                catch (error) {
                    if (error instanceof zlink.RecvError && Date.now() < deadline) {
                        setImmediate(poll);
                        return;
                    }
                    reject(error);
                }
            };
            poll();
        });
        const reply = await requester.requestToSpot(responderNode.routingId, responder.routingId, Buffer.from(REQUEST_PAYLOAD), 2000);
        assert.equal(reply.length, 1);
        assert.equal(reply[0].data().toString(), REPLY_PAYLOAD);
        await responderPromise;
        await handled;
        console.log(`[spot/request/async] request: "${REQUEST_PAYLOAD}" -> reply: "${REPLY_PAYLOAD}"`);
    }
    finally {
        if (responder) {
            responder.close();
        }
        if (requester) {
            requester.close();
        }
        responderNode.close();
        requesterNode.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
