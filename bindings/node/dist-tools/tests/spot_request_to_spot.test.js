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
async function waitForPeer(node) {
    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
        const peers = node.peersSnapshot();
        if (peers.some((peer) => peer.channelName === 'api'
            && peer.kind === zlink.SpotPeerKind.RouterChannel)) {
            await new Promise((resolve) => setTimeout(resolve, 250));
            return;
        }
        await new Promise((resolve) => setTimeout(resolve, 10));
    }
    throw new Error('spot router channel peer connection timed out');
}
test('router requestToSpot promise resolves through spot routed reply', async () => {
    const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const ctx = new zlink.Context();
    const responderNode = new zlink.SpotNode(ctx);
    const requester = new zlink.RouterSocket(ctx);
    const responder = responderNode.createSpot();
    try {
        requester.bind(endpoint);
        responderNode.connectRouterChannelPeer('api', endpoint);
        await waitForPeer(responderNode);
        const handled = new Promise((resolve, reject) => {
            const deadline = Date.now() + 5000;
            const received = new zlink.Received();
            const poll = () => {
                try {
                    if (!responder.recvRouted(received, zlink.RecvFlags.DontWait)) {
                        if (Date.now() < deadline) {
                            setImmediate(poll);
                            return;
                        }
                        reject(new Error('timed out waiting for routed request'));
                        return;
                    }
                    try {
                        assert.ok(received.routingId);
                        assert.equal(received.spotRid, null);
                        assert.notEqual(received.requestSeq, null);
                        assert.equal(received.parts.length, 1);
                        assert.equal(received.parts[0].data().toString(), 'spot-ping');
                        received.reply().message(Buffer.from('spot-pong')).submit();
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
        const reply = await requester.requestToSpot(responderNode.routingId, responder.routingId).message(Buffer.from('spot-ping')).timeout(2000).submitAsync();
        assert.equal(reply.length, 1);
        assert.equal(reply[0].data().toString(), 'spot-pong');
        await handled;
    }
    finally {
        responder.close();
        requester.close();
        responderNode.close();
        ctx.close();
    }
});
